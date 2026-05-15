#ifndef KVSTORE_NETWORK_IOCP_IOCPLOOP_H
#define KVSTORE_NETWORK_IOCP_IOCPLOOP_H

#include "network/EventLoop.h"
#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace kvstore {

class IOCPLoop : public EventLoop {
public:
    IOCPLoop();
    ~IOCPLoop() override;

    void loop() override;
    void quit() override;

    void updateChannel(Channel* channel) override;
    void removeChannel(Channel* channel) override;
    bool hasChannel(Channel* channel) const override;

    HANDLE iocpHandle() const { return iocpHandle_; }

    // Async I/O interface for Connection
    void postRead(Channel* channel);
    void postWriteData(SOCKET fd, const char* data, size_t len);
    std::vector<char> takeReadData(SOCKET fd);
    DWORD takeWriteResult(SOCKET fd);
    bool hasReadClosed(SOCKET fd);

private:
    struct OverlappedContext {
        OVERLAPPED overlapped;
        Channel* channel;
        int eventType; // 0: read, 1: write
        std::vector<char> ioBuffer;
        WSABUF wsaBuf;
        DWORD bytesTransferred;

        explicit OverlappedContext(Channel* ch, int type, size_t bufferSize = 65536);
        void resetForRead(size_t bufferSize = 65536);
        void resetForWrite(const char* data, size_t len);
    };

    void wakeup() override;
    void handleWakeup();
    void handleRead(); // base class compatibility
    void handleCompletions(DWORD initialTimeoutMs);
    void cleanupZombieContext(OverlappedContext* ctx);
    void drainZombieForFd(SOCKET fd);

    HANDLE iocpHandle_;
    SOCKET wakeupSocket_[2];
    std::unordered_map<SOCKET, Channel*> channels_;
    std::unordered_map<SOCKET, OverlappedContext*> readContexts_;
    std::unordered_map<SOCKET, OverlappedContext*> writeContexts_;
    std::unordered_map<SOCKET, std::vector<char>> completedReads_;
    std::unordered_map<SOCKET, DWORD> completedWrites_;
    std::unordered_set<OverlappedContext*> zombieContexts_;
    mutable std::mutex channelsMutex_;
    std::atomic<bool> quitPending_;
    bool wsaStartedByIocpLoop_ = false;
};

} // namespace kvstore

#endif // KVSTORE_NETWORK_IOCP_IOCPLOOP_H
