#include "network/ConnectionPool.h"
#include "utils/Logging.h"

#include <vector>

namespace kvstore {

void ConnectionPool::addConnection(const std::shared_ptr<Connection>& conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string& name = conn->name();
    connections_[name] = conn;
    LOG_INFO << "Connection added: " << name << ", total: " << connections_.size();
}

void ConnectionPool::removeConnection(const std::shared_ptr<Connection>& conn) {
    removeConnection(conn->name());
}

void ConnectionPool::removeConnection(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connections_.find(name);
    if (it != connections_.end()) {
        connections_.erase(it);
        LOG_INFO << "Connection removed: " << name << ", total: " << connections_.size();
    }
}

std::shared_ptr<Connection> ConnectionPool::getConnection(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connections_.find(name);
    if (it != connections_.end()) {
        return it->second;
    }
    return nullptr;
}

size_t ConnectionPool::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}

void ConnectionPool::forEachConnection(const ConnectionCallback& callback) {
    std::vector<std::shared_ptr<Connection>> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot.reserve(connections_.size());
        for (auto& pair : connections_) {
            snapshot.push_back(pair.second);
        }
    }
    for (const auto& conn : snapshot) {
        callback(conn);
    }
}

void ConnectionPool::removeAllConnections() {
    std::vector<std::shared_ptr<Connection>> conns;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        LOG_INFO << "Removing all " << connections_.size() << " connections";
        for (auto& pair : connections_) {
            conns.push_back(pair.second);
        }
        connections_.clear();
    }
    for (auto& conn : conns) {
        conn->shutdown();
    }
}

} // namespace kvstore
