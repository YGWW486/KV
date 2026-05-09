#ifndef KVSTORE_NETWORK_CONNECTION_H
#define KVSTORE_NETWORK_CONNECTION_H

#include "network/EventLoop.h"
#include "network/Channel.h"
#include "network/InetAddress.h"
#include "network/Buffer.h"
#include "kvstore/Types.h"

#include <memory>
#include <functional>
#include <atomic>

namespace kvstore {

class Connection : public std::enable_shared_from_this<Connection> {
public:
    enum State { kConnecting, kConnected, kDisconnecting, kDisconnected };

    using ConnectionCallback = std::function<void(const std::shared_ptr<Connection>&)>;
    using MessageCallback = std::function<void(const std::shared_ptr<Connection>&, Buffer*, Timestamp)>;
    using CloseCallback = std::function<void(const std::shared_ptr<Connection>&)>;
    using WriteCompleteCallback = std::function<void(const std::shared_ptr<Connection>&)>;

    Connection(EventLoop* loop, const std::string& name, socket_t sockfd,
               const InetAddress& localAddr, const InetAddress& peerAddr);
    ~Connection();

    void setConnectionCallback(const ConnectionCallback& cb) { connectionCallback_ = cb; }
    void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb; }
    void setCloseCallback(const CloseCallback& cb) { closeCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback& cb) { writeCompleteCallback_ = cb; }

    const std::string& name() const { return name_; }
    EventLoop* getLoop() const { return loop_; }
    const InetAddress& localAddress() const { return localAddr_; }
    const InetAddress& peerAddress() const { return peerAddr_; }
    bool connected() const { return state_ == kConnected; }
    void setAuthenticated(bool auth) { authenticated_ = auth; }
    bool isAuthenticated() const { return authenticated_; }

    void connectEstablished();
    void connectDestroyed();
    void send(const void* message, size_t len);
    void send(const std::string& message);
    void shutdown();
    void forceClose();

private:
    void setState(State s) { state_ = s; }
    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();
    void sendInLoop(const std::string& message);
    void sendInLoop(const void* message, size_t len);
    void shutdownInLoop();
    void forceCloseInLoop();

    EventLoop* loop_;
    const std::string name_;
    State state_;
    socket_t sockfd_;
    std::unique_ptr<Channel> channel_;
    InetAddress localAddr_;
    InetAddress peerAddr_;
    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    CloseCallback closeCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    Buffer inputBuffer_;
    Buffer outputBuffer_;
    bool authenticated_ = false;
};

} // namespace kvstore

#endif // KVSTORE_NETWORK_CONNECTION_H
