#ifndef KVSTORE_NETWORK_EPOLL_EPOLLLOOP_H
#define KVSTORE_NETWORK_EPOLL_EPOLLLOOP_H

#include "network/EventLoop.h"

#include <unordered_map>
#include <mutex>

struct epoll_event;

namespace kvstore {

class EpollLoop : public EventLoop {
public:
    EpollLoop();
    ~EpollLoop() override;

    void loop() override;
    void quit() override;

    void updateChannel(Channel* channel) override;
    void removeChannel(Channel* channel) override;
    bool hasChannel(Channel* channel) const override;

private:
    static constexpr int kIndexNew = -1;
    static constexpr int kIndexAdded = 1;
    static constexpr int kIndexDeleted = 2;

    void wakeup() override;
    void drainWakeupFd();
    int epollCtl(int op, Channel* channel, struct epoll_event* ev);

    int epollfd_;
    int wakeupFd_;
    static char wakeupEventTag_;
    std::unordered_map<int, Channel*> channels_;
    mutable std::mutex channelsMutex_;
};

} // namespace kvstore

#endif // KVSTORE_NETWORK_EPOLL_EPOLLLOOP_H
