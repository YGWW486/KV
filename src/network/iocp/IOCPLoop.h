#ifndef KVSTORE_NETWORK_IOCP_IOCPLOOP_H
#define KVSTORE_NETWORK_IOCP_IOCPLOOP_H

#include "network/EventLoop.h"
#include "network/Buffer.h"
#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <vector>
#include <unordered_map>
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

private:
    // 重叠 I/O 上下文
    struct OverlappedContext {
        OVERLAPPED overlapped;
        Channel* channel;
        int eventType; // 0: read, 1: write
        Buffer buffer;
        WSABUF wsaBuf;

        explicit OverlappedContext(Channel* ch, int type = 0);
        void reset();
    };

    void wakeup();
    void handleWakeup();
    void handleRead(); // 兼容基类（保留）
    void handleCompletions();
    void postCompletion(Channel* channel, DWORD bytesTransferred);
    void postRead(Channel* channel);
    void postWrite(Channel* channel);

    HANDLE iocpHandle_;
    HANDLE wakeupEvent_;
    SOCKET wakeupSocket_[2];
    std::unordered_map<SOCKET, Channel*> channels_;
    std::unordered_map<SOCKET, OverlappedContext*> readContexts_;
    std::unordered_map<SOCKET, OverlappedContext*> writeContexts_;
    mutable std::mutex channelsMutex_;
    std::atomic<bool> quitPending_;
};

} // namespace kvstore

#endif // KVSTORE_NETWORK_IOCP_IOCPLOOP_H
