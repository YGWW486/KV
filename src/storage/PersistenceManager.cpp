#include "PersistenceManager.h"
#include "../utils/Logging.h"
#include <thread>
#include <chrono>

namespace kvstore {

PersistenceManager::PersistenceManager(std::unique_ptr<PersistenceEngine> engine)
    : engine_(std::move(engine)), running_(false) {
    LOG_INFO << "持久化管理器初始化";
}

bool PersistenceManager::start() {
    if (running_) {
        LOG_WARNING << "持久化管理器已经在运行";
        return true;
    }
    
    running_ = true;
    LOG_INFO << "持久化管理器启动";
    
    // 启动后台线程进行定期保存（如果是RDB）
    // 这里可以添加定时保存逻辑
    
    return true;
}

bool PersistenceManager::stop() {
    if (!running_) {
        LOG_WARNING << "持久化管理器已经停止";
        return true;
    }
    
    running_ = false;
    LOG_INFO << "持久化管理器停止";
    
    // 停止后台线程
    
    return true;
}

void PersistenceManager::recordCommand(const std::string& command) {
    if (!running_) {
        LOG_WARNING << "持久化管理器未运行，忽略命令记录";
        return;
    }
    
    if (engine_) {
        engine_->recordWrite(command);
    }
}

bool PersistenceManager::manualSave(const StorageEngine* storage) {
    if (!running_) {
        LOG_ERROR << "持久化管理器未运行，无法手动保存";
        return false;
    }
    
    if (!engine_) {
        LOG_ERROR << "持久化引擎为空，无法保存";
        return false;
    }
    
    LOG_INFO << "开始手动保存数据";
    bool result = engine_->save(storage);
    
    if (result) {
        LOG_INFO << "手动保存完成";
    } else {
        LOG_ERROR << "手动保存失败";
    }
    
    return result;
}

bool PersistenceManager::loadData(StorageEngine* storage) {
    if (!engine_) {
        LOG_ERROR << "持久化引擎为空，无法加载数据";
        return false;
    }
    
    LOG_INFO << "开始加载持久化数据";
    bool result = engine_->load(storage);
    
    if (result) {
        LOG_INFO << "数据加载完成";
    } else {
        LOG_ERROR << "数据加载失败";
    }
    
    return result;
}

std::string PersistenceManager::getStatus() const {
    if (!engine_) {
        return "持久化管理器 - 引擎未设置";
    }
    
    std::string status = "持久化管理器 - ";
    status += running_ ? "运行中" : "已停止";
    status += ", " + engine_->getStatus();
    
    return status;
}

} // namespace kvstore