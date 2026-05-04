#ifndef KVSTORE_NETWORK_EVENTLOOP_H
#define KVSTORE_NETWORK_EVENTLOOP_H

#include "kvstore/Types.h"
#include "utils/Timestamp.h"
#include "network/Timer.h"
#include <functional>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <unordered_set>

namespace kvstore {

class Channel;
class EventLoop {
public:
    using Functor = std::function<void()>;
    using TimerCallback = std::function<void()>;

    EventLoop();
    virtual ~EventLoop();

    // 禁止拷贝
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    virtual void loop();
    virtual void quit();

    virtual void updateChannel(Channel* channel) = 0;
    virtual void removeChannel(Channel* channel) = 0;
    virtual bool hasChannel(Channel* channel) const { return false; }

    void assertInLoopThread() const;
    bool isInLoopThread() const;

    void runInLoop(Functor cb);
    void queueInLoop(Functor cb);

    TimerId runAt(Timestamp time, TimerCallback cb);
    TimerId runAfter(double delay, TimerCallback cb);
    TimerId runEvery(double interval, TimerCallback cb);
    void cancel(TimerId timerId);

protected:
    virtual void wakeup();
    void handleRead();
    void doPendingFunctors();

    TimerId addTimer(TimerCallback cb, Timestamp when, double interval);
    void processTimers();

    std::atomic<bool> looping_;
    std::atomic<bool> quit_;
    bool eventHandling_;
    bool callingPendingFunctors_;

    const std::thread::id threadId_;
    Timestamp pollReturnTime_;

    std::vector<Channel*> activeChannels_;
    Channel* currentActiveChannel_;

    std::mutex mutex_;
    std::vector<Functor> pendingFunctors_;

    struct TimerEntry {
        int64_t sequence;
        Timestamp expiration;
        TimerCallback callback;
        double interval;
        bool repeat;
    };

    std::mutex timerMutex_;
    std::vector<TimerEntry> timers_;
    std::unordered_set<int64_t> cancelledTimers_;
    int64_t nextTimerSequence_;
};

} // namespace kvstore

#endif // KVSTORE_NETWORK_EVENTLOOP_H
