#include "../src/storage/MemoryStorageEngine.h"
#include "../src/storage/SegmentedMemoryStorageEngine.h"
#include "Benchmark.h"
#include "StressTest.h"
#include <thread>
#include <vector>
#include <atomic>
#include <cstdio>

using namespace kvstore;

// ---- Correctness tests on SegmentedMemoryStorageEngine ----

static int g_failures = 0;
static void check(bool cond, const char* msg) {
    if (!cond) { ++g_failures; fprintf(stderr, "FAIL: %s\n", msg); }
}

static bool test_basic_crud() {
    SegmentedMemoryStorageEngine s;
    check(s.set("name", "KVStore"), "set ok");
    auto v = s.get("name");
    check(v.has_value() && *v == "KVStore", "get matches");
    check(!s.get("no_such_key").has_value(), "get missing nullopt");
    check(s.exists("name"), "exists true");
    check(!s.exists("no_such_key"), "exists false");
    check(s.del("name"), "del ok");
    check(!s.exists("name"), "exists false after del");
    return true;
}

static bool test_type_switching() {
    SegmentedMemoryStorageEngine s;
    s.set("k", "string_val");
    check(s.getType("k") == KeyType::String, "type is string");
    s.hset("k", "f", "hash_val");
    check(s.getType("k") == KeyType::Hash, "type switched to hash");
    check(!s.get("k").has_value(), "old string value gone");
    auto hv = s.hget("k", "f");
    check(hv.has_value() && *hv == "hash_val", "hash value ok");
    return true;
}

static bool test_all_types() {
    SegmentedMemoryStorageEngine s;
    // String
    s.set("s", "v"); check(s.get("s") == "v", "string ok");
    // Hash
    s.hset("h", "f1", "v1"); s.hset("h", "f2", "v2");
    check(s.hlen("h") == 2, "hash len");
    check(s.hexists("h", "f1"), "hexists ok");
    auto hkeys = s.hkeys("h");
    check(hkeys.size() == 2, "hkeys size");
    // List
    s.rpush("l", "a"); s.rpush("l", "b"); s.lpush("l", "c");
    check(s.llen("l") == 3, "list len");
    check(s.lpop("l") == "c", "lpop c");
    check(s.lrange("l", 0, -1).size() == 2, "lrange size");
    // Set
    s.sadd("set", "x"); s.sadd("set", "y"); s.sadd("set", "x");
    check(s.scard("set") == 2, "set card");
    check(s.sismember("set", "x"), "sismember ok");
    check(!s.sismember("set", "z"), "sismember false");
    // ZSet
    s.zadd("z", 10, "Alice"); s.zadd("z", 20, "Bob");
    check(s.zcard("z") == 2, "zset card");
    auto zr = s.zrange("z", 0, -1);
    check(zr.size() == 2 && zr[0].second == "Alice", "zrange ok");
    return true;
}

static bool test_keys_and_flushall() {
    SegmentedMemoryStorageEngine s;
    s.set("user:1", "a");
    s.set("user:2", "b");
    s.set("order:1", "c");
    auto k1 = s.keys("user:*");
    check(k1.size() == 2, "keys user:*");
    auto k2 = s.keys("*");
    check(k2.size() == 3, "keys *");
    s.flushall();
    auto k3 = s.keys("*");
    check(k3.size() == 0, "keys empty after flushall");
    return true;
}

static bool test_segment_count_power_of_2() {
    SegmentedMemoryStorageEngine s1(7);
    check(s1.segmentCount() == 8, "7 rounded to 8");
    SegmentedMemoryStorageEngine s2(16);
    check(s2.segmentCount() == 16, "16 stays 16");
    SegmentedMemoryStorageEngine s3(1);
    check(s3.segmentCount() == 1, "1 stays 1");
    return true;
}

// ---- Performance comparison ----

static void benchmarkBoth(const char* label, int ops,
                          std::function<void(StorageEngine&)> fn) {
    // Warmup
    {
        MemoryStorageEngine m;
        fn(m);
    }
    {
        SegmentedMemoryStorageEngine s;
        fn(s);
    }

    Benchmark bench;
    {
        MemoryStorageEngine m;
        bench.run(std::string(label) + " (Original)", ops, [&]() { fn(m); });
    }
    {
        SegmentedMemoryStorageEngine s;
        bench.run(std::string(label) + " (Segmented)", ops, [&]() { fn(s); });
    }
    bench.printSummary();
}

static void testSingleThreadedComparison() {
    LOG_INFO << "=== Single-Threaded SET (disjoint keys) ===";
    std::atomic<int> counter{0};
    benchmarkBoth("SET", 500000, [&](StorageEngine& s) {
        int n = counter++;
        s.set("key:" + std::to_string(n), "value");
    });

    LOG_INFO << "";
    LOG_INFO << "=== Single-Threaded GET (50K keys preloaded) ===";
    {
        MemoryStorageEngine m;
        SegmentedMemoryStorageEngine seg;
        for (int i = 0; i < 50000; ++i) {
            auto k = "key:" + std::to_string(i);
            m.set(k, "v");
            seg.set(k, "v");
        }
        std::atomic<int> idx{0};
        benchmarkBoth("GET", 500000, [&](StorageEngine& s) {
            int i = idx++ % 50000;
            s.get("key:" + std::to_string(i));
        });
    }
}

static void testConcurrentScaling() {
    LOG_INFO << "=== Concurrent SET Scaling (disjoint keys) ===";

    for (int threads : {1, 2, 4, 8, 16}) {
        fprintf(stdout, "\n--- %d threads ---\n", threads);
        {
            MemoryStorageEngine m;
            StressTest st;
            std::atomic<int> counter{0};
            auto result = st.runConcurrentTest("Original " + std::to_string(threads) + "t",
                threads, 20000, [&]() {
                    int n = counter++;
                    m.set("seq:" + std::to_string(n), "val");
                    return true;
                });
            fprintf(stdout, "  Original:  %d ops, %.0f QPS\n",
                    (int)result.operations, result.qps);
        }
        {
            SegmentedMemoryStorageEngine s;
            StressTest st;
            std::atomic<int> counter{0};
            auto result = st.runConcurrentTest("Segmented " + std::to_string(threads) + "t",
                threads, 20000, [&]() {
                    int n = counter++;
                    s.set("seq:" + std::to_string(n), "val");
                    return true;
                });
            fprintf(stdout, "  Segmented: %d ops, %.0f QPS\n",
                    (int)result.operations, result.qps);
        }
    }
}

static void testConcurrentMixedReadWrite() {
    LOG_INFO << "=== Concurrent Mixed 80R/20W ===";
    // Preload data
    const int kKeys = 10000;

    for (int threads : {4, 8, 16}) {
        fprintf(stdout, "\n--- %d threads ---\n", threads);
        {
            MemoryStorageEngine m;
            for (int i = 0; i < kKeys; ++i)
                m.set("mk:" + std::to_string(i), "v");
            StressTest st;
            std::atomic<int> counter{0};
            auto result = st.runConcurrentTest("Original Mix " + std::to_string(threads) + "t",
                threads, 10000, [&]() {
                    int n = counter++ % kKeys;
                    if (n % 5 == 0)
                        m.set("mk:" + std::to_string(n), "newval");
                    else
                        m.get("mk:" + std::to_string(n));
                    return true;
                });
            fprintf(stdout, "  Original:  %.0f QPS\n", result.qps);
        }
        {
            SegmentedMemoryStorageEngine s;
            for (int i = 0; i < kKeys; ++i)
                s.set("mk:" + std::to_string(i), "v");
            StressTest st;
            std::atomic<int> counter{0};
            auto result = st.runConcurrentTest("Segmented Mix " + std::to_string(threads) + "t",
                threads, 10000, [&]() {
                    int n = counter++ % kKeys;
                    if (n % 5 == 0)
                        s.set("mk:" + std::to_string(n), "newval");
                    else
                        s.get("mk:" + std::to_string(n));
                    return true;
                });
            fprintf(stdout, "  Segmented: %.0f QPS\n", result.qps);
        }
    }
}

int main() {
    LoggerImpl::instance().setLogLevel(INFO);

    LOG_INFO << "================================================";
    LOG_INFO << "  Correctness Tests";
    LOG_INFO << "================================================";

    test_basic_crud();
    test_type_switching();
    test_all_types();
    test_keys_and_flushall();
    test_segment_count_power_of_2();

    if (g_failures == 0)
        fprintf(stdout, "Correctness: ALL PASSED\n\n");
    else {
        fprintf(stderr, "Correctness: %d FAILURES\n", g_failures);
        return 1;
    }

    LOG_INFO << "================================================";
    LOG_INFO << "  Single-Threaded Comparison";
    LOG_INFO << "================================================";
    testSingleThreadedComparison();

    LOG_INFO << "";
    LOG_INFO << "================================================";
    LOG_INFO << "  Concurrent Scaling (SET on disjoint keys)";
    LOG_INFO << "================================================";
    testConcurrentScaling();

    LOG_INFO << "";
    LOG_INFO << "================================================";
    LOG_INFO << "  Concurrent Mixed Read/Write (80% GET, 20% SET)";
    LOG_INFO << "================================================";
    testConcurrentMixedReadWrite();

    LOG_INFO << "";
    LOG_INFO << "All benchmarks complete.";
    return 0;
}
