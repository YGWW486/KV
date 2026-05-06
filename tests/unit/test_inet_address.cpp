#include "../../src/network/InetAddress.h"
#include <cstdlib>
#include <cstdio>
#include <string>

using namespace kvstore;

static int g_failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) { ++g_failures; fprintf(stderr, "FAIL: %s\n", msg); }
}

// ---- Port-only constructor ----
static bool test_port_only() {
    InetAddress addr(8080, true);
    check(addr.port() == 8080, "port only port() == 8080");
    std::string ip = addr.toIp();
    check(!ip.empty(), "port only toIp not empty");
    std::string ipp = addr.toIpPort();
    check(!ipp.empty(), "port only toIpPort not empty");
    return true;
}

// ---- IP + port constructor ----
static bool test_ip_port() {
    InetAddress addr("127.0.0.1", 6379);
    check(addr.port() == 6379, "ip+port port() == 6379");
    check(addr.toIp() == "127.0.0.1", "ip+port toIp == 127.0.0.1");
    check(addr.toIpPort() == "127.0.0.1:6379", "ip+port toIpPort == 127.0.0.1:6379");
    return true;
}

// ---- sockaddr_in constructor ----
static bool test_from_sockaddr() {
    sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(3000);
    sin.sin_addr.s_addr = inet_addr("192.168.1.1");
    InetAddress addr(sin);
    check(addr.port() == 3000, "sockaddr port == 3000");
    check(addr.toIp() == "192.168.1.1", "sockaddr toIp");
    return true;
}

// ---- toIpPort format ----
static bool test_to_ip_port_format() {
    InetAddress addr("0.0.0.0", 9999);
    std::string s = addr.toIpPort();
    check(s == "0.0.0.0:9999", "toIpPort format");
    return true;
}

// ---- Default port ----
static bool test_default_port() {
    InetAddress addr(0, false);
    check(addr.port() == 0, "port 0");
    return true;
}

// ---- setSockAddrInet ----
static bool test_set_sockaddr() {
    InetAddress addr(1234, true);
    sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(5678);
    sin.sin_addr.s_addr = inet_addr("10.0.0.1");
    addr.setSockAddrInet(sin);
    check(addr.port() == 5678, "setSockAddrInet port updated");
    check(addr.toIp() == "10.0.0.1", "setSockAddrInet IP updated");
    return true;
}

// ---- getSockAddr returns valid pointer ----
static bool test_get_sockaddr() {
    InetAddress addr(5678, true);
    const struct sockaddr* sa = addr.getSockAddr();
    check(sa != nullptr, "getSockAddr non-null");
    check(sa->sa_family == AF_INET, "getSockAddr sa_family == AF_INET");
    return true;
}

int main() {
    test_port_only();
    test_ip_port();
    test_from_sockaddr();
    test_to_ip_port_format();
    test_default_port();
    test_set_sockaddr();
    test_get_sockaddr();

    if (g_failures == 0) {
        fprintf(stdout, "test_inet_address: ALL TESTS PASSED\n");
        return 0;
    }
    fprintf(stderr, "test_inet_address: %d FAILURE(S)\n", g_failures);
    return 1;
}
