#include "MemoryStorageEngine.h"

namespace kvstore {

// ==================== String 操作 ====================
bool MemoryStorageEngine::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    string_map_[key] = value;
    return true;
}

std::optional<std::string> MemoryStorageEngine::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = string_map_.find(key);
    if (it == string_map_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool MemoryStorageEngine::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    return string_map_.erase(key) > 0;
}

bool MemoryStorageEngine::exists(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    return string_map_.count(key) > 0;
}

// ==================== Hash 操作 ====================
bool MemoryStorageEngine::hset(const std::string& key, const std::string& field, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    hash_map_[key][field] = value;
    return true;
}

std::optional<std::string> MemoryStorageEngine::hget(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto hash_it = hash_map_.find(key);
    if (hash_it == hash_map_.end()) {
        return std::nullopt;
    }
    auto& field_map = hash_it->second;
    auto field_it = field_map.find(field);
    if (field_it == field_map.end()) {
        return std::nullopt;
    }
    return field_it->second;
}

bool MemoryStorageEngine::hdel(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto hash_it = hash_map_.find(key);
    if (hash_it == hash_map_.end()) {
        return false;
    }
    return hash_it->second.erase(field) > 0;
}

bool MemoryStorageEngine::hexists(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto hash_it = hash_map_.find(key);
    if (hash_it == hash_map_.end()) {
        return false;
    }
    return hash_it->second.count(field) > 0;
}

std::vector<std::string> MemoryStorageEngine::hkeys(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    auto hash_it = hash_map_.find(key);
    if (hash_it == hash_map_.end()) {
        return result;
    }
    for (const auto& pair : hash_it->second) {
        result.push_back(pair.first);
    }
    return result;
}

std::vector<std::string> MemoryStorageEngine::hvals(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    auto hash_it = hash_map_.find(key);
    if (hash_it == hash_map_.end()) {
        return result;
    }
    for (const auto& pair : hash_it->second) {
        result.push_back(pair.second);
    }
    return result;
}

size_t MemoryStorageEngine::hlen(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto hash_it = hash_map_.find(key);
    if (hash_it == hash_map_.end()) {
        return 0;
    }
    return hash_it->second.size();
}

// ==================== List 操作 ====================
bool MemoryStorageEngine::lpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    list_map_[key].push_front(value);
    return true;
}

bool MemoryStorageEngine::rpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    list_map_[key].push_back(value);
    return true;
}

std::optional<std::string> MemoryStorageEngine::lpop(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto list_it = list_map_.find(key);
    if (list_it == list_map_.end() || list_it->second.empty()) {
        return std::nullopt;
    }
    std::string value = list_it->second.front();
    list_it->second.pop_front();
    if (list_it->second.empty()) {
        list_map_.erase(list_it);
    }
    return value;
}

std::optional<std::string> MemoryStorageEngine::rpop(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto list_it = list_map_.find(key);
    if (list_it == list_map_.end() || list_it->second.empty()) {
        return std::nullopt;
    }
    std::string value = list_it->second.back();
    list_it->second.pop_back();
    if (list_it->second.empty()) {
        list_map_.erase(list_it);
    }
    return value;
}

std::vector<std::string> MemoryStorageEngine::lrange(const std::string& key, long start, long end) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    auto list_it = list_map_.find(key);
    if (list_it == list_map_.end()) {
        return result;
    }
    
    const auto& list = list_it->second;
    long len = static_cast<long>(list.size());
    
    if (start < 0) start += len;
    if (end < 0) end += len;
    if (start < 0) start = 0;
    if (end >= len) end = len - 1;
    if (start > end) return result;
    
    auto it = list.begin();
    std::advance(it, start);
    for (long i = start; i <= end; ++i, ++it) {
        result.push_back(*it);
    }
    
    return result;
}

size_t MemoryStorageEngine::llen(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto list_it = list_map_.find(key);
    if (list_it == list_map_.end()) {
        return 0;
    }
    return list_it->second.size();
}

// ==================== Set 操作 ====================
bool MemoryStorageEngine::sadd(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    return set_map_[key].insert(value).second;
}

bool MemoryStorageEngine::srem(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto set_it = set_map_.find(key);
    if (set_it == set_map_.end()) {
        return false;
    }
    size_t erased = set_it->second.erase(value);
    if (set_it->second.empty()) {
        set_map_.erase(set_it);
    }
    return erased > 0;
}

bool MemoryStorageEngine::sismember(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto set_it = set_map_.find(key);
    if (set_it == set_map_.end()) {
        return false;
    }
    return set_it->second.count(value) > 0;
}

std::vector<std::string> MemoryStorageEngine::smembers(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    auto set_it = set_map_.find(key);
    if (set_it == set_map_.end()) {
        return result;
    }
    for (const auto& s : set_it->second) {
        result.push_back(s);
    }
    return result;
}

size_t MemoryStorageEngine::scard(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto set_it = set_map_.find(key);
    if (set_it == set_map_.end()) {
        return 0;
    }
    return set_it->second.size();
}

// ==================== Sorted Set 操作 ====================
bool MemoryStorageEngine::zadd(const std::string& key, double score, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 如果 value 已经存在，先移除旧的
    auto& score_map = z_score_map_[key];
    auto& order_map = z_order_map_[key];
    auto old_it = score_map.find(value);
    if (old_it != score_map.end()) {
        double old_score = old_it->second;
        auto& s = order_map[old_score];
        s.erase(value);
        if (s.empty()) {
            order_map.erase(old_score);
        }
    }
    
    // 插入新的
    score_map[value] = score;
    order_map[score].insert(value);
    return true;
}

bool MemoryStorageEngine::zrem(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto score_map_it = z_score_map_.find(key);
    auto order_map_it = z_order_map_.find(key);
    
    if (score_map_it == z_score_map_.end()) {
        return false;
    }
    
    auto& score_map = score_map_it->second;
    auto& order_map = order_map_it->second;
    
    auto it = score_map.find(value);
    if (it == score_map.end()) {
        return false;
    }
    
    double score = it->second;
    score_map.erase(it);
    
    auto& s = order_map[score];
    s.erase(value);
    if (s.empty()) {
        order_map.erase(score);
    }
    
    if (score_map.empty()) {
        z_score_map_.erase(score_map_it);
        z_order_map_.erase(order_map_it);
    }
    
    return true;
}

std::vector<std::pair<double, std::string>> MemoryStorageEngine::zrange(const std::string& key, long start, long end) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<double, std::string>> result;
    auto order_map_it = z_order_map_.find(key);
    if (order_map_it == z_order_map_.end()) {
        return result;
    }
    
    const auto& order_map = order_map_it->second;
    long pos = 0;
    
    for (const auto& score_pair : order_map) {
        double score = score_pair.first;
        for (const auto& value : score_pair.second) {
            if (pos >= start && (end == -1 || pos <= end)) {
                result.emplace_back(score, value);
            }
            pos++;
            if (end != -1 && pos > end) break;
        }
        if (end != -1 && pos > end) break;
    }
    
    return result;
}

std::vector<std::pair<double, std::string>> MemoryStorageEngine::zrevrange(const std::string& key, long start, long end) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<double, std::string>> result;
    auto order_map_it = z_order_map_.find(key);
    if (order_map_it == z_order_map_.end()) {
        return result;
    }
    
    const auto& order_map = order_map_it->second;
    std::vector<std::pair<double, std::string>> temp;
    
    for (const auto& score_pair : order_map) {
        double score = score_pair.first;
        for (const auto& value : score_pair.second) {
            temp.emplace_back(score, value);
        }
    }
    
    long len = static_cast<long>(temp.size());
    if (start < 0) start += len;
    if (end < 0) end += len;
    if (start < 0) start = 0;
    if (end >= len) end = len - 1;
    if (start > end) return result;
    
    for (long i = len - 1 - start; i >= len - 1 - end; --i) {
        result.push_back(temp[i]);
    }
    
    return result;
}

size_t MemoryStorageEngine::zcard(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto score_map_it = z_score_map_.find(key);
    if (score_map_it == z_score_map_.end()) {
        return 0;
    }
    return score_map_it->second.size();
}

// ==================== 通用操作 ====================
std::vector<std::string> MemoryStorageEngine::keys(const std::string& pattern) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    
    for (const auto& p : string_map_) result.push_back(p.first);
    for (const auto& p : hash_map_) result.push_back(p.first);
    for (const auto& p : list_map_) result.push_back(p.first);
    for (const auto& p : set_map_) result.push_back(p.first);
    for (const auto& p : z_score_map_) result.push_back(p.first);
    
    return result;
}

bool MemoryStorageEngine::flushall() {
    std::lock_guard<std::mutex> lock(mutex_);
    string_map_.clear();
    hash_map_.clear();
    list_map_.clear();
    set_map_.clear();
    z_score_map_.clear();
    z_order_map_.clear();
    return true;
}

} // namespace kvstore
