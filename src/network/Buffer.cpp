#include "network/Buffer.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace kvstore {

const char Buffer::kCRLF[] = "\r\n";

#ifdef _WIN32

ssize_t Buffer::readFd(SOCKET sockfd, int* savedErrno) {
    char extrabuf[65536];
    DWORD bytesRead = 0;
    DWORD flags = 0;
    WSABUF wsaBufs[2];
    
    size_t writable = writableBytes();
    wsaBufs[0].buf = beginWrite();
    wsaBufs[0].len = static_cast<ULONG>(writable);
    wsaBufs[1].buf = extrabuf;
    wsaBufs[1].len = sizeof(extrabuf);
    
    int rc = WSARecv(sockfd, wsaBufs, 2, &bytesRead, &flags, nullptr, nullptr);
    
    if (rc == SOCKET_ERROR) {
        *savedErrno = WSAGetLastError();
        return -1;
    }
    
    ssize_t n = bytesRead;
    if (n <= 0) {
        return n;
    }
    
    if (static_cast<size_t>(n) <= writable) {
        writerIndex_ += n;
    } else {
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writable);
    }
    
    return n;
}

ssize_t Buffer::writeFd(SOCKET sockfd, int* savedErrno) {
    size_t n = readableBytes();
    const char* data = peek();
    int rc = send(sockfd, data, static_cast<int>(n), 0);
    
    if (rc == SOCKET_ERROR) {
        *savedErrno = WSAGetLastError();
        return -1;
    }
    
    if (rc > 0) {
        retrieve(rc);
    }
    
    return rc;
}

#else

ssize_t Buffer::readFd(int sockfd, int* savedErrno) {
    char extrabuf[65536];
    struct iovec vec[2];
    const size_t writable = writableBytes();
    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);
    
    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    const ssize_t n = readv(sockfd, vec, iovcnt);
    
    if (n < 0) {
        *savedErrno = errno;
    } else if (static_cast<size_t>(n) <= writable) {
        writerIndex_ += n;
    } else {
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writable);
    }
    
    return n;
}

ssize_t Buffer::writeFd(int sockfd, int* savedErrno) {
    ssize_t n = write(sockfd, peek(), readableBytes());
    
    if (n < 0) {
        *savedErrno = errno;
    } else {
        retrieve(n);
    }
    
    return n;
}

#endif

} // namespace kvstore
