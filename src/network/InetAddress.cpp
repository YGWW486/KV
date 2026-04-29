#include "network/InetAddress.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>

namespace kvstore {

InetAddress::InetAddress(uint16_t port, bool loopbackOnly) {
    memset(&addr_, 0, sizeof addr_);
    addr_.sin_family = AF_INET;
    addr_.sin_addr.s_addr = loopbackOnly ? htonl(INADDR_LOOPBACK) : htonl(INADDR_ANY);
    addr_.sin_port = htons(port);
}

InetAddress::InetAddress(const char* ip, uint16_t port) {
    memset(&addr_, 0, sizeof addr_);
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr_.sin_addr);
}

std::string InetAddress::toIp() const {
    char buf[64];
    inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof buf);
    return buf;
}

std::string InetAddress::toIpPort() const {
    char buf[64];
    inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof buf);
    uint16_t hostPort = ntohs(addr_.sin_port);
    char portBuf[16];
    sprintf(portBuf, ":%u", hostPort);
    return std::string(buf) + portBuf;
}

uint16_t InetAddress::port() const {
    return ntohs(addr_.sin_port);
}

} // namespace kvstore
