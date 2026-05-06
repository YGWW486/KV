#include "../../src/protocol/RESP.h"
#include <string>
#include <cstdlib>
#include <cstdio>

using namespace kvstore;

static int g_failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) { ++g_failures; fprintf(stderr, "FAIL: %s\n", msg); }
}

// ---- SimpleString ----
static bool test_simple_string() {
    RESPSimpleString ss("OK");
    check(ss.type() == RESPType::SimpleString, "SimpleString type");
    check(ss.value() == "OK", "SimpleString value");
    check(ss.toString() == "+OK\r\n", "SimpleString toString +OK\\r\\n");

    RESPSimpleString ss2("");
    check(ss2.toString() == "+\r\n", "SimpleString empty toString +\\r\\n");
    return true;
}

// ---- Error ----
static bool test_error() {
    RESPError err("ERR unknown command");
    check(err.type() == RESPType::Error, "Error type");
    check(err.message() == "ERR unknown command", "Error message");
    check(err.toString() == "-ERR unknown command\r\n", "Error toString");
    return true;
}

// ---- Integer ----
static bool test_integer() {
    RESPInteger i0(0);
    check(i0.type() == RESPType::Integer, "Integer type");
    check(i0.value() == 0, "Integer value 0");
    check(i0.toString() == ":0\r\n", "Integer toString :0");

    RESPInteger iPos(42);
    check(iPos.toString() == ":42\r\n", "Integer toString :42");

    RESPInteger iNeg(-7);
    check(iNeg.toString() == ":-7\r\n", "Integer toString :-7");

    RESPInteger iMax(INT64_MAX);
    check(!iMax.toString().empty(), "Integer INT64_MAX not empty");
    return true;
}

// ---- BulkString ----
static bool test_bulk_string() {
    RESPBulkString bs("Hello");
    check(bs.type() == RESPType::BulkString, "BulkString type");
    check(bs.value() == "Hello", "BulkString value");
    check(!bs.isNull(), "BulkString not null");
    check(bs.toString() == "$5\r\nHello\r\n", "BulkString toString");

    RESPBulkString empty("");
    check(empty.toString() == "$0\r\n\r\n", "BulkString empty toString");

    RESPBulkString nil = RESPBulkString::null();
    check(nil.isNull(), "null BulkString isNull");
    check(nil.toString() == "$-1\r\n", "null BulkString toString $-1");
    return true;
}

// ---- Array ----
static bool test_array() {
    std::vector<std::shared_ptr<RESPObject>> elems;
    elems.push_back(std::make_shared<RESPBulkString>("SET"));
    elems.push_back(std::make_shared<RESPBulkString>("key"));
    elems.push_back(std::make_shared<RESPBulkString>("value"));
    RESPArray arr(std::move(elems));
    check(arr.type() == RESPType::Array, "Array type");
    check(arr.size() == 3, "Array size 3");
    check(!arr.isNull(), "Array not null");
    check(arr.toString() == "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n", "Array toString");

    RESPArray nilArr = RESPArray::null();
    check(nilArr.isNull(), "null Array isNull");
    check(nilArr.toString() == "*-1\r\n", "null Array toString");
    return true;
}

// ---- Empty Array ----
static bool test_empty_array() {
    RESPArray arr({});
    check(arr.size() == 0, "empty Array size 0");
    check(arr.toString() == "*0\r\n", "empty Array toString");
    return true;
}

// ---- Nested Array ----
static bool test_nested_array() {
    auto inner = std::make_shared<RESPArray>(std::vector<std::shared_ptr<RESPObject>>{
        std::make_shared<RESPSimpleString>("OK"),
        std::make_shared<RESPInteger>(1)
    });
    RESPArray outer({inner, std::make_shared<RESPBulkString>("hello")});
    check(outer.size() == 2, "nested Array size 2");
    std::string s = outer.toString();
    check(s == "*2\r\n*2\r\n+OK\r\n:1\r\n$5\r\nhello\r\n", "nested Array toString");
    return true;
}

// ---- RESP basic roundtrip for common types ----
static bool test_roundtrip_all_types() {
    // SimpleString round-trip
    check(RESPSimpleString("PONG").toString() == "+PONG\r\n", "PONG roundtrip");
    // Error round-trip
    check(RESPError("ERR test").toString() == "-ERR test\r\n", "Error roundtrip");
    // Integer round-trip
    check(RESPInteger(100).toString() == ":100\r\n", "Integer 100 roundtrip");
    // BulkString round-trip
    check(RESPBulkString("test").toString() == "$4\r\ntest\r\n", "BulkString test roundtrip");
    return true;
}

int main() {
    test_simple_string();
    test_error();
    test_integer();
    test_bulk_string();
    test_array();
    test_empty_array();
    test_nested_array();
    test_roundtrip_all_types();

    if (g_failures == 0) {
        fprintf(stdout, "test_resp: ALL TESTS PASSED\n");
        return 0;
    }
    fprintf(stderr, "test_resp: %d FAILURE(S)\n", g_failures);
    return 1;
}
