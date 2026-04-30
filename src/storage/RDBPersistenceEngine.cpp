#include "storage/PersistenceEngine.h"
#include "storage/StorageEngine.h"
#include "utils/Logging.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace kvstore {

RDBPersistenceEngine::RDBPersistenceEngine(const std::string& rdb_path)
    : rdb_path_(rdb_path) {
    LOG_INFO << "RDB持久化引擎初始化，文件路径: " << rdb_path_;
}

RDBPersistenceEngine::~RDBPersistenceEngine() {
    LOG_INFO << "RDB持久化引擎销毁";
}

bool RDBPersistenceEngine::load(StorageEngine* storage) {
    return loadSnapshot(storage);
}

bool RDBPersistenceEngine::save(const StorageEngine* storage) {
    return saveSnapshot(storage);
}

void RDBPersistenceEngine::recordWrite(const std::string& command) {
    // RDB不记录单个写操作，只在快照时保存
    LOG_DEBUG << "RDB引擎忽略写操作记录: " << command;
}

void RDBPersistenceEngine::setPolicy(AOFPolicy policy) {
    LOG_INFO << "RDB引擎忽略AOF策略设置";
}

std::string RDBPersistenceEngine::getStatus() const {
    std::ifstream file(rdb_path_);
    std::string status = "RDB持久化引擎";
    
    if (file.is_open()) {
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.close();
        status += ", 快照文件大小: " + std::to_string(size) + " 字节";
    } else {
        status += ", 快照文件不存在";
    }
    
    return status;
}

bool RDBPersistenceEngine::saveSnapshot(const StorageEngine* storage) {
    if (!storage) {
        LOG_ERROR << "存储引擎为空，无法保存RDB快照";
        return false;
    }
    
    std::ofstream file(rdb_path_, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR << "无法创建RDB文件: " << rdb_path_;
        return false;
    }
    
    LOG_INFO << "开始保存RDB快照";
    
    // RDB文件头
    file << "KVSTORE_RDB_V1\n";
    file << "timestamp: " << std::time(nullptr) << "\n";
    file << "---DATA---\n";
    
    // 保存各种数据类型
    if (!saveStringData(storage, file)) {
        LOG_ERROR << "保存字符串数据失败";
        return false;
    }
    
    if (!saveHashData(storage, file)) {
        LOG_ERROR << "保存哈希数据失败";
        return false;
    }
    
    if (!saveListData(storage, file)) {
        LOG_ERROR << "保存列表数据失败";
        return false;
    }
    
    if (!saveSetData(storage, file)) {
        LOG_ERROR << "保存集合数据失败";
        return false;
    }
    
    if (!saveSortedSetData(storage, file)) {
        LOG_ERROR << "保存有序集合数据失败";
        return false;
    }
    
    file << "---END---\n";
    file.close();
    
    LOG_INFO << "RDB快照保存完成";
    return true;
}

bool RDBPersistenceEngine::loadSnapshot(StorageEngine* storage) {
    if (!storage) {
        LOG_ERROR << "存储引擎为空，无法加载RDB快照";
        return false;
    }
    
    std::ifstream file(rdb_path_, std::ios::binary);
    if (!file.is_open()) {
        LOG_INFO << "RDB文件不存在，跳过加载: " << rdb_path_;
        return true; // 文件不存在是正常情况
    }
    
    LOG_INFO << "开始加载RDB快照";
    
    // 读取文件头
    std::string header;
    std::getline(file, header);
    
    if (header != "KVSTORE_RDB_V1") {
        LOG_ERROR << "RDB文件格式不匹配: " << header;
        return false;
    }
    
    // 跳过时间戳行
    std::getline(file, header);
    
    // 读取数据分隔符
    std::getline(file, header);
    if (header != "---DATA---") {
        LOG_ERROR << "RDB文件数据格式错误";
        return false;
    }
    
    // 清空现有数据
    storage->flushall();
    
    std::string line;
    size_t loaded_count = 0;
    
    while (std::getline(file, line)) {
        if (line == "---END---") {
            break;
        }
        
        if (line.empty()) continue;

        // 解析数据行格式: TYPE|KEY|VALUE (or more fields depending on type)
        std::vector<std::string> parts;
        size_t pos = 0;
        for (int i = 0; i < 3; ++i) {
            size_t sep = line.find('|', pos);
            if (sep == std::string::npos) break;
            parts.push_back(line.substr(pos, sep - pos));
            pos = sep + 1;
        }
        parts.push_back(line.substr(pos)); // rest of line (may contain more |)

        if (parts.size() < 2) {
            LOG_WARN << "跳过格式错误的RDB数据行: " << line;
            continue;
        }

        const std::string& type = parts[0];
        const std::string& key = parts[1];

        if (type == "STRING" && parts.size() >= 3) {
            if (storage->set(key, parts[2])) ++loaded_count;
        } else if (type == "HASH" && parts.size() >= 4) {
            if (storage->hset(key, parts[2], parts[3])) ++loaded_count;
        } else if (type == "LIST" && parts.size() >= 4) {
            if (storage->rpush(key, parts[3])) ++loaded_count;
        } else if (type == "SET" && parts.size() >= 3) {
            if (storage->sadd(key, parts[2])) ++loaded_count;
        } else if (type == "ZSET" && parts.size() >= 4) {
            double score = 0.0;
            try { score = std::stod(parts[2]); } catch (...) { continue; }
            if (storage->zadd(key, score, parts[3])) ++loaded_count;
        }
    }
    
    file.close();
    LOG_INFO << "RDB快照加载完成，加载了 " << loaded_count << " 个键";
    return true;
}

bool RDBPersistenceEngine::saveStringData(const StorageEngine* storage, std::ostream& os) {
    auto* ms = const_cast<StorageEngine*>(storage);
    size_t count = 0;
    for (const auto& key : ms->keys("*")) {
        if (ms->getType(key) == KeyType::String) {
            if (auto value = ms->get(key)) {
                os << "STRING|" << key << "|" << *value << "\n";
                ++count;
            }
        }
    }
    LOG_INFO << "RDB saved " << count << " string keys";
    return true;
}

bool RDBPersistenceEngine::saveHashData(const StorageEngine* storage, std::ostream& os) {
    auto* ms = const_cast<StorageEngine*>(storage);
    size_t count = 0;
    for (const auto& key : ms->keys("*")) {
        if (ms->getType(key) == KeyType::Hash) {
            for (const auto& [field, value] : ms->hgetall(key)) {
                os << "HASH|" << key << "|" << field << "|" << value << "\n";
            }
            ++count;
        }
    }
    LOG_INFO << "RDB saved " << count << " hash keys";
    return true;
}

bool RDBPersistenceEngine::saveListData(const StorageEngine* storage, std::ostream& os) {
    auto* ms = const_cast<StorageEngine*>(storage);
    size_t count = 0;
    for (const auto& key : ms->keys("*")) {
        if (ms->getType(key) == KeyType::List) {
            auto elements = ms->lrange(key, 0, -1);
            for (size_t i = 0; i < elements.size(); ++i) {
                os << "LIST|" << key << "|" << i << "|" << elements[i] << "\n";
            }
            ++count;
        }
    }
    LOG_INFO << "RDB saved " << count << " list keys";
    return true;
}

bool RDBPersistenceEngine::saveSetData(const StorageEngine* storage, std::ostream& os) {
    auto* ms = const_cast<StorageEngine*>(storage);
    size_t count = 0;
    for (const auto& key : ms->keys("*")) {
        if (ms->getType(key) == KeyType::Set) {
            for (const auto& member : ms->smembers(key)) {
                os << "SET|" << key << "|" << member << "\n";
            }
            ++count;
        }
    }
    LOG_INFO << "RDB saved " << count << " set keys";
    return true;
}

bool RDBPersistenceEngine::saveSortedSetData(const StorageEngine* storage, std::ostream& os) {
    auto* ms = const_cast<StorageEngine*>(storage);
    size_t count = 0;
    for (const auto& key : ms->keys("*")) {
        if (ms->getType(key) == KeyType::ZSet) {
            auto members = ms->zrange(key, 0, -1);
            for (const auto& [score, member] : members) {
                os << "ZSET|" << key << "|" << score << "|" << member << "\n";
            }
            ++count;
        }
    }
    LOG_INFO << "RDB saved " << count << " zset keys";
    return true;
}

} // namespace kvstore