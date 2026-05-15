#include "network/Connection.h"
#include "utils/Logging.h"

#ifdef _WIN32
#include "network/iocp/IOCPLoop.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#endif

namespace kvstore {

Connection::Connection(EventLoop* loop, const std::string& name, socket_t sockfd,
                       const InetAddress& localAddr, const InetAddress& peerAddr)
    : loop_(loop)
    , name_(name)
    , state_(kConnecting)
    , sockfd_(sockfd)
    , localAddr_(localAddr)
    , peerAddr_(peerAddr) {
    channel_.reset(new Channel(loop_, static_cast<int>(sockfd_)));
    channel_->setReadCallback(std::bind(&Connection::handleRead, this, std::placeholders::_1));
    channel_->setWriteCallback(std::bind(&Connection::handleWrite, this));
    channel_->setCloseCallback(std::bind(&Connection::handleClose, this));
    channel_->setErrorCallback(std::bind(&Connection::handleError, this));
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sockfd_, FIONBIO, &mode);
#else
    int flags = ::fcntl(sockfd_, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK);
    }
    int nodelay = 1;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
#endif
}

Connection::~Connection() {
#ifdef _WIN32
    closesocket(sockfd_);
#else
    ::close(sockfd_);
#endif
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

#ifdef _WIN32
    // ── Windows async write path ─────────────────────────────────
    bool startWrite = (!channel_->isWriting() && outputBuffer_.readableBytes() == 0);
    outputBuffer_.append(static_cast<const char*>(message), len);
    if (startWrite) {
        channel_->enableWriting();
        auto* iocpLoop = static_cast<IOCPLoop*>(loop_);
        iocpLoop->postWriteData(sockfd_, outputBuffer_.peek(),
                                 outputBuffer_.readableBytes());
    }
#else
    // ── Linux synchronous write path ─────────────────────────────
    size_t remaining = len;
    const char* data = static_cast<const char*>(message);

    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
        ssize_t n = ::send(sockfd_, data, remaining, MSG_NOSIGNAL);
        if (n >= 0) {
            remaining -= static_cast<size_t>(n);
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
#endif
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
#ifdef _WIN32
        ::shutdown(sockfd_, SD_SEND);
#else
        ::shutdown(sockfd_, SHUT_WR);
#endif
    }
}

void Connection::handleRead(Timestamp receiveTime) {
    loop_->assertInLoopThread();

#ifdef _WIN32
    // ── Windows async read path ──────────────────────────────────
    auto* iocpLoop = static_cast<IOCPLoop*>(loop_);
    std::vector<char> data = iocpLoop->takeReadData(sockfd_);
    if (!data.empty()) {
        inputBuffer_.append(data.data(), data.size());
        if (messageCallback_) {
            messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
        }
    }
#else
    // ── Linux synchronous read path ──────────────────────────────
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(sockfd_, &savedErrno);
    if (n > 0) {
        if (messageCallback_) {
            messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
        }
    } else if (n == 0) {
        handleClose();
    } else {
        if (savedErrno == EAGAIN || savedErrno == EWOULDBLOCK) {
            return;
        }
        LOG_ERROR << "Read error: " << savedErrno;
        handleError();
    }
#endif
}

void Connection::handleWrite() {
    loop_->assertInLoopThread();
    if (!channel_->isWriting()) return;

#ifdef _WIN32
    // ── Windows async write completion path ──────────────────────
    auto* iocpLoop = static_cast<IOCPLoop*>(loop_);
    DWORD bytesWritten = iocpLoop->takeWriteResult(sockfd_);
    if (bytesWritten > 0) {
        outputBuffer_.retrieve(bytesWritten);
    }
    if (outputBuffer_.readableBytes() > 0) {
        // More data queued — post next async write
        iocpLoop->postWriteData(sockfd_, outputBuffer_.peek(),
                                 outputBuffer_.readableBytes());
    } else {
        // All data sent
        channel_->disableWriting();
        if (writeCompleteCallback_) {
            loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
        }
        if (state_ == kDisconnecting) {
            shutdownInLoop();
        }
    }
#else
    // ── Linux synchronous write path ─────────────────────────────
    ssize_t n = ::send(sockfd_, outputBuffer_.peek(), outputBuffer_.readableBytes(), MSG_NOSIGNAL);
    if (n > 0) {
        outputBuffer_.retrieve(static_cast<size_t>(n));
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
#endif
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
#ifdef _WIN32
    int err = WSAGetLastError();
#else
    int soerr = 0;
    socklen_t optlen = sizeof(soerr);
    if (::getsockopt(sockfd_, SOL_SOCKET, SO_ERROR, &soerr, &optlen) < 0) {
        soerr = errno;
    }
    int err = soerr;
#endif
    LOG_ERROR << "Connection::handleError [" << name_ << "] - SO_ERROR = " << err;
}

} // namespace kvstore
