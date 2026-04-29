#ifndef KVSTORE_NETWORK_TIMERQUEUE_H
#define KVSTORE_NETWORK_TIMERQUEUE_H

#include "network/Timer.h"
#include "utils/Timestamp.h"
#include <set>
#include <vector>
#include <mutex>
#include <memory>

namespace kvstore {

class EventLoop;
class Channel;

class TimerQueue {
public:
    using TimerCallback = std::function<void()>;

    TimerQueue(EventLoop* loop);
    ~TimerQueue();

    TimerId addTimer(TimerCallback cb, Timestamp when, double interval);
    void cancel(TimerId timerId);

private:
    using Entry = std::pair<Timestamp, Timer*>;
    using TimerList = std::set<Entry>;
    using ActiveTimer = std::pair<Timer*, int64_t>;
    using ActiveTimerSet = std::set<ActiveTimer>;

    void addTimerInLoop(Timer* timer);
    void cancelInLoop(TimerId timerId);
    void handleRead();
    std::vector<Entry> getExpired(Timestamp now);
    void reset(const std::vector<Entry>& expired, Timestamp now);

    bool insert(Timer* timer);

    EventLoop* loop_;
    const int timerfd_;
    std::unique_ptr<Channel> timerfdChannel_;
    TimerList timers_;
    ActiveTimerSet activeTimers_;
    bool callingExpiredTimers_;
    ActiveTimerSet cancelingTimers_;
};

} // namespace kvstore

#endif // KVSTORE_NETWORK_TIMERQUEUE_H
