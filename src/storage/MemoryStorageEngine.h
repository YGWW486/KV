#ifndef KVSTORE_STORAGE_MEMORY_STORAGE_ENGINE_H
#define KVSTORE_STORAGE_MEMORY_STORAGE_ENGINE_H

#include "StorageEngine.h"
#include <unordered_map>
#include <list>
#include <unordered_set>
#include <map>
#include <mutex>

namespace kvstore {

class MemoryStorageEngine : public StorageEngine {
public:
    MemoryStorageEngine() = default;
    ~MemoryStorageEngine() override = default;
    
    // --- String 操作
    bool set(const std::string& key, const std::string& value) override;
    std::optional<std::string> get(const std::string& key) override;
    bool del(const std::string& key) override;
    bool exists(const std::string& key) override;
    
    // --- Hash 操作
    bool hset(const std::string& key, const std::string& field, const std::string& value) override;
    std::optional<std::string> hget(const std::string& key, const std::string& field) override;
    bool hdel(const std::string& key, const std::string& field) override;
    bool hexists(const std::string& key, const std::string& field) override;
    std::vector<std::string> hkeys(const std::string& key) override;
    std::vector<std::string> hvals(const std::string& key) override;
    std::vector<std::pair<std::string, std::string>> hgetall(const std::string& key) override;
    size_t hlen(const std::string& key) override;
    
    // --- List 操作
    bool lpush(const std::string& key, const std::string& value) override;
    bool rpush(const std::string& key, const std::string& value) override;
    std::optional<std::string> lpop(const std::string& key) override;
    std::optional<std::string> rpop(const std::string& key) override;
    std::vector<std::string> lrange(const std::string& key, long start, long end) override;
    size_t llen(const std::string& key) override;
    
    // --- Set 操作
    bool sadd(const std::string& key, const std::string& value) override;
    bool srem(const std::string& key, const std::string& value) override;
    bool sismember(const std::string& key, const std::string& value) override;
    std::vector<std::string> smembers(const std::string& key) override;
    size_t scard(const std::string& key) override;
    
    // --- Sorted Set 操作
    bool zadd(const std::string& key, double score, const std::string& value) override;
    bool zrem(const std::string& key, const std::string& value) override;
    std::vector<std::pair<double, std::string>> zrange(const std::string& key, long start, long end) override;
    std::vector<std::pair<double, std::string>> zrevrange(const std::string& key, long start, long end) override;
    size_t zcard(const std::string& key) override;
    
    // --- 通用操作
    std::vector<std::string> keys(const std::string& pattern) override;
    KeyType getType(const std::string& key) override;
    bool flushall() override;
    
private:
    // String 存储
    std::unordered_map<std::string, std::string> string_map_;
    // Hash 存储
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> hash_map_;
    // List 存储
    std::unordered_map<std::string, std::list<std::string>> list_map_;
    // Set 存储
    std::unordered_map<std::string, std::unordered_set<std::string>> set_map_;
    // Sorted Set 存储（使用两个 map：一个值到分数，一个分数到值）
    std::unordered_map<std::string, std::unordered_map<std::string, double>> z_score_map_;
    std::unordered_map<std::string, std::map<double, std::unordered_set<std::string>>> z_order_map_;
    
    // 锁（简单起见，一个大锁，后续可以优化为细粒度锁）
    mutable std::mutex mutex_;
};

} // namespace kvstore

#endif // KVSTORE_STORAGE_MEMORY_STORAGE_ENGINE_H
