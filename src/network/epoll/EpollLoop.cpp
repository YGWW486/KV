#include "network/epoll/EpollLoop.h"
#include "network/Channel.h"
#include "utils/Logging.h"

#include <cerrno>
#include <cstring>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cassert>
#include <stdexcept>

namespace kvstore {

char EpollLoop::wakeupEventTag_ = 0;

namespace {

int toEpollEvents(Channel* channel) {
    int ev = 0;
    if (channel->isReading()) {
        ev |= EPOLLIN | EPOLLPRI;
    }
    if (channel->isWriting()) {
        ev |= EPOLLOUT;
    }
    return ev | EPOLLERR | EPOLLHUP;
}

int toChannelRevents(uint32_t epollEvents) {
    int rev = 0;
    if (epollEvents & EPOLLIN) {
        rev |= Channel::kReadEvent;
    }
    if (epollEvents & EPOLLPRI) {
        rev |= Channel::kPriEvent;
    }
    if (epollEvents & EPOLLOUT) {
        rev |= Channel::kWriteEvent;
    }
    if (epollEvents & EPOLLERR) {
        rev |= Channel::kErrorEvent;
    }
    if (epollEvents & EPOLLHUP) {
        rev |= Channel::kCloseEvent;
    }
    return rev;
}

} // namespace

EpollLoop::EpollLoop()
    : epollfd_(::epoll_create1(EPOLL_CLOEXEC))
    , wakeupFd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) {
    if (epollfd_ < 0) {
        throw std::runtime_error("EpollLoop: epoll_create1 failed: " + std::string(strerror(errno)));
    }
    if (wakeupFd_ < 0) {
        ::close(epollfd_);
        epollfd_ = -1;
        throw std::runtime_error("EpollLoop: eventfd failed: " + std::string(strerror(errno)));
    }

    struct epoll_event ev {};
    ev.events = EPOLLIN;
    ev.data.ptr = &wakeupEventTag_;
    if (::epoll_ctl(epollfd_, EPOLL_CTL_ADD, wakeupFd_, &ev) != 0) {
        int e = errno;
        ::close(wakeupFd_);
        wakeupFd_ = -1;
        ::close(epollfd_);
        epollfd_ = -1;
        throw std::runtime_error("EpollLoop: epoll_ctl (wakeup) failed: " + std::string(strerror(e)));
    }

    LOG_INFO << "EpollLoop created " << this;
}

EpollLoop::~EpollLoop() {
    if (wakeupFd_ >= 0) {
        ::epoll_ctl(epollfd_, EPOLL_CTL_DEL, wakeupFd_, nullptr);
        ::close(wakeupFd_);
        wakeupFd_ = -1;
    }
    if (epollfd_ >= 0) {
        ::close(epollfd_);
        epollfd_ = -1;
    }
    LOG_INFO << "EpollLoop " << this << " destructed";
}

void EpollLoop::loop() {
    assert(!looping_);
    assertInLoopThread();
    looping_ = true;
    quit_ = false;

    LOG_INFO << "EpollLoop " << this << " start looping";

    const int kMaxEvents = 256;
    struct epoll_event events[kMaxEvents];

    while (!quit_) {
        activeChannels_.clear();

        Timestamp nextExp = nextExpiration();
        int timeoutMs = 1000;
        if (nextExp.valid()) {
            double diff = timeDifference(nextExp, Timestamp::now());
            if (diff <= 0.0) timeoutMs = 0;
            else if (diff < 1.0) timeoutMs = static_cast<int>(diff * 1000.0);
        }

        int numEvents = ::epoll_wait(epollfd_, events, kMaxEvents, timeoutMs);
        pollReturnTime_ = Timestamp::now();

        if (numEvents < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG_ERROR << "EpollLoop::epoll_wait: " << strerror(errno);
            break;
        }

        for (int i = 0; i < numEvents; ++i) {
            void* ptr = events[i].data.ptr;
            if (ptr == &wakeupEventTag_) {
                drainWakeupFd();
                continue;
            }
            auto* channel = static_cast<Channel*>(ptr);
            channel->set_revents(toChannelRevents(events[i].events));
            activeChannels_.push_back(channel);
        }

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

    LOG_INFO << "EpollLoop " << this << " stop looping";
    looping_ = false;
}

void EpollLoop::quit() {
    quit_ = true;
    if (!isInLoopThread()) {
        wakeup();
    }
}

void EpollLoop::wakeup() {
    if (wakeupFd_ < 0) {
        return;
    }
    uint64_t one = 1;
    ssize_t n = ::write(wakeupFd_, &one, sizeof one);
    if (n != static_cast<ssize_t>(sizeof one)) {
        if (errno != EAGAIN) {
            LOG_WARN << "EpollLoop::wakeup write: " << strerror(errno);
        }
    }
}

void EpollLoop::drainWakeupFd() {
    uint64_t dummy = 0;
    ssize_t n = 0;
    while ((n = ::read(wakeupFd_, &dummy, sizeof dummy)) > 0) {
    }
    if (n < 0 && errno != EAGAIN) {
        LOG_WARN << "EpollLoop::drainWakeupFd: " << strerror(errno);
    }
}

int EpollLoop::epollCtl(int op, Channel* channel, struct epoll_event* ev) {
    const int fd = channel->fd();
    int r = ::epoll_ctl(epollfd_, op, fd, ev);
    if (r != 0) {
        LOG_ERROR << "epoll_ctl op=" << op << " fd=" << fd << " : " << strerror(errno);
    }
    return r;
}

void EpollLoop::updateChannel(Channel* channel) {
    assertInLoopThread();
    const int fd = channel->fd();
    const int index = channel->index();
    LOG_TRACE << "fd = " << fd << " events = " << channel->events() << " index = " << index;

    std::lock_guard<std::mutex> lock(channelsMutex_);

    if (channel->isNoneEvent()) {
        if (index == kIndexAdded) {
            struct epoll_event ev {};
            if (epollCtl(EPOLL_CTL_DEL, channel, &ev) == 0) {
                channel->set_index(kIndexDeleted);
            }
        } else if (index == kIndexNew) {
            channel->set_index(kIndexDeleted);
        }
        return;
    }

    struct epoll_event ev {};
    ev.events = static_cast<uint32_t>(toEpollEvents(channel));
    ev.data.ptr = channel;

    if (index == kIndexNew || index == kIndexDeleted) {
        if (index == kIndexNew) {
            if (channels_.find(fd) != channels_.end()) {
                LOG_FATAL << "EpollLoop::updateChannel fd already in map: " << fd;
            }
            channels_[fd] = channel;
        } else {
            if (channels_.find(fd) == channels_.end() || channels_[fd] != channel) {
                LOG_FATAL << "EpollLoop::updateChannel kDeleted state inconsistent for fd " << fd;
            }
        }
        if (epollCtl(EPOLL_CTL_ADD, channel, &ev) == 0) {
            channel->set_index(kIndexAdded);
        }
    } else if (index == kIndexAdded) {
        epollCtl(EPOLL_CTL_MOD, channel, &ev);
    }
}

void EpollLoop::removeChannel(Channel* channel) {
    assertInLoopThread();
    const int fd = channel->fd();
    LOG_TRACE << "removeChannel fd = " << fd;

    std::lock_guard<std::mutex> lock(channelsMutex_);

    auto it = channels_.find(fd);
    if (it == channels_.end() || it->second != channel) {
        LOG_FATAL << "EpollLoop::removeChannel: unknown fd " << fd;
    }
    assert(channel->isNoneEvent());
    assert(channel->index() == kIndexDeleted);

    channels_.erase(it);
    channel->set_index(kIndexNew);
}

bool EpollLoop::hasChannel(Channel* channel) const {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    const int fd = channel->fd();
    auto it = channels_.find(fd);
    return it != channels_.end() && it->second == channel;
}

} // namespace kvstore
