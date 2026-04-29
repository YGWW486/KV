#include <iostream>
#include <string>
#include <filesystem>

#include "storage/MemoryStorageEngine.h"
#include "storage/PersistenceEngine.h"
#include "utils/Logging.h"

using namespace kvstore;

void testAOF() {
    LOG_INFO << "=== 测试AOF持久化 ===";
    
    std::string aof_path = "test.aof";
    
    // 清理可能存在的旧文件
    if (std::filesystem::exists(aof_path)) {
        std::filesystem::remove(aof_path);
    }
    
    // 创建AOF引擎
    auto aof_engine = std::make_unique<AOFPersistenceEngine>(aof_path);
    PersistenceManager aof_manager(std::move(aof_engine));
    
    // 创建存储引擎
    MemoryStorageEngine storage;
    
    // 启动持久化管理器
    aof_manager.start();
    
    // 添加一些数据
    LOG_INFO << "添加测试数据...";
    storage.set("name", "Alice");
    storage.set("age", "25");
    storage.set("city", "Beijing");
    
    // 记录写操作到AOF
    aof_manager.recordCommand("SET name Alice");
    aof_manager.recordCommand("SET age 25");
    aof_manager.recordCommand("SET city Beijing");
    
    // 手动保存
    aof_manager.manualSave(&storage);
    
    // 检查AOF文件是否存在
    if (std::filesystem::exists(aof_path)) {
        LOG_INFO << "AOF文件创建成功，大小: " 
                 << std::filesystem::file_size(aof_path) << " 字节";
    } else {
        LOG_ERROR << "AOF文件创建失败";
    }
    
    // 测试加载
    LOG_INFO << "测试AOF数据加载...";
    MemoryStorageEngine new_storage;
    aof_manager.loadData(&new_storage);
    
    // 验证加载的数据
    if (auto name = new_storage.get("name")) {
        LOG_INFO << "加载的name: " << *name;
    } else {
        LOG_ERROR << "加载name失败";
    }
    
    if (auto age = new_storage.get("age")) {
        LOG_INFO << "加载的age: " << *age;
    } else {
        LOG_ERROR << "加载age失败";
    }
    
    // 停止管理器
    aof_manager.stop();
    
    LOG_INFO << "AOF测试完成";
}

void testRDB() {
    LOG_INFO << "=== 测试RDB持久化 ===";
    
    std::string rdb_path = "test.rdb";
    
    // 清理可能存在的旧文件
    if (std::filesystem::exists(rdb_path)) {
        std::filesystem::remove(rdb_path);
    }
    
    // 创建RDB引擎
    auto rdb_engine = std::make_unique<RDBPersistenceEngine>(rdb_path);
    PersistenceManager rdb_manager(std::move(rdb_engine));
    
    // 创建存储引擎
    MemoryStorageEngine storage;
    
    // 启动持久化管理器
    rdb_manager.start();
    
    // 添加一些数据
    LOG_INFO << "添加测试数据...";
    storage.set("fruit", "apple");
    storage.set("color", "red");
    storage.set("count", "10");
    
    // 手动保存快照
    rdb_manager.manualSave(&storage);
    
    // 检查RDB文件是否存在
    if (std::filesystem::exists(rdb_path)) {
        LOG_INFO << "RDB文件创建成功，大小: " 
                 << std::filesystem::file_size(rdb_path) << " 字节";
    } else {
        LOG_ERROR << "RDB文件创建失败";
    }
    
    // 测试加载
    LOG_INFO << "测试RDB数据加载...";
    MemoryStorageEngine new_storage;
    rdb_manager.loadData(&new_storage);
    
    // 验证加载的数据
    if (auto fruit = new_storage.get("fruit")) {
        LOG_INFO << "加载的fruit: " << *fruit;
    } else {
        LOG_ERROR << "加载fruit失败";
    }
    
    if (auto color = new_storage.get("color")) {
        LOG_INFO << "加载的color: " << *color;
    } else {
        LOG_ERROR << "加载color失败";
    }
    
    // 停止管理器
    rdb_manager.stop();
    
    LOG_INFO << "RDB测试完成";
}

void testPersistenceStatus() {
    LOG_INFO << "=== 测试持久化状态查询 ===";
    
    // 测试AOF状态
    auto aof_engine = std::make_unique<AOFPersistenceEngine>("status_test.aof");
    PersistenceManager aof_manager(std::move(aof_engine));
    
    LOG_INFO << "AOF状态: " << aof_manager.getStatus();
    
    // 测试RDB状态
    auto rdb_engine = std::make_unique<RDBPersistenceEngine>("status_test.rdb");
    PersistenceManager rdb_manager(std::move(rdb_engine));
    
    LOG_INFO << "RDB状态: " << rdb_manager.getStatus();
    
    LOG_INFO << "状态查询测试完成";
}

int main() {
    LoggerImpl::instance().setLogLevel(INFO);
    LOG_INFO << "=== 持久化测试程序开始 ===";
    
    try {
        testAOF();
        testRDB();
        testPersistenceStatus();
        
        LOG_INFO << "=== 所有持久化测试完成 ===";
        
        // 清理测试文件
        std::filesystem::remove("test.aof");
        std::filesystem::remove("test.rdb");
        std::filesystem::remove("status_test.aof");
        std::filesystem::remove("status_test.rdb");
        
    } catch (const std::exception& e) {
        LOG_ERROR << "测试过程中发生异常: " << e.what();
        return 1;
    }
    
    return 0;
}