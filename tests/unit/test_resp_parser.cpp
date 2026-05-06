#include "../../src/protocol/RESPParser.h"
#include "../../src/network/Buffer.h"
#include <cstdlib>
#include <cstdio>
#include <memory>

using namespace kvstore;

static int g_failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) { ++g_failures; fprintf(stderr, "FAIL: %s\n", msg); }
}

// Helper: feed data and parse one object
static ParseResult feedParse(Buffer* buf, RESPParser* parser,
                              const char* data, std::shared_ptr<RESPObject>* out) {
    buf->append(data, strlen(data));
    return parser->parse(buf, out);
}

// ---- Parse SimpleString ----
static bool test_parse_simple_string() {
    Buffer buf; RESPParser parser; std::shared_ptr<RESPObject> obj;
    ParseResult r = feedParse(&buf, &parser, "+OK\r\n", &obj);
    check(r == ParseResult::Success, "SS parse success");
    check(obj->type() == RESPType::SimpleString, "SS type");
    check(std::static_pointer_cast<RESPSimpleString>(obj)->value() == "OK", "SS value");
    check(buf.readableBytes() == 0, "SS buffer consumed");
    return true;
}

// ---- Parse Error ----
static bool test_parse_error() {
    Buffer buf; RESPParser parser; std::shared_ptr<RESPObject> obj;
    ParseResult r = feedParse(&buf, &parser, "-ERR something\r\n", &obj);
    check(r == ParseResult::Success, "Error parse success");
    check(obj->type() == RESPType::Error, "Error type");
    check(std::static_pointer_cast<RESPError>(obj)->message() == "ERR something", "Error msg");
    return true;
}

// ---- Parse Integer ----
static bool test_parse_integer() {
    Buffer buf; RESPParser parser; std::shared_ptr<RESPObject> obj;
    ParseResult r = feedParse(&buf, &parser, ":12345\r\n", &obj);
    check(r == ParseResult::Success, "Integer parse success");
    check(obj->type() == RESPType::Integer, "Integer type");
    check(std::static_pointer_cast<RESPInteger>(obj)->value() == 12345, "Integer value");
    return true;
}

// ---- Parse negative Integer ----
static bool test_parse_negative_integer() {
    Buffer buf; RESPParser parser; std::shared_ptr<RESPObject> obj;
    ParseResult r = feedParse(&buf, &parser, ":-1\r\n", &obj);
    check(r == ParseResult::Success, "negative int parse success");
    check(std::static_pointer_cast<RESPInteger>(obj)->value() == -1, "negative int value");
    return true;
}

// ---- Parse BulkString ----
static bool test_parse_bulk_string() {
    Buffer buf; RESPParser parser; std::shared_ptr<RESPObject> obj;
    ParseResult r = feedParse(&buf, &parser, "$5\r\nhello\r\n", &obj);
    check(r == ParseResult::Success, "BS parse success");
    check(obj->type() == RESPType::BulkString, "BS type");
    check(std::static_pointer_cast<RESPBulkString>(obj)->value() == "hello", "BS value");
    return true;
}

// ---- Parse null BulkString ----
static bool test_parse_null_bulk_string() {
    Buffer buf; RESPParser parser; std::shared_ptr<RESPObject> obj;
    ParseResult r = feedParse(&buf, &parser, "$-1\r\n", &obj);
    check(r == ParseResult::Success, "null BS parse success");
    check(std::static_pointer_cast<RESPBulkString>(obj)->isNull(), "null BS isNull");
    return true;
}

// ---- Parse empty BulkString ----
static bool test_parse_empty_bulk_string() {
    Buffer buf; RESPParser parser; std::shared_ptr<RESPObject> obj;
    ParseResult r = feedParse(&buf, &parser, "$0\r\n\r\n", &obj);
    check(r == ParseResult::Success, "empty BS parse success");
    check(std::static_pointer_cast<RESPBulkString>(obj)->value() == "", "empty BS value");
    return true;
}

// ---- Parse Array ----
static bool test_parse_array() {
    Buffer buf; RESPParser parser; std::shared_ptr<RESPObject> obj;
    ParseResult r = feedParse(&buf, &parser,
                              "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n", &obj);
    check(r == ParseResult::Success, "Array parse success");
    check(obj->type() == RESPType::Array, "Array type");
    auto arr = std::static_pointer_cast<RESPArray>(obj);
    check(arr->size() == 3, "Array size 3");
    check(std::static_pointer_cast<RESPBulkString>(arr->elements()[0])->value() == "SET", "arr[0]");
    check(std::static_pointer_cast<RESPBulkString>(arr->elements()[1])->value() == "key", "arr[1]");
    check(std::static_pointer_cast<RESPBulkString>(arr->elements()[2])->value() == "value", "arr[2]");
    return true;
}

// ---- Parse nested Array ----
static bool test_parse_nested_array() {
    Buffer buf; RESPParser parser; std::shared_ptr<RESPObject> obj;
    ParseResult r = feedParse(&buf, &parser,
                              "*2\r\n*1\r\n:42\r\n$4\r\ntest\r\n", &obj);
    check(r == ParseResult::Success, "nested Array parse success");
    auto outer = std::static_pointer_cast<RESPArray>(obj);
    check(outer->size() == 2, "outer size 2");
    auto inner = std::static_pointer_cast<RESPArray>(outer->elements()[0]);
    check(inner->size() == 1, "inner size 1");
    check(std::static_pointer_cast<RESPInteger>(inner->elements()[0])->value() == 42, "inner val");
    return true;
}

// ---- Incomplete: missing data ----
static bool test_incomplete_bulk_string() {
    Buffer buf; RESPParser parser; std::shared_ptr<RESPObject> obj = nullptr;
    buf.append("$10\r\nhello", 9);  // body should be 10 bytes, only 5 given
    ParseResult r = parser.parse(&buf, &obj);
    check(r == ParseResult::Incomplete, "incomplete BS returns Incomplete");
    check(buf.readableBytes() == 9, "buffer untouched on Incomplete");
    return true;
}

// ---- Incomplete: no CRLF after identifier ----
static bool test_incomplete_no_crlf() {
    Buffer buf; RESPParser parser; std::shared_ptr<RESPObject> obj = nullptr;
    buf.append("+OK", 3);  // no \r\n yet
    ParseResult r = parser.parse(&buf, &obj);
    check(r == ParseResult::Incomplete, "no CRLF returns Incomplete");
    return true;
}

// ---- Pipeline: two commands in one buffer ----
static bool test_pipeline() {
    Buffer buf; RESPParser parser;
    buf.append("+OK\r\n:42\r\n", 10);

    std::shared_ptr<RESPObject> obj1, obj2;
    ParseResult r1 = parser.parse(&buf, &obj1);
    check(r1 == ParseResult::Success, "pipeline first success");
    check(obj1->type() == RESPType::SimpleString, "pipeline first type");

    ParseResult r2 = parser.parse(&buf, &obj2);
    check(r2 == ParseResult::Success, "pipeline second success");
    check(obj2->type() == RESPType::Integer, "pipeline second type");
    check(buf.readableBytes() == 0, "pipeline buffer consumed");
    return true;
}

// ---- Error: garbage prefix ----
static bool test_parse_garbage() {
    Buffer buf; RESPParser parser; std::shared_ptr<RESPObject> obj = nullptr;
    buf.append("!garbage\r\n", 11);
    ParseResult r = parser.parse(&buf, &obj);
    check(r == ParseResult::Error, "garbage prefix returns Error");
    return true;
}

// ---- Error: invalid bulk string length ----
static bool test_parse_invalid_bulk_length() {
    Buffer buf; RESPParser parser; std::shared_ptr<RESPObject> obj = nullptr;
    buf.append("$abc\r\n", 6);
    ParseResult r = parser.parse(&buf, &obj);
    check(r == ParseResult::Error, "invalid BS length returns Error");
    return true;
}

// ---- Parse null Array ----
static bool test_parse_null_array() {
    Buffer buf; RESPParser parser; std::shared_ptr<RESPObject> obj;
    ParseResult r = feedParse(&buf, &parser, "*-1\r\n", &obj);
    check(r == ParseResult::Success, "null Array parse success");
    check(std::static_pointer_cast<RESPArray>(obj)->isNull(), "null Array isNull");
    return true;
}

// ---- Parse empty Array ----
static bool test_parse_empty_array() {
    Buffer buf; RESPParser parser; std::shared_ptr<RESPObject> obj;
    ParseResult r = feedParse(&buf, &parser, "*0\r\n", &obj);
    check(r == ParseResult::Success, "empty Array parse success");
    check(std::static_pointer_cast<RESPArray>(obj)->size() == 0, "empty Array size 0");
    return true;
}

// ---- Parse error message with spaces ----
static bool test_parse_error_with_spaces() {
    Buffer buf; RESPParser parser; std::shared_ptr<RESPObject> obj;
    ParseResult r = feedParse(&buf, &parser, "-ERR wrong number of arguments\r\n", &obj);
    check(r == ParseResult::Success, "error with spaces parse");
    check(std::static_pointer_cast<RESPError>(obj)->message() == "ERR wrong number of arguments", "spaces preserved");
    return true;
}

int main() {
    test_parse_simple_string();
    test_parse_error();
    test_parse_integer();
    test_parse_negative_integer();
    test_parse_bulk_string();
    test_parse_null_bulk_string();
    test_parse_empty_bulk_string();
    test_parse_array();
    test_parse_nested_array();
    test_incomplete_bulk_string();
    test_incomplete_no_crlf();
    test_pipeline();
    test_parse_garbage();
    test_parse_invalid_bulk_length();
    test_parse_null_array();
    test_parse_empty_array();
    test_parse_error_with_spaces();

    if (g_failures == 0) {
        fprintf(stdout, "test_resp_parser: ALL TESTS PASSED\n");
        return 0;
    }
    fprintf(stderr, "test_resp_parser: %d FAILURE(S)\n", g_failures);
    return 1;
}
