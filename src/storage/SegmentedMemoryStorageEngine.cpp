#include "storage/SegmentedMemoryStorageEngine.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace kvstore {

static size_t roundUpPowerOf2(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

SegmentedMemoryStorageEngine::SegmentedMemoryStorageEngine(size_t segment_count)
    : segment_count_(roundUpPowerOf2(segment_count)) {
    segments_.reserve(segment_count_);
    for (size_t i = 0; i < segment_count_; ++i)
        segments_.emplace_back(std::make_unique<StorageSegment>());
}

size_t SegmentedMemoryStorageEngine::segmentIndex(const std::string& key) const {
    return std::hash<std::string>{}(key) & (segment_count_ - 1);
}

bool SegmentedMemoryStorageEngine::matchPattern(const std::string& pattern, const std::string& text) {
    size_t p = 0, t = 0;
    size_t star = std::string::npos;
    size_t match = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p; ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++; match = t;
        } else if (star != std::string::npos) {
            p = star + 1; t = ++match;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

#define SEG auto& seg = *segments_[segmentIndex(key)]
#define LOCK_EX std::unique_lock<std::shared_mutex> lock(seg.mutex)
#define LOCK_SH std::shared_lock<std::shared_mutex> lock(seg.mutex)

// ---- String ops ----

bool SegmentedMemoryStorageEngine::set(const std::string& key, const std::string& value) {
    SEG; LOCK_EX;
    seg.eraseKeyByType(key);
    seg.string_map[key] = value;
    seg.key_types[key] = KeyType::String;
    return true;
}

std::optional<std::string> SegmentedMemoryStorageEngine::get(const std::string& key) {
    SEG; LOCK_SH;
    auto it = seg.string_map.find(key);
    if (it == seg.string_map.end()) return std::nullopt;
    return it->second;
}

bool SegmentedMemoryStorageEngine::del(const std::string& key) {
    SEG; LOCK_EX;
    return seg.eraseKeyByType(key);
}

bool SegmentedMemoryStorageEngine::exists(const std::string& key) {
    SEG; LOCK_SH;
    return seg.keyExists(key);
}

// ---- Hash ops ----

bool SegmentedMemoryStorageEngine::hset(const std::string& key, const std::string& field, const std::string& value) {
    SEG; LOCK_EX;
    auto it = seg.hash_map.find(key);
    if (it == seg.hash_map.end()) {
        seg.eraseKeyByType(key);
        seg.key_types[key] = KeyType::Hash;
    }
    seg.hash_map[key][field] = value;
    return true;
}

std::optional<std::string> SegmentedMemoryStorageEngine::hget(const std::string& key, const std::string& field) {
    SEG; LOCK_SH;
    auto hit = seg.hash_map.find(key);
    if (hit == seg.hash_map.end()) return std::nullopt;
    auto fit = hit->second.find(field);
    if (fit == hit->second.end()) return std::nullopt;
    return fit->second;
}

bool SegmentedMemoryStorageEngine::hdel(const std::string& key, const std::string& field) {
    SEG; LOCK_EX;
    auto hit = seg.hash_map.find(key);
    if (hit == seg.hash_map.end()) return false;
    bool erased = hit->second.erase(field) > 0;
    if (hit->second.empty()) {
        seg.hash_map.erase(hit);
        seg.key_types.erase(key);
    }
    return erased;
}

bool SegmentedMemoryStorageEngine::hexists(const std::string& key, const std::string& field) {
    SEG; LOCK_SH;
    auto hit = seg.hash_map.find(key);
    if (hit == seg.hash_map.end()) return false;
    return hit->second.count(field) > 0;
}

std::vector<std::string> SegmentedMemoryStorageEngine::hkeys(const std::string& key) {
    SEG; LOCK_SH;
    std::vector<std::string> result;
    auto hit = seg.hash_map.find(key);
    if (hit != seg.hash_map.end()) {
        result.reserve(hit->second.size());
        for (const auto& p : hit->second) result.push_back(p.first);
    }
    return result;
}

std::vector<std::string> SegmentedMemoryStorageEngine::hvals(const std::string& key) {
    SEG; LOCK_SH;
    std::vector<std::string> result;
    auto hit = seg.hash_map.find(key);
    if (hit != seg.hash_map.end()) {
        result.reserve(hit->second.size());
        for (const auto& p : hit->second) result.push_back(p.second);
    }
    return result;
}

std::vector<std::pair<std::string, std::string>> SegmentedMemoryStorageEngine::hgetall(const std::string& key) {
    SEG; LOCK_SH;
    std::vector<std::pair<std::string, std::string>> result;
    auto hit = seg.hash_map.find(key);
    if (hit != seg.hash_map.end()) {
        result.reserve(hit->second.size());
        for (const auto& p : hit->second) result.emplace_back(p.first, p.second);
    }
    return result;
}

size_t SegmentedMemoryStorageEngine::hlen(const std::string& key) {
    SEG; LOCK_SH;
    auto hit = seg.hash_map.find(key);
    if (hit == seg.hash_map.end()) return 0;
    return hit->second.size();
}

// ---- List ops ----

bool SegmentedMemoryStorageEngine::lpush(const std::string& key, const std::string& value) {
    SEG; LOCK_EX;
    auto it = seg.list_map.find(key);
    if (it == seg.list_map.end()) {
        seg.eraseKeyByType(key);
        seg.key_types[key] = KeyType::List;
    }
    seg.list_map[key].push_front(value);
    return true;
}

bool SegmentedMemoryStorageEngine::rpush(const std::string& key, const std::string& value) {
    SEG; LOCK_EX;
    auto it = seg.list_map.find(key);
    if (it == seg.list_map.end()) {
        seg.eraseKeyByType(key);
        seg.key_types[key] = KeyType::List;
    }
    seg.list_map[key].push_back(value);
    return true;
}

std::optional<std::string> SegmentedMemoryStorageEngine::lpop(const std::string& key) {
    SEG; LOCK_EX;
    auto it = seg.list_map.find(key);
    if (it == seg.list_map.end() || it->second.empty()) return std::nullopt;
    std::string v = std::move(it->second.front());
    it->second.pop_front();
    if (it->second.empty()) {
        seg.list_map.erase(it);
        seg.key_types.erase(key);
    }
    return v;
}

std::optional<std::string> SegmentedMemoryStorageEngine::rpop(const std::string& key) {
    SEG; LOCK_EX;
    auto it = seg.list_map.find(key);
    if (it == seg.list_map.end() || it->second.empty()) return std::nullopt;
    std::string v = std::move(it->second.back());
    it->second.pop_back();
    if (it->second.empty()) {
        seg.list_map.erase(it);
        seg.key_types.erase(key);
    }
    return v;
}

std::vector<std::string> SegmentedMemoryStorageEngine::lrange(const std::string& key, long start, long end) {
    SEG; LOCK_SH;
    std::vector<std::string> result;
    auto it = seg.list_map.find(key);
    if (it == seg.list_map.end()) return result;
    const auto& lst = it->second;
    long len = static_cast<long>(lst.size());
    if (start < 0) start += len;
    if (end < 0) end += len;
    if (start < 0) start = 0;
    if (end >= len) end = len - 1;
    if (start > end) return result;
    result.reserve(static_cast<size_t>(end - start + 1));
    for (long i = start; i <= end; ++i) result.push_back(lst[static_cast<size_t>(i)]);
    return result;
}

size_t SegmentedMemoryStorageEngine::llen(const std::string& key) {
    SEG; LOCK_SH;
    auto it = seg.list_map.find(key);
    if (it == seg.list_map.end()) return 0;
    return it->second.size();
}

// ---- Set ops ----

bool SegmentedMemoryStorageEngine::sadd(const std::string& key, const std::string& value) {
    SEG; LOCK_EX;
    auto it = seg.set_map.find(key);
    if (it == seg.set_map.end()) {
        seg.eraseKeyByType(key);
        seg.key_types[key] = KeyType::Set;
    }
    return seg.set_map[key].insert(value).second;
}

bool SegmentedMemoryStorageEngine::srem(const std::string& key, const std::string& value) {
    SEG; LOCK_EX;
    auto it = seg.set_map.find(key);
    if (it == seg.set_map.end()) return false;
    size_t erased = it->second.erase(value);
    if (it->second.empty()) {
        seg.set_map.erase(it);
        seg.key_types.erase(key);
    }
    return erased > 0;
}

bool SegmentedMemoryStorageEngine::sismember(const std::string& key, const std::string& value) {
    SEG; LOCK_SH;
    auto it = seg.set_map.find(key);
    if (it == seg.set_map.end()) return false;
    return it->second.count(value) > 0;
}

std::vector<std::string> SegmentedMemoryStorageEngine::smembers(const std::string& key) {
    SEG; LOCK_SH;
    std::vector<std::string> result;
    auto it = seg.set_map.find(key);
    if (it != seg.set_map.end()) {
        result.reserve(it->second.size());
        for (const auto& m : it->second) result.push_back(m);
    }
    return result;
}

size_t SegmentedMemoryStorageEngine::scard(const std::string& key) {
    SEG; LOCK_SH;
    auto it = seg.set_map.find(key);
    if (it == seg.set_map.end()) return 0;
    return it->second.size();
}

// ---- Sorted Set ops ----

bool SegmentedMemoryStorageEngine::zadd(const std::string& key, double score, const std::string& value) {
    SEG; LOCK_EX;
    auto it = seg.z_score_map.find(key);
    if (it == seg.z_score_map.end()) {
        seg.eraseKeyByType(key);
        seg.key_types[key] = KeyType::ZSet;
    }
    auto& score_map = seg.z_score_map[key];
    auto& order_map = seg.z_order_map[key];
    auto old_it = score_map.find(value);
    if (old_it != score_map.end()) {
        double old_score = old_it->second;
        auto& bucket = order_map[old_score];
        bucket.erase(value);
        if (bucket.empty()) order_map.erase(old_score);
    }
    score_map[value] = score;
    order_map[score].insert(value);
    return true;
}

bool SegmentedMemoryStorageEngine::zrem(const std::string& key, const std::string& value) {
    SEG; LOCK_EX;
    auto sit = seg.z_score_map.find(key);
    if (sit == seg.z_score_map.end()) return false;
    auto oit = seg.z_order_map.find(key);
    auto& score_map = sit->second;
    auto& order_map = oit->second;
    auto it = score_map.find(value);
    if (it == score_map.end()) return false;
    double score = it->second;
    score_map.erase(it);
    auto& bucket = order_map[score];
    bucket.erase(value);
    if (bucket.empty()) order_map.erase(score);
    if (score_map.empty()) {
        seg.z_score_map.erase(sit);
        seg.z_order_map.erase(oit);
        seg.key_types.erase(key);
    }
    return true;
}

std::vector<std::pair<double, std::string>> SegmentedMemoryStorageEngine::zrange(
    const std::string& key, long start, long end) {
    SEG; LOCK_SH;
    std::vector<std::pair<double, std::string>> result;
    auto it = seg.z_order_map.find(key);
    if (it == seg.z_order_map.end()) return result;
    long pos = 0;
    for (const auto& [score, members] : it->second) {
        for (const auto& member : members) {
            if (pos >= start && (end == -1 || pos <= end))
                result.emplace_back(score, member);
            ++pos;
            if (end != -1 && pos > end) break;
        }
        if (end != -1 && pos > end) break;
    }
    return result;
}

std::vector<std::pair<double, std::string>> SegmentedMemoryStorageEngine::zrevrange(
    const std::string& key, long start, long end) {
    SEG; LOCK_SH;
    std::vector<std::pair<double, std::string>> result;
    auto score_it = seg.z_score_map.find(key);
    if (score_it == seg.z_score_map.end()) return result;
    long len = static_cast<long>(score_it->second.size());

    if (start < 0) start += len;
    if (end < 0) end += len;
    if (start < 0) start = 0;
    if (end >= len) end = len - 1;
    if (start > end) return result;

    result.reserve(static_cast<size_t>(end - start + 1));
    auto order_it = seg.z_order_map.find(key);
    const auto& order_map = order_it->second;
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

size_t SegmentedMemoryStorageEngine::zcard(const std::string& key) {
    SEG; LOCK_SH;
    auto it = seg.z_score_map.find(key);
    if (it == seg.z_score_map.end()) return 0;
    return it->second.size();
}

// ---- Global ops ----

KeyType SegmentedMemoryStorageEngine::getType(const std::string& key) {
    SEG; LOCK_SH;
    return seg.getKeyType(key);
}

std::vector<std::string> SegmentedMemoryStorageEngine::keys(const std::string& pattern) {
    std::vector<std::shared_lock<std::shared_mutex>> locks;
    locks.reserve(segment_count_);
    for (size_t i = 0; i < segment_count_; ++i)
        locks.emplace_back(segments_[i]->mutex);

    std::vector<std::string> result;
    for (size_t i = 0; i < segment_count_; ++i) {
        for (const auto& [key, type] : segments_[i]->key_types)
            if (matchPattern(pattern, key))
                result.push_back(key);
    }
    return result;
}

bool SegmentedMemoryStorageEngine::flushall() {
    std::vector<std::unique_lock<std::shared_mutex>> locks;
    locks.reserve(segment_count_);
    for (size_t i = 0; i < segment_count_; ++i)
        locks.emplace_back(segments_[i]->mutex);

    for (size_t i = 0; i < segment_count_; ++i) {
        auto& s = *segments_[i];
        s.string_map.clear();
        s.hash_map.clear();
        s.list_map.clear();
        s.set_map.clear();
        s.z_score_map.clear();
        s.z_order_map.clear();
        s.key_types.clear();
    }
    return true;
}

} // namespace kvstore
