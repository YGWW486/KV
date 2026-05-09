#include "network/Connection.h"
#include "network/ConnectionPool.h"
#include "network/InetAddress.h"
#include "protocol/RESPParser.h"
#include "commands/CommandDispatcher.h"
#include "storage/SegmentedMemoryStorageEngine.h"
#include "storage/PersistenceEngine.h"
#include "utils/Logging.h"
#include "utils/Config.h"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>

#if defined(_WIN32)

#include "network/iocp/IOCPLoop.h"
#include <winsock2.h>
#include <ws2tcpip.h>

using namespace kvstore;

static IOCPLoop* g_loop = nullptr;

static BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT) {
        if (g_loop) {
            g_loop->quit();
        }
        return TRUE;
    }
    return FALSE;
}

class KvServer {
public:
    KvServer(IOCPLoop* loop, uint16_t port)
        : loop_(loop)
        , aof_("kvstore.aof")
        , rdb_("kvstore.rdb")
        , dispatcher_(&storage_, &aof_) {
        rdb_.load(&storage_);
        aof_.load(&storage_);

        listenSock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSock_ == INVALID_SOCKET) {
            throw std::runtime_error("KvServer: socket failed: " + std::to_string(WSAGetLastError()));
        }

        u_long mode = 1;
        if (ioctlsocket(listenSock_, FIONBIO, &mode) != 0) {
            int err = WSAGetLastError();
            closesocket(listenSock_);
            listenSock_ = INVALID_SOCKET;
            throw std::runtime_error("KvServer: ioctlsocket FIONBIO failed: " + std::to_string(err));
        }

        int on = 1;
        setsockopt(listenSock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        if (bind(listenSock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            int err = WSAGetLastError();
            closesocket(listenSock_);
            listenSock_ = INVALID_SOCKET;
            throw std::runtime_error("KvServer: bind failed: " + std::to_string(err));
        }
        if (::listen(listenSock_, SOMAXCONN) == SOCKET_ERROR) {
            int err = WSAGetLastError();
            closesocket(listenSock_);
            listenSock_ = INVALID_SOCKET;
            throw std::runtime_error("KvServer: listen failed: " + std::to_string(err));
        }
    }

    ~KvServer() {
        if (listenSock_ != INVALID_SOCKET) {
            closesocket(listenSock_);
            listenSock_ = INVALID_SOCKET;
        }
    }

    void start() {
        loop_->runEvery(0.05, [this]() { acceptLoop(); });
        loop_->runEvery(1.0, [this]() { aof_.syncAOF(); });
        loop_->runEvery(30.0, [this]() { rdb_.save(&storage_); });
    }

private:
    void acceptLoop() {
        if (listenSock_ == INVALID_SOCKET) {
            return;
        }
        // Accept all pending connections in one batch
        while (true) {
            sockaddr_in addr{};
            int addrlen = sizeof(addr);
            SOCKET connfd = ::accept(listenSock_, reinterpret_cast<sockaddr*>(&addr), &addrlen);
            if (connfd == INVALID_SOCKET) {
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK) break;
                if (err != WSAECONNRESET) {
                    LOG_WARN << "Accept error: " << err;
                }
                break;
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
    SOCKET listenSock_ = INVALID_SOCKET;
    ConnectionPool connPool_;
    RESPParser parser_;
    SegmentedMemoryStorageEngine storage_;
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
    LOG_INFO << "  KV-Store Server v0.1.0 (Windows/IOCP)";
    LOG_INFO << "========================================";

    try {
        IOCPLoop loop;
        KvServer server(&loop, static_cast<uint16_t>(port));
        g_loop = &loop;
        server.start();

        LOG_INFO << "KV-Store listening on port " << port;

        loop.loop();
    } catch (const std::exception& e) {
        LOG_ERROR << e.what();
        g_loop = nullptr;
        WSACleanup();
        return 1;
    }

    g_loop = nullptr;
    WSACleanup();
    LOG_INFO << "KV-Store shutting down...";
    LOG_INFO << "KV-Store stopped.";
    return 0;
}

#elif defined(__linux__)

#include "network/epoll/EpollLoop.h"
#include "network/Acceptor.h"

#include <signal.h>
#include <sys/socket.h>

using namespace kvstore;

static EpollLoop* g_loop = nullptr;

static void onSignal(int /*sig*/) {
    if (g_loop) {
        g_loop->quit();
    }
}

class KvServer {
public:
    KvServer(EpollLoop* loop, uint16_t port)
        : loop_(loop)
        , acceptor_(loop, InetAddress(port), true)
        , aof_("kvstore.aof")
        , rdb_("kvstore.rdb")
        , dispatcher_(&storage_, &aof_) {
        rdb_.load(&storage_);
        aof_.load(&storage_);
        acceptor_.setNewConnectionCallback(
            std::bind(&KvServer::onNewConnection, this, std::placeholders::_1, std::placeholders::_2));
    }

    void start() {
        acceptor_.listen();
        loop_->runEvery(1.0, [this]() { aof_.syncAOF(); });
        loop_->runEvery(30.0, [this]() { rdb_.save(&storage_); });
    }

private:
    void onNewConnection(socket_t sockfd, const InetAddress& peerAddr) {
        std::string connName = "KV-" + std::to_string(nextConnId_);
        InetAddress localAddr;
        sockaddr_in la{};
        socklen_t len = sizeof(la);
        if (::getsockname(sockfd, reinterpret_cast<sockaddr*>(&la), &len) == 0) {
            localAddr.setSockAddrInet(la);
        }
        LOG_INFO << "New connection [" << connName << "] from " << peerAddr.toIpPort();

        auto conn = std::make_shared<Connection>(loop_, connName, sockfd, localAddr, peerAddr);
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
            auto t0 = std::chrono::high_resolution_clock::now();

            std::shared_ptr<RESPObject> request;
            ParseResult result = parser_.parse(buf, &request);
            auto t1 = std::chrono::high_resolution_clock::now();

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
            auto t2 = std::chrono::high_resolution_clock::now();

            std::string wire = response->toString();
            auto t3 = std::chrono::high_resolution_clock::now();

            conn->send(wire);
            auto t4 = std::chrono::high_resolution_clock::now();

            // accumulate timing (in nanoseconds)
            parse_ns_   += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            dispatch_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
            serialize_ns_+= std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count();
            send_ns_    += std::chrono::duration_cast<std::chrono::nanoseconds>(t4 - t3).count();
            req_count_++;

            if (req_count_ % 10000 == 0) {
                double div = static_cast<double>(req_count_);
                LOG_INFO << "=== PERF BREAKDOWN (n=" << req_count_ << ") ===";
                LOG_INFO << "  parse:    " << (parse_ns_ / div / 1000.0) << " us/req";
                LOG_INFO << "  dispatch: " << (dispatch_ns_ / div / 1000.0) << " us/req";
                LOG_INFO << "  serialize:" << (serialize_ns_ / div / 1000.0) << " us/req";
                LOG_INFO << "  send:     " << (send_ns_ / div / 1000.0) << " us/req";
                LOG_INFO << "  TOTAL:    " << ((parse_ns_+dispatch_ns_+serialize_ns_+send_ns_) / div / 1000.0) << " us/req";
            }
        }
    }

    EpollLoop* loop_;
    Acceptor acceptor_;
    ConnectionPool connPool_;
    RESPParser parser_;
    SegmentedMemoryStorageEngine storage_;
    AOFPersistenceEngine aof_;
    RDBPersistenceEngine rdb_;
    CommandDispatcher dispatcher_;
    int nextConnId_ = 1;

    // perf counters
    int64_t req_count_ = 0;
    int64_t parse_ns_ = 0;
    int64_t dispatch_ns_ = 0;
    int64_t serialize_ns_ = 0;
    int64_t send_ns_ = 0;
};

int main() {
    LoggerImpl::instance().setLogLevel(INFO);

    ::signal(SIGINT, onSignal);
    ::signal(SIGTERM, onSignal);
    ::signal(SIGPIPE, SIG_IGN);

    Config config;
    config.set("server.port", "6379");
    int port = config.get<int>("server.port", 6379);

    LOG_INFO << "========================================";
    LOG_INFO << "  KV-Store Server v0.1.0 (Linux/epoll)";
    LOG_INFO << "========================================";

    try {
        EpollLoop loop;
        g_loop = &loop;
        KvServer server(&loop, static_cast<uint16_t>(port));
        server.start();

        LOG_INFO << "KV-Store listening on port " << port;

        loop.loop();
    } catch (const std::exception& e) {
        LOG_ERROR << e.what();
        g_loop = nullptr;
        return 1;
    }

    g_loop = nullptr;
    LOG_INFO << "KV-Store shutting down...";
    LOG_INFO << "KV-Store stopped.";
    return 0;
}

#else

#error "kv-server is only supported on Windows or Linux"

#endif
