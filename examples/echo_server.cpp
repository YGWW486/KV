#include "network/Acceptor.h"
#include "network/Connection.h"
#include "network/ConnectionPool.h"
#include "network/InetAddress.h"
#include "utils/Logging.h"

#include <memory>
#include <stdexcept>

#if defined(_WIN32)

#include "network/iocp/IOCPLoop.h"
#include <winsock2.h>
#include <ws2tcpip.h>

using namespace kvstore;

class EchoServer {
public:
    EchoServer(IOCPLoop* loop, const InetAddress& listenAddr)
        : loop_(loop)
        , acceptor_(loop, listenAddr, false) {
        acceptor_.setNewConnectionCallback(
            std::bind(&EchoServer::onNewConnection, this, std::placeholders::_1, std::placeholders::_2));
    }

    void start() { acceptor_.listen(); }

private:
    void onNewConnection(socket_t sockfd, const InetAddress& peerAddr) {
        std::string connName = "Connection-" + std::to_string(nextConnId_);
        LOG_INFO << "EchoServer - new connection [" << connName << "] from " << peerAddr.toIpPort();

        InetAddress localAddr;
        auto conn = std::make_shared<Connection>(loop_, connName, sockfd, localAddr, peerAddr);

        connPool_.addConnection(conn);

        conn->setConnectionCallback(std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        conn->setMessageCallback(std::bind(&EchoServer::onMessage, this, std::placeholders::_1,
                                           std::placeholders::_2, std::placeholders::_3));
        conn->connectEstablished();

        nextConnId_++;
    }

    void onConnection(const std::shared_ptr<Connection>& conn) {
        LOG_INFO << "EchoServer - connection " << conn->name() << " is " << (conn->connected() ? "UP" : "DOWN");
        if (!conn->connected()) {
            connPool_.removeConnection(conn);
        }
    }

    void onMessage(const std::shared_ptr<Connection>& conn, Buffer* buf, Timestamp time) {
        std::string msg = buf->retrieveAllAsString();
        LOG_INFO << "EchoServer - received " << msg.size() << " bytes at " << time.toFormattedString(false);
        conn->send(msg);
    }

    IOCPLoop* loop_;
    Acceptor acceptor_;
    ConnectionPool connPool_;
    int nextConnId_ = 1;
};

int main() {
    LoggerImpl::instance().setLogLevel(INFO);
    LOG_INFO << "=== Echo Server starting (Windows/IOCP) ===";

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_FATAL << "WSAStartup failed";
        return 1;
    }

    try {
        IOCPLoop loop;
        InetAddress listenAddr(6379);
        EchoServer server(&loop, listenAddr);
        server.start();

        LOG_INFO << "EchoServer listening on port " << listenAddr.port() << ", waiting for connections...";
        loop.loop();
    } catch (const std::exception& e) {
        LOG_ERROR << e.what();
        WSACleanup();
        return 1;
    }

    WSACleanup();
    LOG_INFO << "=== Echo Server stopped ===";
    return 0;
}

#elif defined(__linux__)

#include "network/epoll/EpollLoop.h"

#include <signal.h>
#include <sys/socket.h>

using namespace kvstore;

static EpollLoop* g_loop = nullptr;

static void onSignal(int) {
    if (g_loop) {
        g_loop->quit();
    }
}

class EchoServer {
public:
    EchoServer(EpollLoop* loop, const InetAddress& listenAddr)
        : loop_(loop)
        , acceptor_(loop, listenAddr, false) {
        acceptor_.setNewConnectionCallback(
            std::bind(&EchoServer::onNewConnection, this, std::placeholders::_1, std::placeholders::_2));
    }

    void start() { acceptor_.listen(); }

private:
    void onNewConnection(socket_t sockfd, const InetAddress& peerAddr) {
        std::string connName = "Connection-" + std::to_string(nextConnId_);
        LOG_INFO << "EchoServer - new connection [" << connName << "] from " << peerAddr.toIpPort();

        InetAddress localAddr;
        sockaddr_in la{};
        socklen_t len = sizeof(la);
        if (::getsockname(sockfd, reinterpret_cast<sockaddr*>(&la), &len) == 0) {
            localAddr.setSockAddrInet(la);
        }

        auto conn = std::make_shared<Connection>(loop_, connName, sockfd, localAddr, peerAddr);

        connPool_.addConnection(conn);

        conn->setConnectionCallback(std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        conn->setMessageCallback(std::bind(&EchoServer::onMessage, this, std::placeholders::_1,
                                           std::placeholders::_2, std::placeholders::_3));
        conn->connectEstablished();

        nextConnId_++;
    }

    void onConnection(const std::shared_ptr<Connection>& conn) {
        LOG_INFO << "EchoServer - connection " << conn->name() << " is " << (conn->connected() ? "UP" : "DOWN");
        if (!conn->connected()) {
            connPool_.removeConnection(conn);
        }
    }

    void onMessage(const std::shared_ptr<Connection>& conn, Buffer* buf, Timestamp time) {
        std::string msg = buf->retrieveAllAsString();
        LOG_INFO << "EchoServer - received " << msg.size() << " bytes at " << time.toFormattedString(false);
        conn->send(msg);
    }

    EpollLoop* loop_;
    Acceptor acceptor_;
    ConnectionPool connPool_;
    int nextConnId_ = 1;
};

int main() {
    LoggerImpl::instance().setLogLevel(INFO);
    LOG_INFO << "=== Echo Server starting (Linux/epoll) ===";

    ::signal(SIGINT, onSignal);
    ::signal(SIGTERM, onSignal);
    ::signal(SIGPIPE, SIG_IGN);

    try {
        EpollLoop loop;
        g_loop = &loop;
        InetAddress listenAddr(6379);
        EchoServer server(&loop, listenAddr);
        server.start();

        LOG_INFO << "EchoServer listening on port " << listenAddr.port() << ", waiting for connections...";
        loop.loop();
    } catch (const std::exception& e) {
        LOG_ERROR << e.what();
        g_loop = nullptr;
        return 1;
    }

    g_loop = nullptr;
    LOG_INFO << "=== Echo Server stopped ===";
    return 0;
}

#else

#error "echo_server requires Windows or Linux"

#endif
