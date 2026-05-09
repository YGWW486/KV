#ifndef KVSTORE_NETWORK_CONNECTIONPOOL_H
#define KVSTORE_NETWORK_CONNECTIONPOOL_H

#include "network/Connection.h"
#include "network/InetAddress.h"

#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <functional>

namespace kvstore {

class ConnectionPool {
public:
    using ConnectionCallback = std::function<void(const std::shared_ptr<Connection>&)>;

    ConnectionPool() = default;
    ~ConnectionPool();

    // 添加一个连接到池中，返回 false 表示达到连接上限
    bool addConnection(const std::shared_ptr<Connection>& conn);
    void setMaxConnections(size_t n) { maxConnections_ = n; }
    size_t maxConnections() const { return maxConnections_; }

    // 从池中移除一个连接
    void removeConnection(const std::shared_ptr<Connection>& conn);
    void removeConnection(const std::string& name);

    // 获取连接
    std::shared_ptr<Connection> getConnection(const std::string& name);

    // 获取当前连接数量
    size_t size() const;

    // 遍历所有连接
    void forEachConnection(const ConnectionCallback& callback);

    // 清理所有连接
    void removeAllConnections();

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<Connection>> connections_;
    size_t maxConnections_ = 0; // 0 = unlimited
};

} // namespace kvstore

#endif // KVSTORE_NETWORK_CONNECTIONPOOL_H
