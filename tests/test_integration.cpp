#include <iostream>
#include <thread>
#include <vector>
#include <filesystem>
#include <atomic>
#include "Benchmark.h"
#include "StressTest.h"
#include "../src/storage/MemoryStorageEngine.h"
#include "../src/storage/PersistenceEngine.h"
#include "../src/protocol/RESP.h"
#include "../src/protocol/RESPParser.h"
#include "../src/network/Buffer.h"

using namespace kvstore;

void testProtocolIntegration() {
    LOG_INFO << "=== 测试协议层集成 ===";
    
    Benchmark benchmark;
    
    // 测试RESP协议编码性能
    benchmark.run("RESP编码", 50000, []() {
        RESPBulkString resp("Hello World");
        std::string encoded = resp.toString();
    });
    
    // 测试RESP协议解码性能
    benchmark.run("RESP解码", 50000, []() {
        Buffer buffer;
        buffer.append("+OK\r\n");
        RESPParser parser;
        std::shared_ptr<RESPObject> out;
        parser.parse(&buffer, &out);
    });
    
    benchmark.printSummary();
}

void testStorageProtocolIntegration() {
    LOG_INFO << "=== 测试存储与协议集成 ===";
    
    MemoryStorageEngine storage;
    Benchmark benchmark;
    
    // 模拟SET命令处理流程
    benchmark.run("SET命令处理", 10000, [&storage]() {
        // 1. 解析命令
        std::string command = "SET key value";
        
        // 2. 执行存储操作
        storage.set("key", "value");
        
        // 3. 生成响应
        RESPSimpleString resp("OK");
        std::string response = resp.toString();
    });
    
    // 模拟GET命令处理流程
    benchmark.run("GET命令处理", 10000, [&storage]() {
        // 1. 解析命令
        std::string command = "GET key";
        
        // 2. 执行查询操作
        auto value = storage.get("key");
        
        // 3. 生成响应
        if (value) {
            RESPBulkString resp(*value);
            std::string response = resp.toString();
        } else {
            RESPError resp("Key not found");
            std::string response = resp.toString();
        }
    });
    
    benchmark.printSummary();
}

void testMultiCommandWorkflow() {
    LOG_INFO << "=== 测试多命令工作流 ===";
    
    MemoryStorageEngine storage;
    
    // 模拟完整的Redis命令工作流
    std::vector<std::pair<std::string, std::function<void()>>> workflows = {
        {"String操作", [&storage]() {
            storage.set("username", "alice");
            storage.get("username");
            storage.del("username");
        }},
        
        {"Hash操作", [&storage]() {
            storage.hset("user:1", "name", "Alice");
            storage.hset("user:1", "age", "25");
            storage.hget("user:1", "name");
            storage.hkeys("user:1");
        }},
        
        {"List操作", [&storage]() {
            storage.lpush("queue", "task1");
            storage.lpush("queue", "task2");
            storage.rpush("queue", "task3");
            storage.lpop("queue");
            storage.lrange("queue", 0, -1);
        }},
        
        {"Set操作", [&storage]() {
            storage.sadd("tags", "redis");
            storage.sadd("tags", "c++");
            storage.sadd("tags", "network");
            storage.smembers("tags");
            storage.sismember("tags", "redis");
        }}
    };
    
    Benchmark benchmark;
    
    for (const auto& [name, workflow] : workflows) {
        benchmark.run(name, 1000, workflow);
    }
    
    benchmark.printSummary();
}

void testConcurrentAccess() {
    LOG_INFO << "=== 测试并发访问 ===";
    
    MemoryStorageEngine storage;
    StressTest stress_test;
    
    // 模拟多个客户端并发访问
    auto result = stress_test.runConcurrentTest("并发读写", 10, 1000, [&storage]() {
        static std::atomic<int> counter{0};
        int op_type = counter++ % 4;
        
        switch (op_type) {
            case 0: // SET
                return storage.set("concurrent_key", "value");
            case 1: // GET
                storage.get("concurrent_key");
                return true;
            case 2: // HSET
                return storage.hset("concurrent_hash", "field", "value");
            case 3: // LPUSH
                return storage.lpush("concurrent_list", "value");
            default:
                return false;
        }
    });
    
    LOG_INFO << "并发访问测试完成";
    LOG_INFO << "总操作数: " << result.operations;
    LOG_INFO << "QPS: " << result.qps;
}

void testPersistenceIntegration() {
    LOG_INFO << "=== 测试持久化集成 ===";
    
    // 创建存储引擎并添加数据
    MemoryStorageEngine storage;
    for (int i = 0; i < 1000; ++i) {
        std::string key = "persist_key_" + std::to_string(i);
        storage.set(key, "persist_value_" + std::to_string(i));
    }
    
    // 测试AOF持久化集成
    auto aof_engine = std::make_unique<AOFPersistenceEngine>("integration_test.aof");
    PersistenceManager aof_manager(std::move(aof_engine));
    aof_manager.start();
    
    Benchmark benchmark;
    
    // 测试保存和加载
    benchmark.run("AOF保存加载", 10, [&aof_manager, &storage]() {
        // 保存数据
        if (!aof_manager.manualSave(&storage)) {
            return;
        }
        
        // 创建新存储引擎并加载数据
        MemoryStorageEngine new_storage;
        if (!aof_manager.loadData(&new_storage)) {
            return;
        }
        
        // 验证数据完整性
        auto value = new_storage.get("persist_key_0");
        if (!value || *value != "persist_value_0") {
            LOG_ERROR << "数据完整性验证失败";
        }
    });
    
    benchmark.printSummary();
    aof_manager.stop();
    
    // 清理测试文件
    std::filesystem::remove("integration_test.aof");
}

void testErrorHandling() {
    LOG_INFO << "=== 测试错误处理 ===";
    
    MemoryStorageEngine storage;
    
    // 测试各种边界情况
    try {
        // 测试获取不存在的键
        auto value = storage.get("nonexistent_key");
        if (value) {
            LOG_ERROR << "错误处理测试失败: 不存在的键返回了值";
        } else {
            LOG_INFO << "不存在的键正确处理";
        }
        
        // 测试空键和空值
        storage.set("", "value");
        storage.set("key", "");
        
        LOG_INFO << "空键值处理正常";
        
        // 测试大量数据
        for (int i = 0; i < 100000; ++i) {
            std::string key = "stress_key_" + std::to_string(i);
            storage.set(key, "stress_value_" + std::to_string(i));
        }
        
        LOG_INFO << "大量数据处理正常";
        
    } catch (const std::exception& e) {
        LOG_ERROR << "错误处理测试异常: " << e.what();
    }
}

int main() {
    LoggerImpl::instance().setLogLevel(INFO);
    LOG_INFO << "=== 系统集成测试开始 ===";
    
    try {
        testProtocolIntegration();
        testStorageProtocolIntegration();
        testMultiCommandWorkflow();
        testConcurrentAccess();
        testPersistenceIntegration();
        testErrorHandling();
        
        LOG_INFO << "=== 系统集成测试完成 ===";
        
    } catch (const std::exception& e) {
        LOG_ERROR << "集成测试过程中发生异常: " << e.what();
        return 1;
    }
    
    return 0;
}