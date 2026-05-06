#include "../../src/storage/MemoryStorageEngine.h"
#include "../../src/storage/PersistenceEngine.h"
#include "../../src/commands/CommandDispatcher.h"
#include "../../src/protocol/RESPParser.h"
#include "../../src/network/Buffer.h"
#include <cstdlib>
#include <cstdio>

using namespace kvstore;

static int g_failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) { ++g_failures; fprintf(stderr, "FAIL: %s\n", msg); }
}

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

static std::shared_ptr<RESPObject> makeRequest(const std::vector<std::string>& args) {
    std::vector<std::shared_ptr<RESPObject>> elems;
    for (const auto& a : args) {
        elems.push_back(std::make_shared<RESPBulkString>(a));
    }
    return std::make_shared<RESPArray>(std::move(elems));
}

static void populateData(MemoryStorageEngine* storage) {
    CommandDispatcher disp(storage);
    disp.dispatch(makeRequest({"SET", "s:greeting", "Hello, KVStore!"}));
    disp.dispatch(makeRequest({"SET", "s:version", "0.1.0"}));
    disp.dispatch(makeRequest({"SET", "s:counter", "42"}));
    disp.dispatch(makeRequest({"HSET", "h:user:1", "name", "Alice", "age", "30", "city", "NYC"}));
    disp.dispatch(makeRequest({"HSET", "h:user:2", "name", "Bob", "role", "admin"}));
    disp.dispatch(makeRequest({"RPUSH", "l:tasks", "task1", "task2", "task3"}));
    disp.dispatch(makeRequest({"LPUSH", "l:queue", "first", "second"}));
    disp.dispatch(makeRequest({"SADD", "set:tags", "cpp", "redis", "database", "kv-store"}));
    disp.dispatch(makeRequest({"SADD", "set:colors", "red", "green", "blue"}));
    disp.dispatch(makeRequest({"ZADD", "z:leaderboard", "100", "Alice", "85", "Bob", "95", "Charlie"}));
}

static void verifyData(MemoryStorageEngine* storage) {
    CommandDispatcher disp(storage);
    check(respVal(disp.dispatch(makeRequest({"GET", "s:greeting"}))) == "Hello, KVStore!", "verify s:greeting");
    check(respVal(disp.dispatch(makeRequest({"GET", "s:version"}))) == "0.1.0", "verify s:version");
    check(respVal(disp.dispatch(makeRequest({"GET", "s:counter"}))) == "42", "verify s:counter");
    check(respVal(disp.dispatch(makeRequest({"HGET", "h:user:1", "name"}))) == "Alice", "verify h:user:1 name");
    check(respVal(disp.dispatch(makeRequest({"HLEN", "h:user:1"}))) == "3", "verify h:user:1 len");
    check(respVal(disp.dispatch(makeRequest({"LLEN", "l:tasks"}))) == "3", "verify l:tasks len");
    check(respVal(disp.dispatch(makeRequest({"SCARD", "set:tags"}))) == "4", "verify set:tags card");
    check(respVal(disp.dispatch(makeRequest({"SISMEMBER", "set:colors", "red"}))) == "1", "verify set:colors red");
    check(respVal(disp.dispatch(makeRequest({"ZCARD", "z:leaderboard"}))) == "3", "verify z:leaderboard card");
}

// ---- RDB save → load roundtrip ----
static bool test_rdb_roundtrip() {
    const char* rdb_file = "_test_rt_rdb.rdb";

    // Save
    {
        MemoryStorageEngine storage1;
        populateData(&storage1);
        RDBPersistenceEngine rdb(rdb_file);
        bool ok = rdb.save(&storage1);
        check(ok, "RDB save returns true");
    }

    // Load into fresh storage (separate scope: save engine destroyed)
    {
        MemoryStorageEngine storage2;
        RDBPersistenceEngine rdb(rdb_file);
        bool ok = rdb.load(&storage2);
        check(ok, "RDB load returns true");
        verifyData(&storage2);
    }

    std::remove(rdb_file);
    return true;
}

// ---- AOF save → load roundtrip (using recordWrite to simulate writes) ----
// NOTE: The AOF file format uses space-separated tokens, so values must not
// contain spaces. This is a known limitation of the current AOF implementation.
static bool test_aof_roundtrip() {
    const char* aof_file = "_test_rt_aof.aof";

    {
        MemoryStorageEngine storage;
        AOFPersistenceEngine aof(aof_file);
        CommandDispatcher disp(&storage, &aof);
        // Use values without spaces (AOF parser splits on whitespace)
        disp.dispatch(makeRequest({"SET", "s:greeting", "HelloKV"}));
        disp.dispatch(makeRequest({"SET", "s:version", "0.1.0"}));
        disp.dispatch(makeRequest({"SET", "s:counter", "42"}));
        disp.dispatch(makeRequest({"HSET", "h:user:1", "name", "Alice", "age", "30", "city", "NYC"}));
        disp.dispatch(makeRequest({"SADD", "set:tags", "cpp", "redis", "database"}));
        disp.dispatch(makeRequest({"ZADD", "z:leaderboard", "100", "Alice", "85", "Bob"}));
        aof.save(&storage);
        aof.syncAOF();
    }

    // Load
    {
        MemoryStorageEngine storage;
        AOFPersistenceEngine aof(aof_file);
        bool ok = aof.load(&storage);
        check(ok, "AOF load returns true");

        // Spot-check key items (all values are space-free)
        CommandDispatcher disp(&storage);
        check(respVal(disp.dispatch(makeRequest({"GET", "s:greeting"}))) == "HelloKV",
              "AOF rt s:greeting");
        check(respVal(disp.dispatch(makeRequest({"GET", "s:version"}))) == "0.1.0",
              "AOF rt s:version");
        check(respVal(disp.dispatch(makeRequest({"HGET", "h:user:1", "name"}))) == "Alice",
              "AOF rt h:user:1 name");
        check(respVal(disp.dispatch(makeRequest({"SISMEMBER", "set:tags", "cpp"}))) == "1",
              "AOF rt set:tags cpp");
        check(respVal(disp.dispatch(makeRequest({"ZCARD", "z:leaderboard"}))) == "2",
              "AOF rt z:leaderboard");
    }

    std::remove(aof_file);
    return true;
}

// ---- PersistenceManager lifecycle ----
static bool test_persistence_manager() {
    const char* rdb_file = "_test_pm.rdb";

    {
        auto engine = std::make_unique<RDBPersistenceEngine>(rdb_file);
        PersistenceManager pm(std::move(engine));

        // Status should be available
        std::string status = pm.getStatus();
        check(!status.empty(), "PM getStatus returns non-empty");

        // Start before manual save
        bool started = pm.start();
        check(started, "PM start succeeds");

        MemoryStorageEngine storage;
        populateData(&storage);

        bool saved = pm.manualSave(&storage);
        check(saved, "PM manualSave succeeds");

        // Load into fresh storage
        MemoryStorageEngine storage2;
        bool loaded = pm.loadData(&storage2);
        check(loaded, "PM loadData succeeds");
        check(respVal(CommandDispatcher(&storage2).dispatch(makeRequest({"GET", "s:version"}))) == "0.1.0",
              "PM data ok");

        pm.stop();
    }

    std::remove(rdb_file);
    return true;
}

// ---- Empty storage persistence ----
static bool test_empty_persistence() {
    const char* aof_file = "_test_empty.aof";

    {
        AOFPersistenceEngine aof(aof_file);
        MemoryStorageEngine empty;
        aof.save(&empty);
        aof.syncAOF();
    }

    {
        MemoryStorageEngine empty;
        AOFPersistenceEngine aof(aof_file);
        bool ok = aof.load(&empty);
        check(ok, "empty AOF load succeeds");
    }

    std::remove(aof_file);
    return true;
}

int main() {
    test_rdb_roundtrip();
    test_aof_roundtrip();
    test_persistence_manager();
    test_empty_persistence();

    if (g_failures == 0) {
        fprintf(stdout, "test_persistence_roundtrip: ALL TESTS PASSED\n");
        return 0;
    }
    fprintf(stderr, "test_persistence_roundtrip: %d FAILURE(S)\n", g_failures);
    return 1;
}
