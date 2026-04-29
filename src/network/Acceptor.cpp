#include "network/Acceptor.h"
#include "utils/Logging.h"

#include <winsock2.h>
#include <ws2tcpip.h>

namespace kvstore {

SOCKET Acceptor::createNonblockingOrDie() {
    SOCKET sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    u_long mode = 1;
    ioctlsocket(sockfd, FIONBIO, &mode);
    return sockfd;
}

Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport)
    : loop_(loop)
    , acceptSocket_(createNonblockingOrDie())
    , listenning_(false)
    , idleFd_(INVALID_SOCKET)
{
    if (reuseport) {
        int on = 1;
        setsockopt(acceptSocket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));
    }

    bind(acceptSocket_, listenAddr.getSockAddr(), sizeof(sockaddr_in));
    acceptChannel_.reset(new Channel(loop, (int)acceptSocket_));
    acceptChannel_->setReadCallback(std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor() {
    closesocket(acceptSocket_);
    if (idleFd_ != INVALID_SOCKET) {
        closesocket(idleFd_);
    }
}

void Acceptor::listen() {
    loop_->assertInLoopThread();
    listenning_ = true;
    ::listen(acceptSocket_, 128);
    acceptChannel_->enableReading();
}

void Acceptor::handleRead() {
    loop_->assertInLoopThread();
    InetAddress peerAddr;
    sockaddr_in addr;
    int addrlen = sizeof(addr);
    
    SOCKET connfd = ::accept(acceptSocket_, reinterpret_cast<sockaddr*>(&addr), &addrlen);
    if (connfd != INVALID_SOCKET) {
        peerAddr.setSockAddrInet(addr);
        if (newConnectionCallback_) {
            newConnectionCallback_(connfd, peerAddr);
        } else {
            closesocket(connfd);
        }
    } else {
        int err = WSAGetLastError();
        LOG_WARN << "Accept failed, err: " << err;
    }
}

} // namespace kvstore
