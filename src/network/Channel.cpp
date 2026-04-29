#include "network/Channel.h"
#include "network/EventLoop.h"
#include "utils/Logging.h"

#include <sstream>

namespace kvstore {

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop)
    , fd_(fd)
    , events_(0)
    , revents_(0)
    , index_(-1)
    , tied_(false)
    , eventHandling_(false)
    , addedToLoop_(false)
{
}

Channel::~Channel() {
    assert(!eventHandling_);
    assert(!addedToLoop_);
    if (loop_->isInLoopThread()) {
        assert(!loop_->hasChannel(this));
    }
}

void Channel::tie(const std::shared_ptr<void>& obj) {
    tie_ = obj;
    tied_ = true;
}

void Channel::update() {
    addedToLoop_ = true;
    loop_->updateChannel(this);
}

void Channel::remove() {
    assert(isNoneEvent());
    addedToLoop_ = false;
    loop_->removeChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime) {
    std::shared_ptr<void> guard;
    if (tied_) {
        guard = tie_.lock();
        if (guard) {
            handleEventWithGuard(receiveTime);
        }
    } else {
        handleEventWithGuard(receiveTime);
    }
}

void Channel::handleEventWithGuard(Timestamp receiveTime) {
    eventHandling_ = true;
    LOG_TRACE << "Channel::handleEvent revents: " << revents_;

    if ((revents_ & kCloseEvent) && !(revents_ & kReadEvent)) {
        LOG_WARN << "fd = " << fd_ << " Channel::handleEvent() kCloseEvent";
        if (closeCallback_) closeCallback_();
    }

    if (revents_ & kErrorEvent) {
        LOG_WARN << "fd = " << fd_ << " Channel::handleEvent() kErrorEvent";
        if (errorCallback_) errorCallback_();
    }

    if (revents_ & (kReadEvent | kPriEvent)) {
        LOG_TRACE << "fd = " << fd_ << " Channel::handleEvent() read";
        if (readCallback_) readCallback_(receiveTime);
    }

    if (revents_ & kWriteEvent) {
        LOG_TRACE << "fd = " << fd_ << " Channel::handleEvent() write";
        if (writeCallback_) writeCallback_();
    }

    eventHandling_ = false;
}

} // namespace kvstore
