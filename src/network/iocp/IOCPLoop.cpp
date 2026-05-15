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

// ── OverlappedContext ──────────────────────────────────────────────

IOCPLoop::OverlappedContext::OverlappedContext(Channel* ch, int type, size_t bufferSize)
    : channel(ch), eventType(type), bytesTransferred(0)
{
    ZeroMemory(&overlapped, sizeof(overlapped));
    ioBuffer.resize(bufferSize);
    wsaBuf.buf = ioBuffer.data();
    wsaBuf.len = (eventType == 0) ? static_cast<ULONG>(bufferSize) : 0;
}

void IOCPLoop::OverlappedContext::resetForRead(size_t bufferSize) {
    eventType = 0;
    bytesTransferred = 0;
    ZeroMemory(&overlapped, sizeof(overlapped));
    ioBuffer.resize(bufferSize);
    wsaBuf.buf = ioBuffer.data();
    wsaBuf.len = static_cast<ULONG>(bufferSize);
}

void IOCPLoop::OverlappedContext::resetForWrite(const char* data, size_t len) {
    eventType = 1;
    bytesTransferred = 0;
    ZeroMemory(&overlapped, sizeof(overlapped));
    ioBuffer.assign(data, data + len);
    wsaBuf.buf = ioBuffer.data();
    wsaBuf.len = static_cast<ULONG>(len);
}

// ── IOCPLoop constructor / destructor ──────────────────────────────

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

    // Build a TCP socket-pair for wakeup (loopback connect/accept)
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
    // Clean up any remaining contexts
    for (auto& kv : readContexts_) delete kv.second;
    for (auto& kv : writeContexts_) delete kv.second;
    for (auto* ctx : zombieContexts_) delete ctx;
    if (wsaStartedByIocpLoop_) {
        WSACleanup();
        wsaStartedByIocpLoop_ = false;
    }
    LOG_INFO << "IOCPLoop " << this << " destructed";
}

// ── Event loop ─────────────────────────────────────────────────────

void IOCPLoop::loop() {
    assert(!looping_);
    assertInLoopThread();
    looping_ = true;
    quit_ = false;

    LOG_INFO << "IOCPLoop " << this << " start looping";

    while (!quit_) {
        activeChannels_.clear();

        Timestamp nextExp = nextExpiration();
        DWORD timeoutMs = 1000;
        if (nextExp.valid()) {
            double diff = timeDifference(nextExp, Timestamp::now());
            if (diff <= 0.0) timeoutMs = 0;
            else if (diff < 1.0) timeoutMs = static_cast<DWORD>(diff * 1000.0);
        }

        handleCompletions(timeoutMs);

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
    if (!iocpHandle_) return;
    if (!PostQueuedCompletionStatus(iocpHandle_, 0, reinterpret_cast<ULONG_PTR>(this), nullptr)) {
        LOG_WARN << "PostQueuedCompletionStatus (wakeup) failed: " << GetLastError();
    }
}

void IOCPLoop::handleWakeup() {
    // Wakeup is a no-op — simply interrupts GetQueuedCompletionStatus so the
    // loop can check quit_ and process pending functors.
}

// ── Completion handling ────────────────────────────────────────────

void IOCPLoop::handleCompletions(DWORD initialTimeoutMs) {
    DWORD bytesTransferred = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED pOverlapped = nullptr;

    pollReturnTime_ = Timestamp::now();

    BOOL success = GetQueuedCompletionStatus(
        iocpHandle_,
        &bytesTransferred,
        &completionKey,
        &pOverlapped,
        initialTimeoutMs
    );

    const DWORD gqcsErr = success ? ERROR_SUCCESS : GetLastError();

    // Timeout — no completions available
    if (success == FALSE && pOverlapped == nullptr && gqcsErr == WAIT_TIMEOUT) {
        return;
    }

    // Wakeup notification (pOverlapped is null, key matches loop pointer)
    if (pOverlapped == nullptr &&
        completionKey == reinterpret_cast<ULONG_PTR>(this)) {
        handleWakeup();
        return;
    }

    // Unexpected null overlapped
    if (pOverlapped == nullptr) {
        LOG_WARN << "GetQueuedCompletionStatus (no packet): " << gqcsErr;
        return;
    }

    OverlappedContext* ctx = reinterpret_cast<OverlappedContext*>(pOverlapped);

    // Zombie context — cancelled operation, just clean up
    if (zombieContexts_.count(ctx)) {
        zombieContexts_.erase(ctx);
        delete ctx;
        return;
    }

    Channel* channel = ctx->channel;
    if (channel == nullptr) {
        delete ctx;
        return;
    }

    SOCKET fd = static_cast<SOCKET>(channel->fd());

    // Operation failed
    if (success == FALSE) {
        LOG_WARN << "IOCP operation failed for fd " << fd
                 << " type=" << ctx->eventType << " err=" << gqcsErr;
        channel->set_revents(Channel::kErrorEvent);
        activeChannels_.push_back(channel);
        delete ctx;
        if (ctx->eventType == 0) readContexts_.erase(fd);
        else writeContexts_.erase(fd);
        return;
    }

    ctx->bytesTransferred = bytesTransferred;

    if (ctx->eventType == 0) {
        // ── Read completion ────────────────────────────────────
        if (bytesTransferred > 0) {
            ctx->ioBuffer.resize(bytesTransferred);
            completedReads_[fd] = std::move(ctx->ioBuffer);
            channel->set_revents(Channel::kReadEvent);
            // Re-arm read for the next incoming data
            delete ctx;
            readContexts_.erase(fd);
            postRead(channel);
        } else {
            // Zero bytes = graceful close by peer
            channel->set_revents(Channel::kCloseEvent);
            delete ctx;
            readContexts_.erase(fd);
        }
    } else {
        // ── Write completion ───────────────────────────────────
        completedWrites_[fd] = bytesTransferred;
        channel->set_revents(Channel::kWriteEvent);
        delete ctx;
        writeContexts_.erase(fd);
    }

    activeChannels_.push_back(channel);
}

// ── Channel management ─────────────────────────────────────────────

void IOCPLoop::updateChannel(Channel* channel) {
    assertInLoopThread();
    const int index = channel->index();
    LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events()
              << " index = " << index;

    std::lock_guard<std::mutex> lock(channelsMutex_);
    SOCKET fd = static_cast<SOCKET>(channel->fd());

    if (index == -1) {
        // New channel
        assert(channels_.find(fd) == channels_.end());
        channels_[fd] = channel;
        channel->set_index(1);

        HANDLE h = CreateIoCompletionPort(reinterpret_cast<HANDLE>(fd), iocpHandle_,
                                          reinterpret_cast<ULONG_PTR>(channel), 0);
        if (h == nullptr) {
            LOG_WARN << "CreateIoCompletionPort for fd " << fd << " failed";
        }

        if (channel->isReading()) {
            postRead(channel);
        }
        // Write posting is done directly by Connection via postWriteData(),
        // not triggered here — the Connection owns the output buffer data.
    } else {
        // Update existing channel
        assert(channels_.find(fd) != channels_.end());
        assert(channels_[fd] == channel);

        if (channel->isReading()) {
            auto it = readContexts_.find(fd);
            if (it == readContexts_.end()) {
                postRead(channel);
            }
        }
        // Write path: Connection calls postWriteData() directly after
        // enabling writing. No implicit write posting here.
    }
}

void IOCPLoop::removeChannel(Channel* channel) {
    assertInLoopThread();
    SOCKET fd = static_cast<SOCKET>(channel->fd());
    LOG_TRACE << "fd = " << fd;

    std::lock_guard<std::mutex> lock(channelsMutex_);
    assert(channels_.find(fd) != channels_.end());
    assert(channels_[fd] == channel);
    assert(channel->isNoneEvent() || channel->index() == 1);

    // Cancel pending I/O operations on this socket
    CancelIoEx(reinterpret_cast<HANDLE>(fd), nullptr);

    // Move contexts to zombie set — their completions will arrive with
    // ERROR_OPERATION_ABORTED and be cleaned up safely.
    auto readIt = readContexts_.find(fd);
    if (readIt != readContexts_.end()) {
        zombieContexts_.insert(readIt->second);
        readContexts_.erase(readIt);
    }

    auto writeIt = writeContexts_.find(fd);
    if (writeIt != writeContexts_.end()) {
        zombieContexts_.insert(writeIt->second);
        writeContexts_.erase(writeIt);
    }

    completedReads_.erase(fd);
    completedWrites_.erase(fd);
    channels_.erase(fd);
    channel->set_index(-1);
}

bool IOCPLoop::hasChannel(Channel* channel) const {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    SOCKET fd = static_cast<SOCKET>(channel->fd());
    return channels_.find(fd) != channels_.end();
}

// ── Async I/O operations ───────────────────────────────────────────

void IOCPLoop::postRead(Channel* channel) {
    SOCKET fd = static_cast<SOCKET>(channel->fd());

    // Don't post a read if one is already pending
    if (readContexts_.find(fd) != readContexts_.end()) return;

    OverlappedContext* ctx = new OverlappedContext(channel, 0, 65536);
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

void IOCPLoop::postWriteData(SOCKET fd, const char* data, size_t len) {
    if (len == 0) return;

    Channel* channel = nullptr;
    {
        std::lock_guard<std::mutex> lock(channelsMutex_);
        auto it = channels_.find(fd);
        if (it != channels_.end()) channel = it->second;
    }
    if (channel == nullptr) return;

    // Don't post if one is already pending
    if (writeContexts_.find(fd) != writeContexts_.end()) return;

    OverlappedContext* ctx = new OverlappedContext(channel, 1, len);
    ctx->resetForWrite(data, len);
    writeContexts_[fd] = ctx;

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

// ── Data retrieval for Connection ──────────────────────────────────

std::vector<char> IOCPLoop::takeReadData(SOCKET fd) {
    auto it = completedReads_.find(fd);
    if (it != completedReads_.end()) {
        std::vector<char> data = std::move(it->second);
        completedReads_.erase(it);
        return data;
    }
    return {};
}

DWORD IOCPLoop::takeWriteResult(SOCKET fd) {
    auto it = completedWrites_.find(fd);
    if (it != completedWrites_.end()) {
        DWORD result = it->second;
        completedWrites_.erase(it);
        return result;
    }
    return 0;
}

bool IOCPLoop::hasReadClosed(SOCKET fd) {
    // After a close event is processed, the read context is gone and no
    // completed read data exists — the close is signaled via kCloseEvent.
    return readContexts_.find(fd) == readContexts_.end()
        && completedReads_.find(fd) == completedReads_.end();
}

void IOCPLoop::handleRead() {
    // Base class interface compatibility — IOCP uses handleCompletions().
}

} // namespace kvstore
