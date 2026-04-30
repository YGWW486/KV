#ifndef KVSTORE_NETWORK_INETADDRESS_H
#define KVSTORE_NETWORK_INETADDRESS_H

#include "kvstore/Types.h"

#include <string>

namespace kvstore {

class InetAddress {
public:
    explicit InetAddress(uint16_t port = 0, bool loopbackOnly = false);
    InetAddress(const char* ip, uint16_t port);
    explicit InetAddress(const struct sockaddr_in& addr) : addr_(addr) {}

    const struct sockaddr_in& getSockAddrInet() const { return addr_; }
    void setSockAddrInet(const struct sockaddr_in& addr) { addr_ = addr; }

    const struct sockaddr* getSockAddr() const { 
        return reinterpret_cast<const struct sockaddr*>(&addr_); 
    }
    struct sockaddr* getSockAddr() { 
        return reinterpret_cast<struct sockaddr*>(&addr_); 
    }

    std::string toIp() const;
    std::string toIpPort() const;
    uint16_t port() const;

private:
    struct sockaddr_in addr_;
};

} // namespace kvstore

#endif // KVSTORE_NETWORK_INETADDRESS_H
