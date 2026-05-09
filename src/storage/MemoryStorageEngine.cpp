#include "MemoryStorageEngine.h"
#include <chrono>

namespace kvstore {

namespace {

bool matchPattern(const std::string& pattern, const std::string& text) {
    size_t p = 0;
    size_t t = 0;
    size_t star = std::string::npos;
    size_t match = 0;

    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            match = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++match;
        } else {
            return false;
        }
    }

    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }

    return p == pattern.size();
}

bool eraseKeyFromAllTypes(
    const std::string& key,
    std::unordered_map<std::string, std::string>& string_map,
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& hash_map,
    std::unordered_map<std::string, std::deque<std::string>>& list_map,
    std::unordered_map<std::string, std::unordered_set<std::string>>& set_map,
    std::unordered_map<std::string, std::unordered_map<std::string, double>>& z_score_map,
    std::unordered_map<std::string, std::map<double, std::unordered_set<std::string>>>& z_order_map) {
    bool erased = false;
    erased |= string_map.erase(key) > 0;
    erased |= hash_map.erase(key) > 0;
    erased |= list_map.erase(key) > 0;
    erased |= set_map.erase(key) > 0;
    erased |= z_score_map.erase(key) > 0;
    erased |= z_order_map.erase(key) > 0;
    return erased;
}

} // namespace

// ==================== String 操作 ====================
bool MemoryStorageEngine::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    eraseKeyFromAllTypes(key, string_map_, hash_map_, list_map_, set_map_, z_score_map_, z_order_map_);
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
    return eraseKeyFromAllTypes(key, string_map_, hash_map_, list_map_, set_map_, z_score_map_, z_order_map_);
}

bool MemoryStorageEngine::exists(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    return string_map_.count(key) > 0
        || hash_map_.count(key) > 0
        || list_map_.count(key) > 0
        || set_map_.count(key) > 0
        || z_score_map_.count(key) > 0;
}

// ==================== Hash 操作 ====================
bool MemoryStorageEngine::hset(const std::string& key, const std::string& field, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = hash_map_.find(key);
    if (it == hash_map_.end()) {
        eraseKeyFromAllTypes(key, string_map_, hash_map_, list_map_, set_map_, z_score_map_, z_order_map_);
    }
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
    result.reserve(hash_it->second.size());
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
    result.reserve(hash_it->second.size());
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

std::vector<std::pair<std::string, std::string>> MemoryStorageEngine::hgetall(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, std::string>> result;
    auto hash_it = hash_map_.find(key);
    if (hash_it == hash_map_.end()) {
        return result;
    }
    result.reserve(hash_it->second.size());
    for (const auto& pair : hash_it->second) {
        result.emplace_back(pair.first, pair.second);
    }
    return result;
}

// ==================== List 操作 ====================
bool MemoryStorageEngine::lpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = list_map_.find(key);
    if (it == list_map_.end()) {
        eraseKeyFromAllTypes(key, string_map_, hash_map_, list_map_, set_map_, z_score_map_, z_order_map_);
        list_map_[key].push_front(value);
    } else {
        it->second.push_front(value);
    }
    return true;
}

bool MemoryStorageEngine::rpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = list_map_.find(key);
    if (it == list_map_.end()) {
        eraseKeyFromAllTypes(key, string_map_, hash_map_, list_map_, set_map_, z_score_map_, z_order_map_);
        list_map_[key].push_back(value);
    } else {
        it->second.push_back(value);
    }
    return true;
}

std::optional<std::string> MemoryStorageEngine::lpop(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto list_it = list_map_.find(key);
    if (list_it == list_map_.end() || list_it->second.empty()) {
        return std::nullopt;
    }
    std::string value = std::move(list_it->second.front());
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
    std::string value = std::move(list_it->second.back());
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
    
    result.reserve(static_cast<size_t>(end - start + 1));
    for (long i = start; i <= end; ++i) {
        result.push_back(list[static_cast<size_t>(i)]);
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
    auto it = set_map_.find(key);
    if (it == set_map_.end()) {
        eraseKeyFromAllTypes(key, string_map_, hash_map_, list_map_, set_map_, z_score_map_, z_order_map_);
    }
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
    result.reserve(set_it->second.size());
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
    auto score_map_it = z_score_map_.find(key);
    if (score_map_it == z_score_map_.end()) {
        eraseKeyFromAllTypes(key, string_map_, hash_map_, list_map_, set_map_, z_score_map_, z_order_map_);
    }

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
    auto score_map_it = z_score_map_.find(key);
    if (score_map_it == z_score_map_.end()) return result;
    long len = static_cast<long>(score_map_it->second.size());

    if (start < 0) start += len;
    if (end < 0) end += len;
    if (start < 0) start = 0;
    if (end >= len) end = len - 1;
    if (start > end) return result;

    result.reserve(static_cast<size_t>(end - start + 1));
    auto order_map_it = z_order_map_.find(key);
    const auto& order_map = order_map_it->second;
    long pos = 0;

    for (auto score_rit = order_map.rbegin(); score_rit != order_map.rend(); ++score_rit) {
        for (const auto& member : score_rit->second) {
            if (pos >= start) {
                result.emplace_back(score_rit->first, member);
                if (pos >= end) return result;
            }
            ++pos;
        }
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
KeyType MemoryStorageEngine::getType(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (string_map_.count(key)) return KeyType::String;
    if (hash_map_.count(key))   return KeyType::Hash;
    if (list_map_.count(key))   return KeyType::List;
    if (set_map_.count(key))    return KeyType::Set;
    if (z_score_map_.count(key)) return KeyType::ZSet;
    return KeyType::None;
}

std::vector<std::string> MemoryStorageEngine::keys(const std::string& pattern) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;

    auto collect = [&](const auto& map) {
        for (const auto& p : map) {
            if (seen.insert(p.first).second && matchPattern(pattern, p.first)) {
                result.push_back(p.first);
            }
        }
    };

    collect(string_map_);
    collect(hash_map_);
    collect(list_map_);
    collect(set_map_);
    collect(z_score_map_);

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

// ---- TTL stubs ----

bool MemoryStorageEngine::expire(const std::string& key, int64_t ttlMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (string_map_.count(key) == 0 && hash_map_.count(key) == 0 &&
        list_map_.count(key) == 0 && set_map_.count(key) == 0 &&
        z_score_map_.count(key) == 0) return false;
    expires_[key] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + ttlMs;
    return true;
}

int64_t MemoryStorageEngine::ttl(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!exists(key)) return -2;
    auto it = expires_.find(key);
    if (it == expires_.end()) return -1;
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t remain = it->second - now;
    return remain > 0 ? remain : -2;
}

bool MemoryStorageEngine::persist(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    return expires_.erase(key) > 0;
}

size_t MemoryStorageEngine::evictExpired(size_t) { return 0; }

// ---- Memory / LRU stubs ----

size_t MemoryStorageEngine::getMemoryUsage() const { return memory_usage_.load(); }

size_t MemoryStorageEngine::getKeyCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return string_map_.size() + hash_map_.size() + list_map_.size() +
           set_map_.size() + z_score_map_.size();
}

std::string MemoryStorageEngine::getKeyspaceInfo() const {
    return "db0:keys=" + std::to_string(getKeyCount()) + ",expires=0";
}

void MemoryStorageEngine::touchKey(const std::string&) {}
void MemoryStorageEngine::tickLRUClock() {}
size_t MemoryStorageEngine::evictLRU(size_t) { return 0; }

} // namespace kvstore
