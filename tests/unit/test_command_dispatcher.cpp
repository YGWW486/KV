#include "../../src/commands/CommandDispatcher.h"
#include "../../src/storage/MemoryStorageEngine.h"
#include "../../src/storage/PersistenceEngine.h"
#include <cstdlib>
#include <cstdio>

using namespace kvstore;

static int g_failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) { ++g_failures; fprintf(stderr, "FAIL: %s\n", msg); }
}

// Helper: create a RESP Array from a list of string args
static std::shared_ptr<RESPObject> makeRequest(const std::vector<std::string>& args) {
    std::vector<std::shared_ptr<RESPObject>> elems;
    for (const auto& a : args) {
        elems.push_back(std::make_shared<RESPBulkString>(a));
    }
    return std::make_shared<RESPArray>(std::move(elems));
}

// Helper: get value from a RESP response pointer
static std::string respVal(const std::shared_ptr<RESPObject>& r) {
    switch (r->type()) {
    case RESPType::SimpleString: return static_cast<RESPSimpleString*>(r.get())->value();
    case RESPType::Error:        return static_cast<RESPError*>(r.get())->message();
    case RESPType::Integer:      return std::to_string(static_cast<RESPInteger*>(r.get())->value());
    case RESPType::BulkString:   {
        auto* bs = static_cast<RESPBulkString*>(r.get());
        return bs->isNull() ? "(nil)" : bs->value();
    }
    default: return "?";
    }
}

// ---- PING (no args) ----
static bool test_ping_no_arg() {
    MemoryStorageEngine storage;
    CommandDispatcher disp(&storage);
    auto resp = disp.dispatch(makeRequest({"PING"}));
    check(resp->type() == RESPType::SimpleString, "PING type");
    check(respVal(resp) == "PONG", "PING returns PONG");
    return true;
}

// ---- PING with arg ----
static bool test_ping_with_arg() {
    MemoryStorageEngine storage;
    CommandDispatcher disp(&storage);
    auto resp = disp.dispatch(makeRequest({"PING", "hello"}));
    check(resp->type() == RESPType::BulkString, "PING arg type");
    check(respVal(resp) == "hello", "PING arg returns arg");
    return true;
}

// ---- SET / GET ----
static bool test_set_get() {
    MemoryStorageEngine storage;
    CommandDispatcher disp(&storage);
    auto setResp = disp.dispatch(makeRequest({"SET", "name", "KV"}));
    check(respVal(setResp) == "OK", "SET returns OK");

    auto getResp = disp.dispatch(makeRequest({"GET", "name"}));
    check(respVal(getResp) == "KV", "GET returns value");

    auto getMissing = disp.dispatch(makeRequest({"GET", "no_such_key"}));
    check(respVal(getMissing) == "(nil)", "GET missing key returns nil");
    return true;
}

// ---- DEL ----
static bool test_del() {
    MemoryStorageEngine storage;
    CommandDispatcher disp(&storage);
    disp.dispatch(makeRequest({"SET", "a", "1"}));
    disp.dispatch(makeRequest({"SET", "b", "2"}));
    disp.dispatch(makeRequest({"SET", "c", "3"}));

    auto delResp = disp.dispatch(makeRequest({"DEL", "a", "c", "no_such"}));
    check(respVal(delResp) == "2", "DEL returns deleted count");

    check(respVal(disp.dispatch(makeRequest({"GET", "a"}))) == "(nil)", "a deleted");
    check(respVal(disp.dispatch(makeRequest({"GET", "b"}))) == "2", "b still exists");
    return true;
}

// ---- INCR / DECR ----
static bool test_incr_decr() {
    MemoryStorageEngine storage;
    CommandDispatcher disp(&storage);

    auto r1 = disp.dispatch(makeRequest({"INCR", "counter"}));
    check(respVal(r1) == "1", "INCR on missing key returns 1");

    auto r2 = disp.dispatch(makeRequest({"INCR", "counter"}));
    check(respVal(r2) == "2", "INCR returns 2");

    auto r3 = disp.dispatch(makeRequest({"DECR", "counter"}));
    check(respVal(r3) == "1", "DECR returns 1");

    return true;
}

// ---- INCR on non-integer ----
static bool test_incr_non_integer() {
    MemoryStorageEngine storage;
    CommandDispatcher disp(&storage);
    disp.dispatch(makeRequest({"SET", "str", "hello"}));
    auto resp = disp.dispatch(makeRequest({"INCR", "str"}));
    check(resp->type() == RESPType::Error, "INCR on string returns error");
    return true;
}

// ---- APPEND ----
static bool test_append() {
    MemoryStorageEngine storage;
    CommandDispatcher disp(&storage);
    disp.dispatch(makeRequest({"SET", "msg", "Hello"}));
    auto r = disp.dispatch(makeRequest({"APPEND", "msg", " World"}));
    check(respVal(r) == "11", "APPEND returns new length");
    check(respVal(disp.dispatch(makeRequest({"GET", "msg"}))) == "Hello World", "GET after APPEND");
    return true;
}

// ---- STRLEN ----
static bool test_strlen() {
    MemoryStorageEngine storage;
    CommandDispatcher disp(&storage);
    disp.dispatch(makeRequest({"SET", "k", "hello"}));
    check(respVal(disp.dispatch(makeRequest({"STRLEN", "k"}))) == "5", "STRLEN 5");
    check(respVal(disp.dispatch(makeRequest({"STRLEN", "missing"}))) == "0", "STRLEN missing 0");
    return true;
}

// ---- EXISTS ----
static bool test_exists() {
    MemoryStorageEngine storage;
    CommandDispatcher disp(&storage);
    disp.dispatch(makeRequest({"SET", "x", "1"}));
    disp.dispatch(makeRequest({"SET", "y", "2"}));
    check(respVal(disp.dispatch(makeRequest({"EXISTS", "x", "y", "z"}))) == "2", "EXISTS 2");
    check(respVal(disp.dispatch(makeRequest({"EXISTS", "z"}))) == "0", "EXISTS 0");
    return true;
}

// ---- LPUSH / RPUSH / LPOP / RPOP / LRANGE / LLEN ----
static bool test_list_commands() {
    MemoryStorageEngine storage;
    CommandDispatcher disp(&storage);

    disp.dispatch(makeRequest({"LPUSH", "mylist", "a"}));
    disp.dispatch(makeRequest({"RPUSH", "mylist", "b", "c"}));

    auto len = disp.dispatch(makeRequest({"LLEN", "mylist"}));
    check(respVal(len) == "3", "LLEN 3");

    auto lpop = disp.dispatch(makeRequest({"LPOP", "mylist"}));
    check(respVal(lpop) == "a", "LPOP returns a");

    auto rpop = disp.dispatch(makeRequest({"RPOP", "mylist"}));
    check(respVal(rpop) == "c", "RPOP returns c");

    // LRANGE has nested array response, just check type
    auto range = disp.dispatch(makeRequest({"LRANGE", "mylist", "0", "-1"}));
    check(range->type() == RESPType::Array, "LRANGE returns Array");
    return true;
}

// ---- HSET / HGET / HDEL / HGETALL / HKEYS / HLEN ----
static bool test_hash_commands() {
    MemoryStorageEngine storage;
    CommandDispatcher disp(&storage);

    auto hset = disp.dispatch(makeRequest({"HSET", "user", "name", "Tom", "age", "25"}));
    check(respVal(hset) == "2", "HSET returned 2 new fields");

    check(respVal(disp.dispatch(makeRequest({"HGET", "user", "name"}))) == "Tom", "HGET name");
    check(respVal(disp.dispatch(makeRequest({"HLEN", "user"}))) == "2", "HLEN 2");

    auto hkeys = disp.dispatch(makeRequest({"HKEYS", "user"}));
    check(hkeys->type() == RESPType::Array, "HKEYS returns Array");

    auto hgetall = disp.dispatch(makeRequest({"HGETALL", "user"}));
    check(hgetall->type() == RESPType::Array, "HGETALL returns Array");

    disp.dispatch(makeRequest({"HDEL", "user", "age"}));
    check(respVal(disp.dispatch(makeRequest({"HGET", "user", "age"}))) == "(nil)", "HDEL age removed");
    return true;
}

// ---- SADD / SREM / SMEMBERS / SISMEMBER / SCARD ----
static bool test_set_commands() {
    MemoryStorageEngine storage;
    CommandDispatcher disp(&storage);

    auto sadd = disp.dispatch(makeRequest({"SADD", "tags", "a", "b", "c"}));
    check(respVal(sadd) == "3", "SADD 3");

    // re-add same
    auto sadd2 = disp.dispatch(makeRequest({"SADD", "tags", "a"}));
    check(respVal(sadd2) == "0", "SADD duplicate returns 0");

    check(respVal(disp.dispatch(makeRequest({"SISMEMBER", "tags", "b"}))) == "1", "SISMEMBER true");
    check(respVal(disp.dispatch(makeRequest({"SISMEMBER", "tags", "z"}))) == "0", "SISMEMBER false");
    check(respVal(disp.dispatch(makeRequest({"SCARD", "tags"}))) == "3", "SCARD 3");

    disp.dispatch(makeRequest({"SREM", "tags", "a", "z"}));
    check(respVal(disp.dispatch(makeRequest({"SCARD", "tags"}))) == "2", "SCARD after SREM");

    auto members = disp.dispatch(makeRequest({"SMEMBERS", "tags"}));
    check(members->type() == RESPType::Array, "SMEMBERS returns Array");
    return true;
}

// ---- ZADD / ZRANGE / ZSCORE / ZRANK / ZCARD / ZREM ----
static bool test_zset_commands() {
    MemoryStorageEngine storage;
    CommandDispatcher disp(&storage);

    auto zadd = disp.dispatch(makeRequest({"ZADD", "score", "10", "Alice", "20", "Bob", "15", "Charlie"}));
    check(respVal(zadd) == "3", "ZADD 3");

    check(respVal(disp.dispatch(makeRequest({"ZCARD", "score"}))) == "3", "ZCARD 3");

    auto range = disp.dispatch(makeRequest({"ZRANGE", "score", "0", "-1"}));
    check(range->type() == RESPType::Array, "ZRANGE returns Array");

    check(respVal(disp.dispatch(makeRequest({"ZSCORE", "score", "Bob"}))) == "20.000000", "ZSCORE Bob");

    auto rank = disp.dispatch(makeRequest({"ZRANK", "score", "Bob"}));
    check(respVal(rank) == "2", "ZRANK Bob is 2");

    disp.dispatch(makeRequest({"ZREM", "score", "Charlie"}));
    check(respVal(disp.dispatch(makeRequest({"ZCARD", "score"}))) == "2", "ZCARD after ZREM");
    return true;
}

// ---- Unknown command ----
static bool test_unknown_command() {
    MemoryStorageEngine storage;
    CommandDispatcher disp(&storage);
    auto resp = disp.dispatch(makeRequest({"FOOBAR"}));
    check(resp->type() == RESPType::Error, "unknown command returns error");
    return true;
}

// ---- Wrong argument count ----
static bool test_wrong_arg_count() {
    MemoryStorageEngine storage;
    CommandDispatcher disp(&storage);
    auto resp = disp.dispatch(makeRequest({"SET"}));  // no value
    check(resp->type() == RESPType::Error, "SET with no args returns error");
    check(respVal(resp).find("ERR") == 0, "error starts with ERR");
    return true;
}

// ---- INCR / DECR via AOF persistence ----
static bool test_dispatcher_with_aof() {
    AOFPersistenceEngine aof("_test_dispatcher.aof");
    MemoryStorageEngine storage;
    CommandDispatcher disp(&storage, &aof);

    disp.dispatch(makeRequest({"SET", "hello", "world"}));
    check(respVal(disp.dispatch(makeRequest({"GET", "hello"}))) == "world", "AOF-dispatcher SET/GET");

    disp.dispatch(makeRequest({"DEL", "hello"}));
    check(respVal(disp.dispatch(makeRequest({"GET", "hello"}))) == "(nil)", "AOF-dispatcher DEL");

    aof.save(&storage);
    std::remove("_test_dispatcher.aof");
    return true;
}

int main() {
    test_ping_no_arg();
    test_ping_with_arg();
    test_set_get();
    test_del();
    test_incr_decr();
    test_incr_non_integer();
    test_append();
    test_strlen();
    test_exists();
    test_list_commands();
    test_hash_commands();
    test_set_commands();
    test_zset_commands();
    test_unknown_command();
    test_wrong_arg_count();
    test_dispatcher_with_aof();

    if (g_failures == 0) {
        fprintf(stdout, "test_command_dispatcher: ALL TESTS PASSED\n");
        return 0;
    }
    fprintf(stderr, "test_command_dispatcher: %d FAILURE(S)\n", g_failures);
    return 1;
}
