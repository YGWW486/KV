#ifndef KVSTORE_COMMANDS_COMMANDDISPATCHER_H
#define KVSTORE_COMMANDS_COMMANDDISPATCHER_H

#include "protocol/RESP.h"
#include <memory>
#include <string>

namespace kvstore {

class StorageEngine;
class PersistenceEngine;

class CommandDispatcher {
public:
    CommandDispatcher(StorageEngine* storage, PersistenceEngine* aof = nullptr);

    // 接收已解析的 RESP 命令（通常为 Array），返回 RESP 响应对象
    std::shared_ptr<RESPObject> dispatch(const std::shared_ptr<RESPObject>& request);

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

    StorageEngine* storage_;
    PersistenceEngine* aof_;
};

} // namespace kvstore

#endif // KVSTORE_COMMANDS_COMMANDDISPATCHER_H
