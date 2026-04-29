#include "RDBPersistenceEngine.h"
#include "StorageEngine.h"
#include "../utils/Logging.h"
#include <fstream>
#include <sstream>
#include <iomanip>

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
        
        // 解析数据行格式: TYPE|KEY|VALUE
        size_t first_sep = line.find('|');
        size_t second_sep = line.find('|', first_sep + 1);
        
        if (first_sep == std::string::npos || second_sep == std::string::npos) {
            LOG_WARNING << "跳过格式错误的RDB数据行: " << line;
            continue;
        }
        
        std::string type = line.substr(0, first_sep);
        std::string key = line.substr(first_sep + 1, second_sep - first_sep - 1);
        std::string value = line.substr(second_sep + 1);
        
        if (type == "STRING") {
            if (storage->set(key, value)) {
                loaded_count++;
            }
        }
        // 简化实现，只处理STRING类型
    }
    
    file.close();
    LOG_INFO << "RDB快照加载完成，加载了 " << loaded_count << " 个键";
    return true;
}

bool RDBPersistenceEngine::saveStringData(const StorageEngine* storage, std::ostream& os) {
    // 简化实现：获取所有键并保存为STRING类型
    auto keys = storage->keys("*");
    
    for (const auto& key : keys) {
        if (auto value = storage->get(key)) {
            os << "STRING|" << key << "|" << *value << "\n";
        }
    }
    
    return true;
}

bool RDBPersistenceEngine::saveHashData(const StorageEngine* storage, std::ostream& os) {
    // 简化实现：这里应该保存哈希数据
    // 实际实现需要遍历所有哈希键
    return true;
}

bool RDBPersistenceEngine::saveListData(const StorageEngine* storage, std::ostream& os) {
    // 简化实现：这里应该保存列表数据
    return true;
}

bool RDBPersistenceEngine::saveSetData(const StorageEngine* storage, std::ostream& os) {
    // 简化实现：这里应该保存集合数据
    return true;
}

bool RDBPersistenceEngine::saveSortedSetData(const StorageEngine* storage, std::ostream& os) {
    // 简化实现：这里应该保存有序集合数据
    return true;
}

} // namespace kvstore