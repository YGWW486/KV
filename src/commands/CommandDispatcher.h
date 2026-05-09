#ifndef KVSTORE_COMMANDS_COMMANDDISPATCHER_H
#define KVSTORE_COMMANDS_COMMANDDISPATCHER_H

#include "protocol/RESP.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace kvstore {

class StorageEngine;
class PersistenceEngine;

struct SlowLogEntry {
    int64_t id;
    int64_t timestamp_us;
    int64_t duration_us;
    std::string command;
    std::string client_ip;
};

class CommandDispatcher {
public:
    CommandDispatcher(StorageEngine* storage, PersistenceEngine* aof = nullptr);

    // 接收已解析的 RESP 命令（通常为 Array），返回 RESP 响应对象
    std::shared_ptr<RESPObject> dispatch(const std::shared_ptr<RESPObject>& request);

    // Static helpers for AUTH gating
    static std::string extractCommandStatic(const std::shared_ptr<RESPObject>& request);
    static std::vector<std::string> flattenArgsStatic(const std::shared_ptr<RESPObject>& request);

    // INFO callback
    using InfoCallback = std::function<std::string()>;
    void setInfoCallback(InfoCallback cb) { infoCallback_ = std::move(cb); }

    // Slow log
    void setSlowlogConfig(int64_t thresholdUs, size_t maxLen);
    size_t getSlowlogCount() const { return slowlog_.size(); }
    const std::vector<SlowLogEntry>& slowlogEntries() const { return slowlog_; }
    void resetSlowlog() { slowlog_.clear(); slowlogId_ = 0; }

private:
    std::string extractCommand(const std::shared_ptr<RESPObject>& request);

    // argv[0] = 已转为大写的命令名
    std::shared_ptr<RESPObject> handlePing(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleSet(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleGet(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleDel(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleIncr(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleDecr(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleAppend(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleStrlen(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleExists(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleLpush(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleRpush(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleLpop(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleRpop(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleLrange(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleLlen(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleHset(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleHget(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleHdel(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleHgetall(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleHkeys(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleHlen(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleSadd(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleSrem(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleSmembers(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleSismember(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleScard(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleZadd(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleZrem(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleZrange(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleZscore(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleZrank(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleZcard(const std::vector<std::string>& argv);

    // 将 RESP 命令数组展平为 string 列表
    std::vector<std::string> flattenArgs(const std::shared_ptr<RESPObject>& request);

    void recordAOF(const std::string& cmd, const std::vector<std::string>& argv);

    using CmdHandler = std::shared_ptr<RESPObject> (CommandDispatcher::*)(const std::vector<std::string>&);
    static const std::unordered_map<std::string, CmdHandler> kDispatchTable;

    std::shared_ptr<RESPObject> handleAuth(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleInfo(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleExpire(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleTTL(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handlePersist(const std::vector<std::string>& argv);
    std::shared_ptr<RESPObject> handleSlowlog(const std::vector<std::string>& argv);

    StorageEngine* storage_;
    PersistenceEngine* aof_;
    InfoCallback infoCallback_;

    // Slow log
    std::vector<SlowLogEntry> slowlog_;
    int64_t slowlogId_ = 0;
    size_t slowlogMaxLen_ = 128;
    int64_t slowlogThresholdUs_ = 10000; // 10ms default
};

} // namespace kvstore

#endif // KVSTORE_COMMANDS_COMMANDDISPATCHER_H
