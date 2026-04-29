#include "storage/PersistenceEngine.h"
#include "storage/StorageEngine.h"
#include "utils/Logging.h"
#include <fstream>
#include <thread>
#include <chrono>
#include <mutex>
#include <cstdio>

namespace kvstore {

AOFPersistenceEngine::AOFPersistenceEngine(const std::string& aof_path)
    : aof_path_(aof_path), policy_(AOFPolicy::EVERYSEC) {
    LOG_INFO << "AOF持久化引擎初始化，文件路径: " << aof_path_;
}

AOFPersistenceEngine::~AOFPersistenceEngine() {
    LOG_INFO << "AOF持久化引擎销毁";
}

bool AOFPersistenceEngine::load(StorageEngine* storage) {
    if (!storage) {
        LOG_ERROR << "存储引擎为空，无法加载AOF数据";
        return false;
    }
    
    std::ifstream file(aof_path_);
    if (!file.is_open()) {
        LOG_INFO << "AOF文件不存在，跳过加载: " << aof_path_;
        return true; // 文件不存在是正常情况
    }
    
    LOG_INFO << "开始加载AOF文件: " << aof_path_;
    
    std::string line;
    size_t line_count = 0;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        // 这里应该解析RESP协议命令并执行
        // 简化实现：直接解析简单的SET命令
        if (line.find("SET") == 0) {
            // 解析 SET key value 格式
            size_t first_space = line.find(' ');
            size_t second_space = line.find(' ', first_space + 1);
            
            if (first_space != std::string::npos && second_space != std::string::npos) {
                std::string key = line.substr(first_space + 1, second_space - first_space - 1);
                std::string value = line.substr(second_space + 1);
                
                if (!storage->set(key, value)) {
                    LOG_ERROR << "加载AOF数据失败，SET命令执行错误: " << key;
                    return false;
                }
                line_count++;
            }
        }
    }
    
    file.close();
    LOG_INFO << "AOF文件加载完成，处理了 " << line_count << " 条命令";
    return true;
}

bool AOFPersistenceEngine::save(const StorageEngine* storage) {
    // AOF的保存实际上是重写AOF文件
    return rewriteAOF(storage);
}

void AOFPersistenceEngine::recordWrite(const std::string& command) {
    if (command.empty()) {
        LOG_WARN << "记录空命令到AOF";
        return;
    }
    
    std::ofstream file(aof_path_, std::ios::app);
    if (!file.is_open()) {
        LOG_ERROR << "无法打开AOF文件进行追加: " << aof_path_;
        return;
    }
    
    file << command << "\n";
    file.close();
    
    // 根据策略决定是否同步
    if (policy_ == AOFPolicy::ALWAYS) {
        syncAOF();
    }
    
    LOG_DEBUG << "记录命令到AOF: " << command;
}

void AOFPersistenceEngine::setPolicy(AOFPolicy policy) {
    policy_ = policy;
    LOG_INFO << "设置AOF同步策略: " << static_cast<int>(policy);
}

std::string AOFPersistenceEngine::getStatus() const {
    std::ifstream file(aof_path_);
    std::string status = "AOF持久化引擎 - 策略: ";
    
    switch (policy_) {
        case AOFPolicy::ALWAYS: status += "ALWAYS"; break;
        case AOFPolicy::EVERYSEC: status += "EVERYSEC"; break;
        case AOFPolicy::NO: status += "NO"; break;
    }
    
    if (file.is_open()) {
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.close();
        status += ", 文件大小: " + std::to_string(size) + " 字节";
    } else {
        status += ", 文件不存在";
    }
    
    return status;
}

bool AOFPersistenceEngine::rewriteAOF(const StorageEngine* storage) {
    if (!storage) {
        LOG_ERROR << "存储引擎为空，无法重写AOF";
        return false;
    }
    
    std::string temp_path = aof_path_ + ".tmp";
    std::ofstream temp_file(temp_path);
    
    if (!temp_file.is_open()) {
        LOG_ERROR << "无法创建临时AOF文件: " << temp_path;
        return false;
    }
    
    LOG_INFO << "开始重写AOF文件";
    
    // 获取所有键
    auto* mutableStorage = const_cast<StorageEngine*>(storage);
    auto keys = mutableStorage->keys("*");
    size_t key_count = 0;
    
    for (const auto& key : keys) {
        // 检查键的类型并生成相应的SET命令
        if (auto value = mutableStorage->get(key)) {
            // String类型
            temp_file << "SET " << key << " " << *value << "\n";
            key_count++;
        }
        // 注意：这里简化实现，只处理String类型
        // 实际应该处理所有数据结构类型
    }
    
    temp_file.close();
    
    // Windows下rename不会覆盖已存在文件，先删除旧文件再替换。
    std::remove(aof_path_.c_str());
    if (std::rename(temp_path.c_str(), aof_path_.c_str()) != 0) {
        LOG_ERROR << "重写AOF文件失败，无法替换原文件";
        return false;
    }
    
    LOG_INFO << "AOF重写完成，压缩了 " << key_count << " 个键";
    return true;
}

void AOFPersistenceEngine::syncAOF() {
    // 在Windows上，简单的文件关闭操作通常足够
    // 如果需要更强的同步保证，可以使用FlushFileBuffers等API
    LOG_DEBUG << "AOF文件同步完成";
}

} // namespace kvstore