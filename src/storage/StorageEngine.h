#ifndef KVSTORE_STORAGE_STORAGE_ENGINE_H
#define KVSTORE_STORAGE_STORAGE_ENGINE_H

#include <cstdint>
#include <string>
#include <optional>
#include <vector>
#include <memory>

namespace kvstore {

enum class KeyType {
    String,
    Hash,
    List,
    Set,
    ZSet,
    None
};

// 存储引擎接口
class StorageEngine {
public:
    virtual ~StorageEngine() = default;

    // --- 类型查询
    virtual KeyType getType(const std::string& key) = 0;

    // --- String 操作
    virtual bool set(const std::string& key, const std::string& value) = 0;
    virtual std::optional<std::string> get(const std::string& key) = 0;
    virtual bool del(const std::string& key) = 0;
    virtual bool exists(const std::string& key) = 0;
    
    // --- Hash 操作
    virtual bool hset(const std::string& key, const std::string& field, const std::string& value) = 0;
    virtual std::optional<std::string> hget(const std::string& key, const std::string& field) = 0;
    virtual bool hdel(const std::string& key, const std::string& field) = 0;
    virtual bool hexists(const std::string& key, const std::string& field) = 0;
    virtual std::vector<std::string> hkeys(const std::string& key) = 0;
    virtual std::vector<std::string> hvals(const std::string& key) = 0;
    virtual std::vector<std::pair<std::string, std::string>> hgetall(const std::string& key) = 0;
    virtual size_t hlen(const std::string& key) = 0;
    
    // --- List 操作
    virtual bool lpush(const std::string& key, const std::string& value) = 0;
    virtual bool rpush(const std::string& key, const std::string& value) = 0;
    virtual std::optional<std::string> lpop(const std::string& key) = 0;
    virtual std::optional<std::string> rpop(const std::string& key) = 0;
    virtual std::vector<std::string> lrange(const std::string& key, long start, long end) = 0;
    virtual size_t llen(const std::string& key) = 0;
    
    // --- Set 操作
    virtual bool sadd(const std::string& key, const std::string& value) = 0;
    virtual bool srem(const std::string& key, const std::string& value) = 0;
    virtual bool sismember(const std::string& key, const std::string& value) = 0;
    virtual std::vector<std::string> smembers(const std::string& key) = 0;
    virtual size_t scard(const std::string& key) = 0;
    
    // --- Sorted Set 操作
    virtual bool zadd(const std::string& key, double score, const std::string& value) = 0;
    virtual bool zrem(const std::string& key, const std::string& value) = 0;
    virtual std::vector<std::pair<double, std::string>> zrange(const std::string& key, long start, long end) = 0;
    virtual std::vector<std::pair<double, std::string>> zrevrange(const std::string& key, long start, long end) = 0;
    virtual size_t zcard(const std::string& key) = 0;
    
    // --- 通用操作
    virtual std::vector<std::string> keys(const std::string& pattern) = 0;
    virtual bool flushall() = 0;

    // --- TTL / 过期
    virtual bool expire(const std::string& key, int64_t ttlMs) = 0;
    virtual int64_t ttl(const std::string& key) = 0;       // ms remain, -1=no expire, -2=no key
    virtual bool persist(const std::string& key) = 0;
    virtual size_t evictExpired(size_t maxSamples) = 0;    // return count deleted

    // --- Memory / LRU
    virtual size_t getMemoryUsage() const = 0;
    virtual size_t getKeyCount() const = 0;
    virtual std::string getKeyspaceInfo() const = 0;
    virtual void touchKey(const std::string& key) = 0;
    virtual void tickLRUClock() = 0;
    virtual size_t evictLRU(size_t targetBytes) = 0;       // return bytes freed
};

} // namespace kvstore

#endif // KVSTORE_STORAGE_STORAGE_ENGINE_H
