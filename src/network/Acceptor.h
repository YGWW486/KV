#ifndef KVSTORE_NETWORK_ACCEPTOR_H
#define KVSTORE_NETWORK_ACCEPTOR_H

#include "network/EventLoop.h"
#include "network/Channel.h"
#include "network/InetAddress.h"
#include "kvstore/Types.h"
#include <functional>

namespace kvstore {

class Acceptor {
public:
    using NewConnectionCallback = std::function<void(socket_t sockfd, const InetAddress&)>;

    Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport);
    ~Acceptor();

    void setNewConnectionCallback(NewConnectionCallback cb) { newConnectionCallback_ = std::move(cb); }

    bool listenning() const { return listenning_; }
    void listen();

private:
    void handleRead();
    static socket_t createNonblockingOrDie();

    EventLoop* loop_;
    socket_t acceptSocket_;
    std::unique_ptr<Channel> acceptChannel_;
    NewConnectionCallback newConnectionCallback_;
    bool listenning_;
    socket_t idleFd_;
};

} // namespace kvstore

#endif // KVSTORE_NETWORK_ACCEPTOR_H
