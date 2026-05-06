#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>
#include "Benchmark.h"
#include "StressTest.h"
#include "../src/network/Buffer.h"

using namespace kvstore;

void testBufferPerformance() {
    LOG_INFO << "=== Testing Buffer Performance ===";

    Benchmark benchmark;

    benchmark.run("Buffer write (13 bytes)", 100000, []() {
        Buffer buffer;
        buffer.append("Hello, World!");
    });

    benchmark.run("Buffer write+read (13 bytes)", 100000, []() {
        Buffer buffer;
        buffer.append("Hello, World!");
        std::string data = buffer.retrieveAsString(13);
    });

    benchmark.run("Large buffer (10KB append)", 1000, []() {
        Buffer buffer;
        for (int i = 0; i < 200; ++i) {
            buffer.append("This is a test data for buffer performance testing.");
        }
        std::string data = buffer.retrieveAllAsString();
    });

    benchmark.run("Buffer prepend", 50000, []() {
        Buffer buf;
        buf.append("World", 5);
        buf.prepend("Hello ", 6);
    });

    benchmark.run("findCRLF search", 50000, []() {
        Buffer buf;
        buf.append("GET /index HTTP/1.1\r\nHost: localhost\r\n\r\n", 39);
        const char* p = buf.findCRLF();
        if (p) buf.retrieveUntil(p + 2);
    });

    // Concurrent buffer stress
    StressTest stress;
    stress.runConcurrentTest("Concurrent buffer ops", 8, 5000, []() {
        Buffer buf;
        for (int i = 0; i < 20; ++i) {
            buf.append("0123456789", 10);
        }
        buf.retrieveAllAsString();
        return true;
    });

    benchmark.printSummary();
}

void testDataThroughput() {
    LOG_INFO << "=== Testing Data Throughput ===";

    Benchmark benchmark;
    size_t total_bytes = 0;

    benchmark.run("Small packet (64B) encode/decode", 50000, [&total_bytes]() {
        Buffer buf;
        buf.append(std::string(64, 'X'));
        buf.retrieveAllAsString();
        total_bytes += 64;
    });

    double time = benchmark.run("Large packet (1KB) encode/decode", 1000, [&total_bytes]() {
        Buffer buf;
        buf.append(std::string(1024, 'X'));
        buf.retrieveAllAsString();
        total_bytes += 1024;
    }).total_time;

    double throughput = total_bytes / time / 1024 / 1024;
    LOG_INFO << "Data throughput (Buffer encode/decode):";
    LOG_INFO << "  Total data: " << total_bytes << " bytes";
    LOG_INFO << "  Total time: " << time << " s";
    LOG_INFO << "  Throughput: " << throughput << " MB/s";
}

void testConcurrentBufferStress() {
    LOG_INFO << "=== Testing Concurrent Buffer Operations ===";

    StressTest stress;

    // Many threads doing buffer append/retrieve cycles
    auto result = stress.runConcurrentTest("Concurrent buffer stress", 50, 1000, []() {
        Buffer buf;
        for (int i = 0; i < 50; ++i) {
            buf.append("Hello, World! This is a test message for buffer.");
        }
        std::string s = buf.retrieveAllAsString();
        return !s.empty();
    });

    LOG_INFO << "  Operations: " << result.operations;
    LOG_INFO << "  QPS: " << result.qps;
}

void testMemoryUsageUnderLoad() {
    LOG_INFO << "=== Testing Memory Under Load ===";

    MemoryMonitor::printMemoryUsage("Before load");

    std::vector<std::vector<char>> memory_blocks;
    for (int i = 0; i < 1000; ++i) {
        memory_blocks.emplace_back(1024 * 1024, 'X');
    }

    MemoryMonitor::printMemoryUsage("After allocating 1GB");

    memory_blocks.clear();
    memory_blocks.shrink_to_fit();

    MemoryMonitor::printMemoryUsage("After freeing");
}

int main() {
    LoggerImpl::instance().setLogLevel(INFO);
    LOG_INFO << "=== Network Layer Performance Tests ===";

    try {
        testBufferPerformance();
        testDataThroughput();
        testConcurrentBufferStress();
        testMemoryUsageUnderLoad();

        LOG_INFO << "=== Network Layer Performance Tests Complete ===";
    } catch (const std::exception& e) {
        LOG_ERROR << "Performance test exception: " << e.what();
        return 1;
    }

    return 0;
}
