#ifndef KVSTORE_STORAGE_PERSISTENCE_ENGINE_H
#define KVSTORE_STORAGE_PERSISTENCE_ENGINE_H

#include <string>
#include <memory>
#include <functional>
#include "StorageEngine.h"

namespace kvstore {

// AOF同步策略
enum class AOFPolicy {
    ALWAYS,     // 每次写操作都同步
    EVERYSEC,   // 每秒同步一次
    NO          // 由操作系统决定
};

// 持久化引擎接口
class PersistenceEngine {
public:
    virtual ~PersistenceEngine() = default;
    
    // 加载持久化数据
    virtual bool load(StorageEngine* storage) = 0;
    
    // 保存数据到持久化存储
    virtual bool save(const StorageEngine* storage) = 0;
    
    // 记录写操作（用于AOF）
    virtual void recordWrite(const std::string& command) = 0;
    
    // 设置持久化策略
    virtual void setPolicy(AOFPolicy policy) = 0;
    
    // 获取当前持久化状态
    virtual std::string getStatus() const = 0;
};

// AOF持久化引擎
class AOFPersistenceEngine : public PersistenceEngine {
public:
    AOFPersistenceEngine(const std::string& aof_path);
    ~AOFPersistenceEngine() override;
    
    bool load(StorageEngine* storage) override;
    bool save(const StorageEngine* storage) override;
    void recordWrite(const std::string& command) override;
    void setPolicy(AOFPolicy policy) override;
    std::string getStatus() const override;
    
private:
    std::string aof_path_;
    AOFPolicy policy_;
    
    // 重写AOF文件（压缩）
    bool rewriteAOF(const StorageEngine* storage);
    
    // 同步AOF文件
    void syncAOF();
};

// RDB持久化引擎
class RDBPersistenceEngine : public PersistenceEngine {
public:
    RDBPersistenceEngine(const std::string& rdb_path);
    ~RDBPersistenceEngine() override;
    
    bool load(StorageEngine* storage) override;
    bool save(const StorageEngine* storage) override;
    void recordWrite(const std::string& command) override;
    void setPolicy(AOFPolicy policy) override;
    std::string getStatus() const override;
    
    // RDB特有方法
    bool saveSnapshot(const StorageEngine* storage);
    bool loadSnapshot(StorageEngine* storage);
    
private:
    std::string rdb_path_;
    
    // 保存字符串数据
    bool saveStringData(const StorageEngine* storage, std::ostream& os);
    
    // 保存哈希数据
    bool saveHashData(const StorageEngine* storage, std::ostream& os);
    
    // 保存列表数据
    bool saveListData(const StorageEngine* storage, std::ostream& os);
    
    // 保存集合数据
    bool saveSetData(const StorageEngine* storage, std::ostream& os);
    
    // 保存有序集合数据
    bool saveSortedSetData(const StorageEngine* storage, std::ostream& os);
};

// 持久化管理器
class PersistenceManager {
public:
    PersistenceManager(std::unique_ptr<PersistenceEngine> engine);
    
    // 启动持久化
    bool start();
    
    // 停止持久化
    bool stop();
    
    // 记录写操作
    void recordCommand(const std::string& command);
    
    // 手动保存
    bool manualSave(const StorageEngine* storage);
    
    // 加载数据
    bool loadData(StorageEngine* storage);
    
    // 获取状态
    std::string getStatus() const;
    
private:
    std::unique_ptr<PersistenceEngine> engine_;
    bool running_;
};

} // namespace kvstore

#endif // KVSTORE_STORAGE_PERSISTENCE_ENGINE_H