#include <iostream>
#include <string>

#include "storage/MemoryStorageEngine.h"
#include "utils/Logging.h"

using namespace kvstore;

int main() {
    LoggerImpl::instance().setLogLevel(INFO);
    LOG_INFO << "=== Storage 测试程序开始 ===";
    
    MemoryStorageEngine engine;
    
    // 测试 String
    LOG_INFO << "--- 测试 String ---";
    engine.set("key1", "value1");
    engine.set("key2", "value2");
    LOG_INFO << "GET key1: " << *engine.get("key1");
    LOG_INFO << "GET key2: " << *engine.get("key2");
    LOG_INFO << "EXISTS key1: " << (engine.exists("key1") ? "yes" : "no");
    
    // 测试 Hash
    LOG_INFO << "--- 测试 Hash ---";
    engine.hset("hash1", "field1", "value1");
    engine.hset("hash1", "field2", "value2");
    LOG_INFO << "HGET hash1 field1: " << *engine.hget("hash1", "field1");
    LOG_INFO << "HGET hash1 field2: " << *engine.hget("hash1", "field2");
    auto hkeys = engine.hkeys("hash1");
    LOG_INFO << "HKEYS hash1: ";
    for (const auto& k : hkeys) LOG_INFO << "  " << k;
    
    // 测试 List
    LOG_INFO << "--- 测试 List ---";
    engine.rpush("list1", "a");
    engine.rpush("list1", "b");
    engine.rpush("list1", "c");
    engine.lpush("list1", "X");
    LOG_INFO << "LLEN list1: " << engine.llen("list1");
    auto lrange = engine.lrange("list1", 0, -1);
    LOG_INFO << "LRANGE list1 0 -1: ";
    for (const auto& s : lrange) LOG_INFO << "  " << s;
    auto lpop = engine.lpop("list1");
    LOG_INFO << "LPOP list1: " << *lpop;
    LOG_INFO << "LLEN list1: " << engine.llen("list1");
    
    // 测试 Set
    LOG_INFO << "--- 测试 Set ---";
    engine.sadd("set1", "a");
    engine.sadd("set1", "b");
    engine.sadd("set1", "c");
    LOG_INFO << "SCARD set1: " << engine.scard("set1");
    LOG_INFO << "SISMEMBER set1 b: " << (engine.sismember("set1", "b") ? "yes" : "no");
    auto smembers = engine.smembers("set1");
    LOG_INFO << "SMEMBERS set1: ";
    for (const auto& s : smembers) LOG_INFO << "  " << s;
    engine.srem("set1", "b");
    LOG_INFO << "SREM set1 b: SCARD is now " << engine.scard("set1");
    
    // 测试 Sorted Set
    LOG_INFO << "--- 测试 Sorted Set ---";
    engine.zadd("zset1", 1.0, "a");
    engine.zadd("zset1", 3.0, "c");
    engine.zadd("zset1", 2.0, "b");
    LOG_INFO << "ZCARD zset1: " << engine.zcard("zset1");
    auto zrange = engine.zrange("zset1", 0, -1);
    LOG_INFO << "ZRANGE zset1 0 -1: ";
    for (const auto& p : zrange) LOG_INFO << "  " << p.second << " (" << p.first << ")";
    auto zrevrange = engine.zrevrange("zset1", 0, -1);
    LOG_INFO << "ZREVRANGE zset1 0 -1: ";
    for (const auto& p : zrevrange) LOG_INFO << "  " << p.second << " (" << p.first << ")";
    
    // 测试 KEYS
    LOG_INFO << "--- 测试 KEYS ---";
    auto keys = engine.keys("*");
    LOG_INFO << "KEYS *: ";
    for (const auto& k : keys) LOG_INFO << "  " << k;
    
    // 测试 FLUSHALL
    engine.flushall();
    LOG_INFO << "After FLUSHALL, EXISTS key1: " << (engine.exists("key1") ? "yes" : "no");
    
    LOG_INFO << "=== Storage 测试程序结束 ===";
    return 0;
}
