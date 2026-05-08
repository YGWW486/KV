#include "network/Acceptor.h"
#include "utils/Logging.h"

#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#endif

namespace kvstore {

socket_t Acceptor::createNonblockingOrDie() {
#ifdef _WIN32
    socket_t sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    u_long mode = 1;
    ioctlsocket(sockfd, FIONBIO, &mode);
    return sockfd;
#else
    socket_t sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd < 0) {
        throw std::runtime_error(std::string("Acceptor::socket: ") + strerror(errno));
    }
    return sockfd;
#endif
}

Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport)
    : loop_(loop)
    , acceptSocket_(createNonblockingOrDie())
    , listenning_(false)
    , idleFd_(kInvalidSocket) {
    if (reuseport) {
        int on = 1;
#ifdef _WIN32
        setsockopt(acceptSocket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));
#else
        setsockopt(acceptSocket_, SOL_SOCKET, SO_REUSEADDR, &on, static_cast<socklen_t>(sizeof on));
#endif
    }

    if (::bind(acceptSocket_, listenAddr.getSockAddr(), sizeof(sockaddr_in)) != 0) {
#ifdef _WIN32
        throw std::runtime_error("Acceptor::bind failed: " + std::to_string(WSAGetLastError()));
#else
        throw std::runtime_error(std::string("Acceptor::bind failed: ") + strerror(errno));
#endif
    }
    acceptChannel_.reset(new Channel(loop, static_cast<int>(acceptSocket_)));
    acceptChannel_->setReadCallback(std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor() {
    acceptChannel_->disableAll();
    acceptChannel_->remove();
#ifdef _WIN32
    closesocket(acceptSocket_);
    if (idleFd_ != kInvalidSocket) {
        closesocket(idleFd_);
    }
#else
    ::close(acceptSocket_);
    if (idleFd_ != kInvalidSocket) {
        ::close(idleFd_);
    }
#endif
}

void Acceptor::listen() {
    loop_->assertInLoopThread();
    listenning_ = true;
    if (::listen(acceptSocket_, SOMAXCONN) != 0) {
#ifdef _WIN32
        throw std::runtime_error("Acceptor::listen failed: " + std::to_string(WSAGetLastError()));
#else
        throw std::runtime_error(std::string("Acceptor::listen failed: ") + strerror(errno));
#endif
    }
    acceptChannel_->enableReading();
}

void Acceptor::handleRead() {
    loop_->assertInLoopThread();
    InetAddress peerAddr;
    sockaddr_in addr{};

#ifdef _WIN32
    int addrlen = static_cast<int>(sizeof addr);
    socket_t connfd = ::accept(acceptSocket_, reinterpret_cast<sockaddr*>(&addr), &addrlen);
#else
    socklen_t addrlen = static_cast<socklen_t>(sizeof addr);
    socket_t connfd = ::accept(acceptSocket_, reinterpret_cast<sockaddr*>(&addr), &addrlen);
    if (connfd >= 0) {
        int flags = ::fcntl(connfd, F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(connfd, F_SETFL, flags | O_NONBLOCK);
        }
        int fdflags = ::fcntl(connfd, F_GETFD, 0);
        if (fdflags >= 0) {
            ::fcntl(connfd, F_SETFD, fdflags | FD_CLOEXEC);
        }
    }
#endif
    if (connfd != kInvalidSocket) {
        peerAddr.setSockAddrInet(addr);
        if (newConnectionCallback_) {
            newConnectionCallback_(connfd, peerAddr);
        } else {
#ifdef _WIN32
            closesocket(connfd);
#else
            ::close(connfd);
#endif
        }
    } else {
#ifdef _WIN32
        int err = WSAGetLastError();
        LOG_WARN << "Accept failed, err: " << err;
#else
        int err = errno;
        if (err != EAGAIN && err != EWOULDBLOCK) {
            LOG_WARN << "Accept failed: " << strerror(err);
        }
#endif
    }
}

} // namespace kvstore
