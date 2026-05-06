#include "../../src/network/Buffer.h"
#include <string>
#include <cstring>
#include <cstdlib>

using namespace kvstore;

static int g_failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        ++g_failures;
        fprintf(stderr, "FAIL: %s\n", msg);
    }
}

// ---- Initial state ----
static bool test_initial_state() {
    Buffer buf;
    check(buf.readableBytes() == 0, "initial readableBytes is 0");
    check(buf.writableBytes() > 0, "initial writableBytes > 0");
    check(buf.prependableBytes() == Buffer::kCheapPrepend, "initial prependableBytes is kCheapPrepend");
    return true;
}

// ---- Append + retrieve ----
static bool test_append_retrieve() {
    Buffer buf;
    buf.append("Hello", 5);
    check(buf.readableBytes() == 5, "readableBytes after append 5");
    std::string s = buf.retrieveAsString(3);
    check(s == "Hel", "retrieveAsString(3) == \"Hel\"");
    check(buf.readableBytes() == 2, "readableBytes after retrieve 3");
    s = buf.retrieveAsString(2);
    check(s == "lo", "retrieveAsString(2) == \"lo\"");
    check(buf.readableBytes() == 0, "readableBytes after retrieve all");
    return true;
}

// ---- retrieveAllAsString ----
static bool test_retrieve_all_as_string() {
    Buffer buf;
    buf.append("World");
    std::string s = buf.retrieveAllAsString();
    check(s == "World", "retrieveAllAsString == \"World\"");
    check(buf.readableBytes() == 0, "readableBytes 0 after retrieveAllAsString");
    return true;
}

// ---- CRLF search ----
static bool test_find_crlf() {
    Buffer buf;
    buf.append("GET / HTTP/1.1\r\n", 16);
    const char* p = buf.findCRLF();
    check(p != nullptr, "findCRLF returns non-null");
    check(p == buf.peek() + 14, "findCRLF points to \\r");
    check(*p == '\r' && *(p + 1) == '\n', "findCRLF points to CRLF");

    buf.append("Host: localhost\r\n", 17);
    p = buf.findCRLF(buf.peek() + 16);
    check(p != nullptr, "findCRLF from offset returns non-null");
    return true;
}

// ---- findCRLF not found ----
static bool test_find_crlf_not_found() {
    Buffer buf;
    buf.append("Hello World, no crlf here", 25);
    check(buf.findCRLF() == nullptr, "findCRLF returns null when no CRLF");
    return true;
}

// ---- Large append (triggers makeSpace/grow) ----
static bool test_large_append() {
    Buffer buf;
    std::string big(4096, 'A');
    buf.append(big.data(), big.size());
    check(buf.readableBytes() == 4096, "large append readableBytes");
    std::string back = buf.retrieveAllAsString();
    check(back == big, "large append roundtrip");
    return true;
}

// ---- peek / retrieveUntil ----
static bool test_retrieve_until() {
    Buffer buf;
    buf.append("abcdefghij", 10);
    buf.retrieveUntil(buf.peek() + 4);
    check(buf.readableBytes() == 6, "retrieveUntil(4) leaves 6");
    std::string s = buf.retrieveAsString(6);
    check(s == "efghij", "remaining after retrieveUntil is efghij");
    return true;
}

// ---- prepend ----
static bool test_prepend() {
    Buffer buf;
    buf.append("World", 5);
    buf.prepend("Hello ", 6);
    check(buf.readableBytes() == 11, "prepend readableBytes 11");
    std::string s = buf.retrieveAllAsString();
    check(s == "Hello World", "prepend roundtrip");
    return true;
}

// ---- toStringPiece ----
static bool test_to_string_piece() {
    Buffer buf;
    buf.append("abc", 3);
    StringPiece sp = buf.toStringPiece();
    check(sp.size() == 3, "toStringPiece size 3");
    check(sp[0] == 'a' && sp[1] == 'b' && sp[2] == 'c', "toStringPiece content");
    return true;
}

// ---- Append StringPiece ----
static bool test_append_string_piece() {
    Buffer buf;
    StringPiece sp("Hello", 5);
    buf.append(sp);
    check(buf.readableBytes() == 5, "append StringPiece readableBytes");
    return true;
}

// ---- retrieveAll ----
static bool test_retrieve_all() {
    Buffer buf;
    buf.append("data", 4);
    buf.retrieveAll();
    check(buf.readableBytes() == 0, "readableBytes 0 after retrieveAll");
    check(buf.writableBytes() > 0, "writableBytes > 0 after retrieveAll");
    return true;
}

// ---- Multiple append cycles (internal rearrangement) ----
static bool test_multiple_cycles() {
    Buffer buf;
    for (int i = 0; i < 100; ++i) {
        buf.append("0123456789", 10);
    }
    check(buf.readableBytes() == 1000, "1000 bytes readable after loop");
    for (int i = 0; i < 100; ++i) {
        std::string s = buf.retrieveAsString(10);
        check(s == "0123456789", "cycle retrieve matches");
    }
    check(buf.readableBytes() == 0, "0 readable after cycle retrieves");
    return true;
}

// ---- shrink ----
static bool test_shrink() {
    Buffer buf;
    buf.append(std::string(5000, 'X').data(), 5000);
    size_t cap_before = buf.internalCapacity();
    buf.retrieveAll();
    buf.shrink(256);
    size_t cap_after = buf.internalCapacity();
    check(cap_after < cap_before, "shrink reduces capacity");
    return true;
}

// ---- ensureWritableBytes triggers grow ----
static bool test_ensure_writable() {
    Buffer buf;
    size_t cap0 = buf.internalCapacity();
    buf.ensureWritableBytes(cap0 + 4096);
    check(buf.internalCapacity() >= cap0 + 4096, "capacity grows for ensureWritableBytes");
    return true;
}

int main() {
    test_initial_state();
    test_append_retrieve();
    test_retrieve_all_as_string();
    test_find_crlf();
    test_find_crlf_not_found();
    test_large_append();
    test_retrieve_until();
    test_prepend();
    test_to_string_piece();
    test_append_string_piece();
    test_retrieve_all();
    test_multiple_cycles();
    test_shrink();
    test_ensure_writable();

    if (g_failures == 0) {
        fprintf(stdout, "test_buffer: ALL TESTS PASSED\n");
        return 0;
    }
    fprintf(stderr, "test_buffer: %d FAILURE(S)\n", g_failures);
    return 1;
}
