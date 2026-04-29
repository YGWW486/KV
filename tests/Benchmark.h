#ifndef KVSTORE_TESTS_BENCHMARK_H
#define KVSTORE_TESTS_BENCHMARK_H

#include <chrono>
#include <string>
#include <functional>
#include <vector>
#include <map>
#include <iostream>
#include "../src/utils/Logging.h"

namespace kvstore {

class Timer {
private:
    std::chrono::high_resolution_clock::time_point start_time_;
    
public:
    Timer() : start_time_(std::chrono::high_resolution_clock::now()) {}
    
    double elapsed() const {
        auto end_time = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double>(end_time - start_time_).count();
    }
    
    void reset() {
        start_time_ = std::chrono::high_resolution_clock::now();
    }
};

class BenchmarkResult {
public:
    std::string name;
    double total_time;
    size_t operations;
    double qps;
    double avg_latency;
    double min_latency;
    double max_latency;
    
    BenchmarkResult(const std::string& test_name, double time, size_t ops)
        : name(test_name), total_time(time), operations(ops) {
        qps = operations / total_time;
        avg_latency = total_time * 1000 / operations; // ms
        min_latency = 0;
        max_latency = 0;
    }
    
    void print() const {
        LOG_INFO << "=== " << name << " 基准测试结果 ===";
        LOG_INFO << "总时间: " << total_time << " 秒";
        LOG_INFO << "操作次数: " << operations;
        LOG_INFO << "QPS: " << qps << " 次/秒";
        LOG_INFO << "平均延迟: " << avg_latency << " 毫秒";
        LOG_INFO << "最小延迟: " << min_latency << " 毫秒";
        LOG_INFO << "最大延迟: " << max_latency << " 毫秒";
        LOG_INFO << "================================";
    }
};

class Benchmark {
private:
    std::vector<BenchmarkResult> results_;
    
public:
    // 运行基准测试
    BenchmarkResult run(const std::string& name, 
                       size_t iterations,
                       const std::function<void()>& operation) {
        Timer timer;
        
        for (size_t i = 0; i < iterations; ++i) {
            operation();
        }
        
        double time = timer.elapsed();
        BenchmarkResult result(name, time, iterations);
        results_.push_back(result);
        
        return result;
    }
    
    // 运行带延迟统计的基准测试
    BenchmarkResult runWithLatency(const std::string& name,
                                  size_t iterations,
                                  const std::function<void()>& operation) {
        std::vector<double> latencies;
        latencies.reserve(iterations);
        
        double total_time = 0;
        
        for (size_t i = 0; i < iterations; ++i) {
            Timer op_timer;
            operation();
            double latency = op_timer.elapsed() * 1000; // ms
            latencies.push_back(latency);
            total_time += latency / 1000;
        }
        
        double min_latency = *std::min_element(latencies.begin(), latencies.end());
        double max_latency = *std::max_element(latencies.begin(), latencies.end());
        double avg_latency = total_time * 1000 / iterations;
        
        BenchmarkResult result(name, total_time, iterations);
        result.min_latency = min_latency;
        result.max_latency = max_latency;
        result.avg_latency = avg_latency;
        
        results_.push_back(result);
        return result;
    }
    
    // 打印所有结果
    void printSummary() const {
        LOG_INFO << "=== 基准测试汇总 ===";
        for (const auto& result : results_) {
            LOG_INFO << result.name << ": " << result.qps << " QPS";
        }
        LOG_INFO << "===================";
    }
    
    // 清空结果
    void clear() {
        results_.clear();
    }
};

// 内存使用监控
class MemoryMonitor {
public:
    static size_t getCurrentRSS() {
        // Windows上获取内存使用量的简化实现
        // 实际实现应该使用Windows API
        return 0;
    }
    
    static void printMemoryUsage(const std::string& context = "") {
        size_t rss = getCurrentRSS();
        if (!context.empty()) {
            LOG_INFO << context << " - 内存使用: " << rss << " KB";
        } else {
            LOG_INFO << "当前内存使用: " << rss << " KB";
        }
    }
};

} // namespace kvstore

#endif // KVSTORE_TESTS_BENCHMARK_H