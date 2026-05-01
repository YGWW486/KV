#include "network/iocp/IOCPLoop.h"
#include "network/Connection.h"
#include "network/ConnectionPool.h"
#include "network/InetAddress.h"
#include "protocol/RESPParser.h"
#include "commands/CommandDispatcher.h"
#include "storage/MemoryStorageEngine.h"
#include "storage/PersistenceEngine.h"
#include "utils/Logging.h"
#include "utils/Config.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <memory>

using namespace kvstore;

static IOCPLoop* g_loop = nullptr;

static BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT) {
        if (g_loop) g_loop->quit();
        return TRUE;
    }
    return FALSE;
}

class KvServer {
public:
    KvServer(IOCPLoop* loop, uint16_t port)
        : loop_(loop), aof_("kvstore.aof"), rdb_("kvstore.rdb"), dispatcher_(&storage_, &aof_) {
        // 先加载 RDB 快照（更快），再重放 AOF（增量）
        rdb_.load(&storage_);
        aof_.load(&storage_);

        listenSock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        u_long mode = 1;
        ioctlsocket(listenSock_, FIONBIO, &mode);

        int on = 1;
        setsockopt(listenSock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        bind(listenSock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ::listen(listenSock_, SOMAXCONN);
    }

    void start() {
        loop_->runEvery(0.5, [this]() { acceptLoop(); });
        // EVERYSEC 策略：每 1 秒 sync AOF
        loop_->runEvery(1.0, [this]() { aof_.syncAOF(); });
        // RDB 定期快照：每 30 秒
        loop_->runEvery(30.0, [this]() { rdb_.save(&storage_); });
    }

private:
    void acceptLoop() {
        sockaddr_in addr{};
        int addrlen = sizeof(addr);
        SOCKET connfd = ::accept(listenSock_, reinterpret_cast<sockaddr*>(&addr), &addrlen);
        if (connfd == INVALID_SOCKET) {
            return;
        }

        std::string connName = "KV-" + std::to_string(nextConnId_);
        InetAddress peerAddr(addr);
        InetAddress localAddr;
        LOG_INFO << "New connection [" << connName << "] from " << peerAddr.toIpPort();

        auto conn = std::make_shared<Connection>(loop_, connName, connfd, localAddr, peerAddr);
        connPool_.addConnection(conn);

        conn->setConnectionCallback(
            std::bind(&KvServer::onConnection, this, std::placeholders::_1));
        conn->setMessageCallback(
            std::bind(&KvServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        conn->connectEstablished();
        nextConnId_++;
    }

    void onConnection(const std::shared_ptr<Connection>& conn) {
        if (conn->connected()) {
            LOG_INFO << "Connection [" << conn->name() << "] UP";
        } else {
            LOG_INFO << "Connection [" << conn->name() << "] DOWN";
            connPool_.removeConnection(conn);
        }
    }

    void onMessage(const std::shared_ptr<Connection>& conn, Buffer* buf, Timestamp) {
        while (buf->readableBytes() > 0) {
            std::shared_ptr<RESPObject> request;
            ParseResult result = parser_.parse(buf, &request);

            if (result == ParseResult::Incomplete) {
                return;
            }

            if (result == ParseResult::Error) {
                LOG_ERROR << "Parse error from [" << conn->name() << "], closing";
                conn->send(RESPError("ERR protocol error").toString());
                conn->shutdown();
                return;
            }

            std::shared_ptr<RESPObject> response = dispatcher_.dispatch(request);
            conn->send(response->toString());
        }
    }

    IOCPLoop* loop_;
    SOCKET listenSock_;
    ConnectionPool connPool_;
    RESPParser parser_;
    MemoryStorageEngine storage_;
    AOFPersistenceEngine aof_;
    RDBPersistenceEngine rdb_;
    CommandDispatcher dispatcher_;
    int nextConnId_ = 1;
};

int main() {
    LoggerImpl::instance().setLogLevel(INFO);

    SetConsoleCtrlHandler(consoleHandler, TRUE);

    Config config;
    config.set("server.port", "6379");
    int port = config.get<int>("server.port", 6379);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_FATAL << "WSAStartup failed";
        return 1;
    }

    LOG_INFO << "========================================";
    LOG_INFO << "  KV-Store Server v0.1.0";
    LOG_INFO << "========================================";

    IOCPLoop loop;
    g_loop = &loop;

    KvServer server(&loop, static_cast<uint16_t>(port));
    server.start();

    LOG_INFO << "KV-Store listening on port " << port;

    loop.loop();

    LOG_INFO << "KV-Store shutting down...";
    WSACleanup();
    LOG_INFO << "KV-Store stopped.";
    return 0;
}
