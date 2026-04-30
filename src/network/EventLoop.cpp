#include "network/EventLoop.h"
#include "network/Channel.h"
#include "network/Timer.h"
#include "utils/Logging.h"

#include <sstream>
#include <cassert>

namespace kvstore {

thread_local EventLoop* t_loopInThisThread = nullptr;

namespace {

Timestamp makeExpiration(double delay) {
    return addTime(Timestamp::now(), delay);
}

} // namespace

EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , eventHandling_(false)
    , callingPendingFunctors_(false)
    , threadId_(std::this_thread::get_id())
    , currentActiveChannel_(nullptr)
    , nextTimerSequence_(1)
{
    LOG_DEBUG << "EventLoop created " << this << " in thread " << threadId_;
    if (t_loopInThisThread) {
        LOG_FATAL << "Another EventLoop " << t_loopInThisThread 
                  << " exists in this thread " << threadId_;
    } else {
        t_loopInThisThread = this;
    }
}

EventLoop::~EventLoop() {
    LOG_DEBUG << "EventLoop " << this << " of thread " << threadId_ 
              << " destructs in thread " << std::this_thread::get_id();
    t_loopInThisThread = nullptr;
}

void EventLoop::loop() {
    assert(!looping_);
    assertInLoopThread();
    looping_ = true;
    quit_ = false;

    LOG_INFO << "EventLoop " << this << " start looping";

    while (!quit_) {
        activeChannels_.clear();
        
        // 等待事件（由派生类实现）
        // poll(...)
        
        eventHandling_ = true;
        for (Channel* channel : activeChannels_) {
            currentActiveChannel_ = channel;
            channel->handleEvent(pollReturnTime_);
        }
        currentActiveChannel_ = nullptr;
        eventHandling_ = false;

        processTimers();
        
        doPendingFunctors();
    }

    LOG_INFO << "EventLoop " << this << " stop looping";
    looping_ = false;
}

void EventLoop::quit() {
    quit_ = true;
    if (!isInLoopThread()) {
        wakeup();
    }
}

void EventLoop::runInLoop(Functor cb) {
    if (isInLoopThread()) {
        cb();
    } else {
        queueInLoop(std::move(cb));
    }
}

void EventLoop::queueInLoop(Functor cb) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.push_back(std::move(cb));
    }

    if (!isInLoopThread() || callingPendingFunctors_) {
        wakeup();
    }
}

void EventLoop::doPendingFunctors() {
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    for (const Functor& functor : functors) {
        functor();
    }

    callingPendingFunctors_ = false;
}

void EventLoop::wakeup() {
}

void EventLoop::handleRead() {
}

TimerId EventLoop::addTimer(TimerCallback cb, Timestamp when, double interval) {
    std::lock_guard<std::mutex> lock(timerMutex_);
    const int64_t sequence = nextTimerSequence_++;
    timers_.push_back(TimerEntry{sequence, when, std::move(cb), interval, interval > 0.0});
    return TimerId(sequence, sequence);
}

void EventLoop::processTimers() {
    const Timestamp now = Timestamp::now();
    std::vector<TimerEntry> expired;

    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        auto it = timers_.begin();
        while (it != timers_.end()) {
            if (cancelledTimers_.count(it->sequence)) {
                it = timers_.erase(it);
                continue;
            }
            if (timeDifference(it->expiration, now) <= 0.0) {
                expired.push_back(*it);
                if (it->repeat) {
                    it->expiration = addTime(now, it->interval);
                    ++it;
                } else {
                    it = timers_.erase(it);
                }
            } else {
                ++it;
            }
        }
        cancelledTimers_.clear();
    }

    for (const auto& timer : expired) {
        timer.callback();
    }
}

bool EventLoop::isInLoopThread() const {
    return threadId_ == std::this_thread::get_id();
}

void EventLoop::assertInLoopThread() const {
    if (!isInLoopThread()) {
        LOG_FATAL << "EventLoop was created in thread " << threadId_ 
                  << ", but is running in thread " << std::this_thread::get_id();
    }
}

TimerId EventLoop::runAt(Timestamp time, TimerCallback cb) {
    return addTimer(std::move(cb), time, 0.0);
}

TimerId EventLoop::runAfter(double delay, TimerCallback cb) {
    return addTimer(std::move(cb), makeExpiration(delay), 0.0);
}

TimerId EventLoop::runEvery(double interval, TimerCallback cb) {
    return addTimer(std::move(cb), makeExpiration(interval), interval);
}

void EventLoop::cancel(TimerId timerId) {
    std::lock_guard<std::mutex> lock(timerMutex_);
    cancelledTimers_.insert(timerId.sequence_);
}

} // namespace kvstore
