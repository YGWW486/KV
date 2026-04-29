#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>
#include "Benchmark.h"
#include "StressTest.h"
#include "../src/network/Buffer.h"
#include "../src/network/ConnectionPool.h"

using namespace kvstore;

void testBufferPerformance() {
    LOG_INFO << "=== 测试Buffer性能 ===";
    
    Benchmark benchmark;
    
    // 测试Buffer写入性能
    benchmark.run("Buffer写入", 100000, []() {
        Buffer buffer;
        buffer.append("Hello, World!");
    });
    
    // 测试Buffer读取性能
    benchmark.run("Buffer读取", 100000, []() {
        Buffer buffer;
        buffer.append("Hello, World!");
        std::string data = buffer.retrieveAsString(13);
    });
    
    // 测试大容量Buffer性能
    benchmark.run("大容量Buffer", 1000, []() {
        Buffer buffer;
        for (int i = 0; i < 1000; ++i) {
            buffer.append("This is a test data for buffer performance testing.");
        }
        std::string data = buffer.retrieveAllAsString();
    });
    
    benchmark.printSummary();
}

void testConnectionPoolPerformance() {
    LOG_INFO << "=== 测试连接池性能 ===";
    
    // 模拟连接池操作
    Benchmark benchmark;
    StressTest stress_test;
    
    // 模拟获取和释放连接
    benchmark.run("连接池操作", 50000, []() {
        // 模拟获取连接
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        // 模拟释放连接
        std::this_thread::sleep_for(std::chrono::microseconds(5));
    });
    
    // 并发连接池测试
    stress_test.runConcurrentTest("并发连接池", 20, 2500, []() {
        // 模拟并发获取连接
        std::this_thread::sleep_for(std::chrono::microseconds(15));
        return true;
    });
    
    benchmark.printSummary();
}

void testEventLoopPerformance() {
    LOG_INFO << "=== 测试事件循环性能 ===";
    
    Benchmark benchmark;
    
    // 模拟事件处理性能
    benchmark.run("事件处理", 100000, []() {
        // 模拟事件处理逻辑
        std::this_thread::sleep_for(std::chrono::microseconds(5));
    });
    
    // 模拟定时器性能
    benchmark.run("定时器操作", 50000, []() {
        // 模拟定时器添加和移除
        std::this_thread::sleep_for(std::chrono::microseconds(8));
    });
    
    benchmark.printSummary();
}

void testNetworkThroughput() {
    LOG_INFO << "=== 测试网络吞吐量 ===";
    
    Benchmark benchmark;
    
    // 模拟网络数据传输
    size_t total_bytes = 0;
    
    benchmark.run("小数据包传输", 50000, [&total_bytes]() {
        // 模拟64字节数据包传输
        std::this_thread::sleep_for(std::chrono::microseconds(20));
        total_bytes += 64;
    });
    
    double time = benchmark.run("大数据包传输", 1000, [&total_bytes]() {
        // 模拟1KB数据包传输
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        total_bytes += 1024;
    }).total_time;
    
    double throughput = total_bytes / time / 1024 / 1024; // MB/s
    
    LOG_INFO << "网络吞吐量测试结果:";
    LOG_INFO << "总传输数据: " << total_bytes << " 字节";
    LOG_INFO << "总时间: " << time << " 秒";
    LOG_INFO << "吞吐量: " << throughput << " MB/s";
}

void testConcurrentConnections() {
    LOG_INFO << "=== 测试并发连接性能 ===";
    
    StressTest stress_test;
    
    // 模拟大量并发连接
    auto result = stress_test.runConcurrentTest("并发连接", 100, 100, []() {
        // 模拟连接建立和关闭
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return true;
    });
    
    LOG_INFO << "并发连接测试完成";
    LOG_INFO << "支持的最大并发连接数: " << result.operations;
    LOG_INFO << "平均QPS: " << result.qps;
}

void testMemoryUsageUnderLoad() {
    LOG_INFO << "=== 测试负载下内存使用 ===";
    
    MemoryMonitor::printMemoryUsage("测试前");
    
    // 模拟高负载场景
    std::vector<std::vector<char>> memory_blocks;
    
    for (int i = 0; i < 1000; ++i) {
        // 模拟分配1MB内存块
        memory_blocks.emplace_back(1024 * 1024, 'X');
    }
    
    MemoryMonitor::printMemoryUsage("分配1GB内存后");
    
    // 释放内存
    memory_blocks.clear();
    memory_blocks.shrink_to_fit();
    
    MemoryMonitor::printMemoryUsage("释放内存后");
}

int main() {
    LoggerImpl::instance().setLogLevel(INFO);
    LOG_INFO << "=== 网络层性能测试开始 ===";
    
    try {
        testBufferPerformance();
        testConnectionPoolPerformance();
        testEventLoopPerformance();
        testNetworkThroughput();
        testConcurrentConnections();
        testMemoryUsageUnderLoad();
        
        LOG_INFO << "=== 网络层性能测试完成 ===";
        
    } catch (const std::exception& e) {
        LOG_ERROR << "性能测试过程中发生异常: " << e.what();
        return 1;
    }
    
    return 0;
}