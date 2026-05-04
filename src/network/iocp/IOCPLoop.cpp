#include "network/iocp/IOCPLoop.h"
#include "network/Channel.h"
#include "utils/Logging.h"

#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <iostream>
#include <cassert>
#include <stdexcept>
#include <string>

namespace kvstore {

IOCPLoop::OverlappedContext::OverlappedContext(Channel* ch, int type)
    : channel(ch), eventType(type)
{
    reset();
}

void IOCPLoop::OverlappedContext::reset() {
    ZeroMemory(&overlapped, sizeof(overlapped));
    wsaBuf.buf = buffer.beginWrite();
    // 统一使用0字节I/O作为事件通知，实际读写由Connection非阻塞路径处理。
    wsaBuf.len = 0;
}

IOCPLoop::IOCPLoop()
    : iocpHandle_(nullptr)
    , wakeupSocket_{INVALID_SOCKET, INVALID_SOCKET}
    , quitPending_(false)
{
    WSADATA wsaData;
    int rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (rc != 0) {
        throw std::runtime_error("IOCPLoop: WSAStartup failed: " + std::to_string(rc));
    }
    wsaStartedByIocpLoop_ = true;

    iocpHandle_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (iocpHandle_ == nullptr) {
        WSACleanup();
        wsaStartedByIocpLoop_ = false;
        throw std::runtime_error("IOCPLoop: CreateIoCompletionPort failed: " +
                                 std::to_string(static_cast<int>(GetLastError())));
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        CloseHandle(iocpHandle_);
        iocpHandle_ = nullptr;
        WSACleanup();
        wsaStartedByIocpLoop_ = false;
        throw std::runtime_error("IOCPLoop: listener socket failed: " +
                                 std::to_string(WSAGetLastError()));
    }

    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        closesocket(s);
        CloseHandle(iocpHandle_);
        iocpHandle_ = nullptr;
        WSACleanup();
        wsaStartedByIocpLoop_ = false;
        throw std::runtime_error("IOCPLoop: bind listener failed: " + std::to_string(err));
    }

    if (listen(s, 1) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        closesocket(s);
        CloseHandle(iocpHandle_);
        iocpHandle_ = nullptr;
        WSACleanup();
        wsaStartedByIocpLoop_ = false;
        throw std::runtime_error("IOCPLoop: listen failed: " + std::to_string(err));
    }

    int len = sizeof(addr);
    if (getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        closesocket(s);
        CloseHandle(iocpHandle_);
        iocpHandle_ = nullptr;
        WSACleanup();
        wsaStartedByIocpLoop_ = false;
        throw std::runtime_error("IOCPLoop: getsockname failed: " + std::to_string(err));
    }

    wakeupSocket_[0] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (wakeupSocket_[0] == INVALID_SOCKET) {
        int err = WSAGetLastError();
        closesocket(s);
        CloseHandle(iocpHandle_);
        iocpHandle_ = nullptr;
        WSACleanup();
        wsaStartedByIocpLoop_ = false;
        throw std::runtime_error("IOCPLoop: wakeup client socket failed: " + std::to_string(err));
    }

    if (connect(wakeupSocket_[0], reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ==
        SOCKET_ERROR) {
        int err = WSAGetLastError();
        closesocket(wakeupSocket_[0]);
        wakeupSocket_[0] = INVALID_SOCKET;
        closesocket(s);
        CloseHandle(iocpHandle_);
        iocpHandle_ = nullptr;
        WSACleanup();
        wsaStartedByIocpLoop_ = false;
        throw std::runtime_error("IOCPLoop: wakeup connect failed: " + std::to_string(err));
    }

    wakeupSocket_[1] = accept(s, nullptr, nullptr);
    closesocket(s);
    s = INVALID_SOCKET;

    if (wakeupSocket_[1] == INVALID_SOCKET) {
        int err = WSAGetLastError();
        closesocket(wakeupSocket_[0]);
        wakeupSocket_[0] = INVALID_SOCKET;
        CloseHandle(iocpHandle_);
        iocpHandle_ = nullptr;
        WSACleanup();
        wsaStartedByIocpLoop_ = false;
        throw std::runtime_error("IOCPLoop: wakeup accept failed: " + std::to_string(err));
    }

    HANDLE assoc = CreateIoCompletionPort(reinterpret_cast<HANDLE>(wakeupSocket_[0]), iocpHandle_,
                                          reinterpret_cast<ULONG_PTR>(this), 0);
    if (assoc == nullptr) {
        int err = GetLastError();
        closesocket(wakeupSocket_[0]);
        wakeupSocket_[0] = INVALID_SOCKET;
        closesocket(wakeupSocket_[1]);
        wakeupSocket_[1] = INVALID_SOCKET;
        CloseHandle(iocpHandle_);
        iocpHandle_ = nullptr;
        WSACleanup();
        wsaStartedByIocpLoop_ = false;
        throw std::runtime_error("IOCPLoop: CreateIoCompletionPort (wakeup) failed: " +
                                 std::to_string(err));
    }

    LOG_INFO << "IOCPLoop created " << this;
}

IOCPLoop::~IOCPLoop() {
    if (iocpHandle_) {
        CloseHandle(iocpHandle_);
        iocpHandle_ = nullptr;
    }
    if (wakeupSocket_[0] != INVALID_SOCKET) {
        closesocket(wakeupSocket_[0]);
        wakeupSocket_[0] = INVALID_SOCKET;
    }
    if (wakeupSocket_[1] != INVALID_SOCKET) {
        closesocket(wakeupSocket_[1]);
        wakeupSocket_[1] = INVALID_SOCKET;
    }
    if (wsaStartedByIocpLoop_) {
        WSACleanup();
        wsaStartedByIocpLoop_ = false;
    }
    LOG_INFO << "IOCPLoop " << this << " destructed";
}

void IOCPLoop::loop() {
    assert(!looping_);
    assertInLoopThread();
    looping_ = true;
    quit_ = false;

    LOG_INFO << "IOCPLoop " << this << " start looping";

    while (!quit_) {
        activeChannels_.clear();
        handleCompletions();

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

    LOG_INFO << "IOCPLoop " << this << " stop looping";
    looping_ = false;
}

void IOCPLoop::quit() {
    quit_ = true;
    if (!isInLoopThread()) {
        wakeup();
    }
}

void IOCPLoop::wakeup() {
    if (!iocpHandle_) {
        return;
    }
    if (!PostQueuedCompletionStatus(iocpHandle_, 0, reinterpret_cast<ULONG_PTR>(this), nullptr)) {
        LOG_WARN << "PostQueuedCompletionStatus (wakeup) failed: " << GetLastError();
    }
}

void IOCPLoop::handleWakeup() {
    // 这里是简化版，实际可以用 socketpair 发送消息
}

void IOCPLoop::handleCompletions() {
    DWORD bytesTransferred = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED pOverlapped = nullptr;

    DWORD timeoutMs = 1000; // 1秒超时
    pollReturnTime_ = Timestamp::now();

    BOOL success = GetQueuedCompletionStatus(
        iocpHandle_,
        &bytesTransferred,
        &completionKey,
        &pOverlapped,
        timeoutMs
    );

    const DWORD gqcsErr = success ? ERROR_SUCCESS : GetLastError();

    if (success == FALSE && pOverlapped == nullptr && gqcsErr == WAIT_TIMEOUT) {
        return;
    }

    if (pOverlapped == nullptr &&
        completionKey == reinterpret_cast<ULONG_PTR>(this)) {
        handleWakeup();
        return;
    }

    if (success == FALSE) {
        if (pOverlapped == nullptr) {
            LOG_WARN << "GetQueuedCompletionStatus (no packet): " << gqcsErr;
            return;
        }
        LOG_WARN << "GetQueuedCompletionStatus failed: " << gqcsErr;
        return;
    }

    OverlappedContext* ctx = reinterpret_cast<OverlappedContext*>(pOverlapped);
    if (ctx == nullptr) {
        return;
    }

    Channel* channel = ctx->channel;
    if (channel == nullptr) {
        return;
    }

    if (ctx->eventType == 0) { // read
        // 重新投递0字节读通知，保持可读事件持续生效。
        SOCKET fd = static_cast<SOCKET>(channel->fd());
        ctx->reset();
        DWORD flags = 0;
        DWORD bytesReceived = 0;
        int rc = WSARecv(fd, &ctx->wsaBuf, 1, &bytesReceived, &flags,
                         &ctx->overlapped, nullptr);
        if (rc == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != ERROR_IO_PENDING) {
                LOG_WARN << "WSARecv re-arm failed: " << err;
            }
        }
        channel->set_revents(Channel::kReadEvent);
    } else { // write
        // 对写事件也采用0字节通知，避免IOCP与Connection发送路径混用。
        if (channel->isWriting()) {
            SOCKET fd = static_cast<SOCKET>(channel->fd());
            ctx->reset();
            DWORD bytesSent = 0;
            int rc = WSASend(fd, &ctx->wsaBuf, 1, &bytesSent, 0,
                             &ctx->overlapped, nullptr);
            if (rc == SOCKET_ERROR) {
                int err = WSAGetLastError();
                if (err != ERROR_IO_PENDING) {
                    LOG_WARN << "WSASend re-arm failed: " << err;
                }
            }
        }
        channel->set_revents(Channel::kWriteEvent);
    }

    activeChannels_.push_back(channel);
}

void IOCPLoop::updateChannel(Channel* channel) {
    assertInLoopThread();
    const int index = channel->index();
    LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events() 
              << " index = " << index;

    std::lock_guard<std::mutex> lock(channelsMutex_);
    SOCKET fd = static_cast<SOCKET>(channel->fd());

    if (index == -1) { // 新 channel
        assert(channels_.find(fd) == channels_.end());
        channels_[fd] = channel;
        channel->set_index(1); // 1 表示已添加

        // 将 socket 绑定到 IOCP
        HANDLE h = CreateIoCompletionPort(reinterpret_cast<HANDLE>(fd), iocpHandle_,
                                          reinterpret_cast<ULONG_PTR>(channel), 0);
        if (h == nullptr) {
            LOG_WARN << "CreateIoCompletionPort for fd " << fd << " failed";
        }

        // 开始异步接收
        if (channel->isReading()) {
            postRead(channel);
        }
    } else { // 更新现有 channel
        assert(channels_.find(fd) != channels_.end());
        assert(channels_[fd] == channel);

        if (channel->isReading()) {
            auto it = readContexts_.find(fd);
            if (it == readContexts_.end()) {
                postRead(channel);
            }
        }
        if (channel->isWriting()) {
            // 当前Connection写路径为同步send，IOCP层直接触发一次写回调以刷新输出缓冲。
            channel->set_revents(Channel::kWriteEvent);
            activeChannels_.push_back(channel);
        }
    }
}

void IOCPLoop::removeChannel(Channel* channel) {
    assertInLoopThread();
    SOCKET fd = static_cast<SOCKET>(channel->fd());
    LOG_TRACE << "fd = " << fd;

    std::lock_guard<std::mutex> lock(channelsMutex_);
    assert(channels_.find(fd) != channels_.end());
    assert(channels_[fd] == channel);
    assert(channel->isNoneEvent());

    assert(channel->index() == 1);

    // 清理上下文
    auto readIt = readContexts_.find(fd);
    if (readIt != readContexts_.end()) {
        delete readIt->second;
        readContexts_.erase(readIt);
    }

    auto writeIt = writeContexts_.find(fd);
    if (writeIt != writeContexts_.end()) {
        delete writeIt->second;
        writeContexts_.erase(writeIt);
    }

    channels_.erase(fd);
    channel->set_index(-1);
}

bool IOCPLoop::hasChannel(Channel* channel) const {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    SOCKET fd = static_cast<SOCKET>(channel->fd());
    return channels_.find(fd) != channels_.end();
}

void IOCPLoop::postRead(Channel* channel) {
    SOCKET fd = static_cast<SOCKET>(channel->fd());

    OverlappedContext* ctx = new OverlappedContext(channel, 0);
    readContexts_[fd] = ctx;

    DWORD flags = 0;
    DWORD bytesReceived = 0;
    int rc = WSARecv(fd, &ctx->wsaBuf, 1, &bytesReceived, &flags, 
                     &ctx->overlapped, nullptr);

    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING) {
            LOG_WARN << "WSARecv failed: " << err;
            delete ctx;
            readContexts_.erase(fd);
        }
    }
}

void IOCPLoop::postWrite(Channel* channel) {
    SOCKET fd = static_cast<SOCKET>(channel->fd());

    OverlappedContext* ctx = new OverlappedContext(channel, 1);
    writeContexts_[fd] = ctx;

    ctx->reset();
    DWORD bytesSent = 0;
    int rc = WSASend(fd, &ctx->wsaBuf, 1, &bytesSent, 0,
                     &ctx->overlapped, nullptr);

    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING) {
            LOG_WARN << "WSASend failed: " << err;
            delete ctx;
            writeContexts_.erase(fd);
        }
    }
}

void IOCPLoop::handleRead() {
    // 基类接口兼容
}

} // namespace kvstore
