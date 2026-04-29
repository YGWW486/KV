#include <iostream>
#include <string>
#include <random>
#include <filesystem>
#include <atomic>
#include "Benchmark.h"
#include "StressTest.h"
#include "../src/storage/MemoryStorageEngine.h"
#include "../src/storage/PersistenceEngine.h"

using namespace kvstore;

void testStringPerformance() {
    LOG_INFO << "=== 测试String操作性能 ===";
    
    MemoryStorageEngine storage;
    Benchmark benchmark;
    
    // 测试SET操作性能
    benchmark.run("SET操作", 100000, [&storage]() {
        static size_t counter = 0;
        std::string key = "key_" + std::to_string(counter++);
        storage.set(key, "value");
    });
    
    // 测试GET操作性能
    benchmark.run("GET操作", 100000, [&storage]() {
        static size_t counter = 0;
        std::string key = "key_" + std::to_string(counter++ % 100000);
        storage.get(key);
    });
    
    // 测试并发SET操作
    StressTest stress_test;
    stress_test.runConcurrentTest("并发SET", 10, 10000, [&storage]() {
        static std::atomic<size_t> counter{0};
        std::string key = "concurrent_key_" + std::to_string(counter++);
        return storage.set(key, "concurrent_value");
    });
    
    benchmark.printSummary();
}

void testHashPerformance() {
    LOG_INFO << "=== 测试Hash操作性能 ===";
    
    MemoryStorageEngine storage;
    Benchmark benchmark;
    
    // 测试HSET操作性能
    benchmark.run("HSET操作", 50000, [&storage]() {
        static size_t counter = 0;
        std::string key = "hash_" + std::to_string(counter / 100);
        std::string field = "field_" + std::to_string(counter % 100);
        storage.hset(key, field, "value");
        counter++;
    });
    
    // 测试HGET操作性能
    benchmark.run("HGET操作", 50000, [&storage]() {
        static size_t counter = 0;
        std::string key = "hash_" + std::to_string(counter / 100);
        std::string field = "field_" + std::to_string(counter % 100);
        storage.hget(key, field);
        counter++;
    });
    
    benchmark.printSummary();
}

void testListPerformance() {
    LOG_INFO << "=== 测试List操作性能 ===";
    
    MemoryStorageEngine storage;
    Benchmark benchmark;
    
    // 测试LPUSH操作性能
    benchmark.run("LPUSH操作", 50000, [&storage]() {
        static size_t counter = 0;
        std::string key = "list_" + std::to_string(counter / 1000);
        storage.lpush(key, "value_" + std::to_string(counter));
        counter++;
    });
    
    // 测试LPOP操作性能
    benchmark.run("LPOP操作", 50000, [&storage]() {
        static size_t counter = 0;
        std::string key = "list_" + std::to_string(counter / 1000);
        storage.lpop(key);
        counter++;
    });
    
    benchmark.printSummary();
}

void testMemoryUsage() {
    LOG_INFO << "=== 测试内存使用情况 ===";
    
    MemoryStorageEngine storage;
    
    // 记录初始内存使用
    MemoryMonitor::printMemoryUsage("初始状态");
    
    // 添加大量数据
    for (int i = 0; i < 100000; ++i) {
        std::string key = "mem_key_" + std::to_string(i);
        std::string value = "mem_value_" + std::to_string(i);
        storage.set(key, value);
    }
    
    // 记录添加数据后的内存使用
    MemoryMonitor::printMemoryUsage("添加100,000个键值对后");
    
    // 清空数据
    storage.flushall();
    
    // 记录清空后的内存使用
    MemoryMonitor::printMemoryUsage("清空数据后");
}

void testPersistencePerformance() {
    LOG_INFO << "=== 测试持久化性能 ===";
    
    MemoryStorageEngine storage;
    Benchmark benchmark;
    
    // 准备测试数据
    for (int i = 0; i < 10000; ++i) {
        std::string key = "persist_key_" + std::to_string(i);
        storage.set(key, "persist_value_" + std::to_string(i));
    }
    
    // 测试AOF保存性能
    auto aof_engine = std::make_unique<AOFPersistenceEngine>("perf_test.aof");
    PersistenceManager aof_manager(std::move(aof_engine));
    
    benchmark.run("AOF保存", 10, [&aof_manager, &storage]() {
        return aof_manager.manualSave(&storage);
    });
    
    // 测试RDB保存性能
    auto rdb_engine = std::make_unique<RDBPersistenceEngine>("perf_test.rdb");
    PersistenceManager rdb_manager(std::move(rdb_engine));
    
    benchmark.run("RDB保存", 10, [&rdb_manager, &storage]() {
        return rdb_manager.manualSave(&storage);
    });
    
    benchmark.printSummary();
    
    // 清理测试文件
    std::filesystem::remove("perf_test.aof");
    std::filesystem::remove("perf_test.rdb");
}

int main() {
    LoggerImpl::instance().setLogLevel(INFO);
    LOG_INFO << "=== 存储层性能测试开始 ===";
    
    try {
        testStringPerformance();
        testHashPerformance();
        testListPerformance();
        testMemoryUsage();
        testPersistencePerformance();
        
        LOG_INFO << "=== 存储层性能测试完成 ===";
        
    } catch (const std::exception& e) {
        LOG_ERROR << "性能测试过程中发生异常: " << e.what();
        return 1;
    }
    
    return 0;
}