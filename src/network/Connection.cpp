#include "network/Connection.h"
#include "utils/Logging.h"
#include <winsock2.h>
#include <ws2tcpip.h>

namespace kvstore {

Connection::Connection(EventLoop* loop, const std::string& name, SOCKET sockfd,
                       const InetAddress& localAddr, const InetAddress& peerAddr)
    : loop_(loop), name_(name), state_(kConnecting), sockfd_(sockfd),
      localAddr_(localAddr), peerAddr_(peerAddr) {
    channel_.reset(new Channel(loop_, (int)sockfd_));
    channel_->setReadCallback(std::bind(&Connection::handleRead, this, std::placeholders::_1));
    channel_->setWriteCallback(std::bind(&Connection::handleWrite, this));
    channel_->setCloseCallback(std::bind(&Connection::handleClose, this));
    channel_->setErrorCallback(std::bind(&Connection::handleError, this));
    u_long mode = 1;
    ioctlsocket(sockfd_, FIONBIO, &mode);
}

Connection::~Connection() {
    closesocket(sockfd_);
}

void Connection::connectEstablished() {
    loop_->assertInLoopThread();
    setState(kConnected);
    channel_->tie(shared_from_this());
    channel_->enableReading();

    if (connectionCallback_) {
        connectionCallback_(shared_from_this());
    }
}

void Connection::connectDestroyed() {
    loop_->assertInLoopThread();
    setState(kDisconnected);
    channel_->disableAll();
    if (connectionCallback_) {
        connectionCallback_(shared_from_this());
    }

    channel_->remove();
}

void Connection::send(const std::string& message) {
    if (state_ == kConnected) {
        if (loop_->isInLoopThread()) {
            sendInLoop(message);
        } else {
            std::weak_ptr<Connection> weakConn = shared_from_this();
            loop_->runInLoop([weakConn, message]() {
                if (auto conn = weakConn.lock()) {
                    conn->sendInLoop(message);
                }
            });
        }
    }
}

void Connection::send(const void* message, size_t len) {
    if (state_ == kConnected) {
        if (loop_->isInLoopThread()) {
            sendInLoop(message, len);
        } else {
            std::string data(reinterpret_cast<const char*>(message), len);
            std::weak_ptr<Connection> weakConn = shared_from_this();
            loop_->runInLoop([weakConn, data = std::move(data)]() {
                if (auto conn = weakConn.lock()) {
                    conn->sendInLoop(data);
                }
            });
        }
    }
}

void Connection::sendInLoop(const std::string& message) {
    sendInLoop(message.data(), message.size());
}

void Connection::sendInLoop(const void* message, size_t len) {
    loop_->assertInLoopThread();
    if (state_ == kDisconnected) {
        return;
    }

    size_t remaining = len;
    const char* data = static_cast<const char*>(message);

    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
        int n = ::send(sockfd_, data, (int)remaining, 0);
        if (n >= 0) {
            remaining -= n;
            data += n;
            if (remaining == 0 && writeCompleteCallback_) {
                loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
            }
        }
    }

    if (remaining > 0) {
        outputBuffer_.append(data, remaining);
        if (!channel_->isWriting()) {
            channel_->enableWriting();
        }
    }
}

void Connection::shutdown() {
    if (state_ == kConnected) {
        setState(kDisconnecting);
        std::weak_ptr<Connection> weakConn = shared_from_this();
        loop_->runInLoop([weakConn]() {
            if (auto conn = weakConn.lock()) {
                conn->shutdownInLoop();
            }
        });
    }
}

void Connection::shutdownInLoop() {
    loop_->assertInLoopThread();
    if (!channel_->isWriting()) {
        ::shutdown(sockfd_, SD_SEND);
    }
}

void Connection::handleRead(Timestamp receiveTime) {
    loop_->assertInLoopThread();
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(sockfd_, &savedErrno);
    if (n > 0) {
        if (messageCallback_) {
            messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
        }
    } else if (n == 0) {
        handleClose();
    } else {
        LOG_ERROR << "Read error";
        handleError();
    }
}

void Connection::handleWrite() {
    loop_->assertInLoopThread();
    if (channel_->isWriting()) {
        int n = ::send(sockfd_, outputBuffer_.peek(), (int)outputBuffer_.readableBytes(), 0);
        if (n > 0) {
            outputBuffer_.retrieve(n);
            if (outputBuffer_.readableBytes() == 0) {
                channel_->disableWriting();
                if (writeCompleteCallback_) {
                    loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
                }
                if (state_ == kDisconnecting) {
                    shutdownInLoop();
                }
            }
        }
    }
}

void Connection::handleClose() {
    loop_->assertInLoopThread();
    setState(kDisconnected);
    channel_->disableAll();
    if (closeCallback_) {
        closeCallback_(shared_from_this());
    }
}

void Connection::handleError() {
    int err = WSAGetLastError();
    LOG_ERROR << "Connection::handleError [" << name_ << "] - SO_ERROR = " << err;
}

} // namespace kvstore
