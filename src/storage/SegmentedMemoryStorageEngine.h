#ifndef KVSTORE_STORAGE_SEGMENTED_MEMORY_STORAGE_ENGINE_H
#define KVSTORE_STORAGE_SEGMENTED_MEMORY_STORAGE_ENGINE_H

#include "storage/StorageEngine.h"
#include <unordered_map>
#include <deque>
#include <unordered_set>
#include <map>
#include <shared_mutex>
#include <vector>
#include <string>

namespace kvstore {

static constexpr size_t kDefaultSegmentCount = 16;

struct alignas(64) StorageSegment {
    std::unordered_map<std::string, std::string> string_map;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> hash_map;
    std::unordered_map<std::string, std::deque<std::string>> list_map;
    std::unordered_map<std::string, std::unordered_set<std::string>> set_map;
    std::unordered_map<std::string, std::unordered_map<std::string, double>> z_score_map;
    std::unordered_map<std::string, std::map<double, std::unordered_set<std::string>>> z_order_map;

    std::unordered_map<std::string, KeyType> key_types;
    mutable std::shared_mutex mutex;

    bool eraseKeyByType(const std::string& key) {
        auto it = key_types.find(key);
        if (it == key_types.end()) return false;
        switch (it->second) {
        case KeyType::String: string_map.erase(key); break;
        case KeyType::Hash:   hash_map.erase(key);   break;
        case KeyType::List:   list_map.erase(key);   break;
        case KeyType::Set:    set_map.erase(key);    break;
        case KeyType::ZSet:
            z_score_map.erase(key);
            z_order_map.erase(key);
            break;
        default: break;
        }
        key_types.erase(it);
        return true;
    }

    KeyType getKeyType(const std::string& key) const {
        auto it = key_types.find(key);
        return (it != key_types.end()) ? it->second : KeyType::None;
    }

    bool keyExists(const std::string& key) const {
        return key_types.count(key) > 0;
    }
};

class SegmentedMemoryStorageEngine : public StorageEngine {
public:
    explicit SegmentedMemoryStorageEngine(size_t segment_count = kDefaultSegmentCount);
    ~SegmentedMemoryStorageEngine() override = default;

    // String
    bool set(const std::string& key, const std::string& value) override;
    std::optional<std::string> get(const std::string& key) override;
    bool del(const std::string& key) override;
    bool exists(const std::string& key) override;

    // Hash
    bool hset(const std::string& key, const std::string& field, const std::string& value) override;
    std::optional<std::string> hget(const std::string& key, const std::string& field) override;
    bool hdel(const std::string& key, const std::string& field) override;
    bool hexists(const std::string& key, const std::string& field) override;
    std::vector<std::string> hkeys(const std::string& key) override;
    std::vector<std::string> hvals(const std::string& key) override;
    std::vector<std::pair<std::string, std::string>> hgetall(const std::string& key) override;
    size_t hlen(const std::string& key) override;

    // List
    bool lpush(const std::string& key, const std::string& value) override;
    bool rpush(const std::string& key, const std::string& value) override;
    std::optional<std::string> lpop(const std::string& key) override;
    std::optional<std::string> rpop(const std::string& key) override;
    std::vector<std::string> lrange(const std::string& key, long start, long end) override;
    size_t llen(const std::string& key) override;

    // Set
    bool sadd(const std::string& key, const std::string& value) override;
    bool srem(const std::string& key, const std::string& value) override;
    bool sismember(const std::string& key, const std::string& value) override;
    std::vector<std::string> smembers(const std::string& key) override;
    size_t scard(const std::string& key) override;

    // Sorted Set
    bool zadd(const std::string& key, double score, const std::string& value) override;
    bool zrem(const std::string& key, const std::string& value) override;
    std::vector<std::pair<double, std::string>> zrange(const std::string& key, long start, long end) override;
    std::vector<std::pair<double, std::string>> zrevrange(const std::string& key, long start, long end) override;
    size_t zcard(const std::string& key) override;

    // Global
    std::vector<std::string> keys(const std::string& pattern) override;
    KeyType getType(const std::string& key) override;
    bool flushall() override;

    size_t segmentCount() const { return segment_count_; }

private:
    size_t segmentIndex(const std::string& key) const;
    static bool matchPattern(const std::string& pattern, const std::string& text);

    std::vector<std::unique_ptr<StorageSegment>> segments_;
    const size_t segment_count_;
};

} // namespace kvstore

#endif
