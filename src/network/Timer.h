#ifndef KVSTORE_NETWORK_TIMER_H
#define KVSTORE_NETWORK_TIMER_H

#include "kvstore/Types.h"
#include "utils/Timestamp.h"
#include <functional>
#include <atomic>

namespace kvstore {

class TimerId {
public:
    TimerId() : id_(0), sequence_(0) {}
    TimerId(int64_t id, int64_t seq) : id_(id), sequence_(seq) {}
    
    friend class TimerQueue;
    
private:
    int64_t id_;
    int64_t sequence_;
};

class Timer {
public:
    using TimerCallback = std::function<void()>;

    Timer(TimerCallback cb, Timestamp when, double interval)
        : callback_(std::move(cb))
        , expiration_(when)
        , interval_(interval)
        , repeat_(interval > 0.0)
        , sequence_(s_numCreated_.fetch_add(1))
    {
    }

    void run() const { callback_(); }

    Timestamp expiration() const { return expiration_; }
    bool repeat() const { return repeat_; }
    int64_t sequence() const { return sequence_; }

    void restart(Timestamp now);

    static int64_t numCreated() { return s_numCreated_.load(); }

private:
    TimerCallback callback_;
    Timestamp expiration_;
    const double interval_;
    const bool repeat_;
    const int64_t sequence_;

    static std::atomic<int64_t> s_numCreated_;
};

} // namespace kvstore

#endif // KVSTORE_NETWORK_TIMER_H
