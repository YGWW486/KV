#ifndef KVSTORE_TYPES_H
#define KVSTORE_TYPES_H

#include <cstdint>
#include <cstring>
#include <string>
#include <functional>
#include <memory>
#include <chrono>
#include <cstddef>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <BaseTsd.h>
#pragma comment(lib, "ws2_32.lib")
using ssize_t = SSIZE_T;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
// glibc <endian.h> often defines htobe16/htobe32/... as macros. Those would break our
// inline helpers below by renaming them (e.g. htobe16 -> __bswap_16) and then
// htons(...) becomes an ambiguous __bswap_16 overload in TUs that use both.
#undef htobe16
#undef htole16
#undef betoh16
#undef letoh16
#undef htobe32
#undef htole32
#undef betoh32
#undef letoh32
#undef htobe64
#undef htole64
#undef betoh64
#undef letoh64
#endif

namespace kvstore {

using std::string;
using std::shared_ptr;
using std::unique_ptr;
using std::function;

using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::microseconds;
using std::chrono::system_clock;
using std::chrono::steady_clock;

// StringPiece (类似 folly/abseil 的实现)
class StringPiece {
public:
    StringPiece() : data_(nullptr), size_(0) {}
    StringPiece(const char* str) : data_(str), size_(strlen(str)) {}
    StringPiece(const char* d, size_t n) : data_(d), size_(n) {}
    StringPiece(const std::string& str) : data_(str.data()), size_(str.size()) {}

    const char* data() const { return data_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    const char* begin() const { return data_; }
    const char* end() const { return data_ + size_; }

    char operator[](size_t i) const { return data_[i]; }

    std::string asString() const { return std::string(data_, size_); }

private:
    const char* data_;
    size_t size_;
};

// 字节序转换（跨平台兼容）
inline uint16_t htobe16(uint16_t x) { return htons(x); }
inline uint16_t htole16(uint16_t x) { return x; }
inline uint16_t betoh16(uint16_t x) { return ntohs(x); }
inline uint16_t letoh16(uint16_t x) { return x; }

inline uint32_t htobe32(uint32_t x) { return htonl(x); }
inline uint32_t htole32(uint32_t x) { return x; }
inline uint32_t betoh32(uint32_t x) { return ntohl(x); }
inline uint32_t letoh32(uint32_t x) { return x; }

inline uint64_t htobe64(uint64_t x) {
    return (uint64_t(htobe32(uint32_t(x & 0xffffffffULL))) << 32) |
           uint64_t(htobe32(uint32_t(x >> 32)));
}
inline uint64_t htole64(uint64_t x) { return x; }
inline uint64_t betoh64(uint64_t x) { return htobe64(x); }
inline uint64_t letoh64(uint64_t x) { return x; }

// 跨平台套接字类型
#ifdef _WIN32
using socket_t = SOCKET;
const socket_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_t = int;
const socket_t kInvalidSocket = -1;
#endif

} // namespace kvstore

#endif // KVSTORE_TYPES_H
