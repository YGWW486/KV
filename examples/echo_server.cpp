#include "network/iocp/IOCPLoop.h"
#include "network/Acceptor.h"
#include "network/Connection.h"
#include "network/ConnectionPool.h"
#include "utils/Logging.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <memory>

using namespace kvstore;

class EchoServer {
public:
    EchoServer(IOCPLoop* loop, const InetAddress& listenAddr)
        : loop_(loop), acceptor_(loop, listenAddr, false) {
        acceptor_.setNewConnectionCallback(std::bind(&EchoServer::onNewConnection, this, std::placeholders::_1, std::placeholders::_2));
    }

    void start() { acceptor_.listen(); }

private:
    void onNewConnection(SOCKET sockfd, const InetAddress& peerAddr) {
        std::string connName = "Connection-" + std::to_string(nextConnId_);
        LOG_INFO << "EchoServer - new connection [" << connName << "] from " << peerAddr.toIpPort();
        
        InetAddress localAddr;
        auto conn = std::make_shared<Connection>(loop_, connName, sockfd, localAddr, peerAddr);
        
        // 使用ConnectionPool管理连接
        connPool_.addConnection(conn);
        
        conn->setConnectionCallback(std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        conn->setMessageCallback(std::bind(&EchoServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        conn->connectEstablished();
        
        nextConnId_++;
    }

    void onConnection(const std::shared_ptr<Connection>& conn) {
        LOG_INFO << "EchoServer - connection " << conn->name() << " is " << (conn->connected() ? "UP" : "DOWN");
        if (!conn->connected()) {
            // 使用ConnectionPool移除连接
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
    ConnectionPool connPool_; // 使用ConnectionPool管理连接
    int nextConnId_ = 1;
};

int main() {
    LoggerImpl::instance().setLogLevel(INFO);
    LOG_INFO << "=== Echo Server starting ===";

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    IOCPLoop loop;
    InetAddress listenAddr(6379);
    EchoServer server(&loop, listenAddr);
    server.start();

    LOG_INFO << "EchoServer listening on port " << listenAddr.port() << ", waiting for connections...";
    loop.loop();

    WSACleanup();
    LOG_INFO << "=== Echo Server stopped ===";
    return 0;
}
