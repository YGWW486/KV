#include "commands/CommandDispatcher.h"
#include "storage/StorageEngine.h"
#include "storage/PersistenceEngine.h"

#include <cctype>
#include <chrono>
#include <sstream>
#include <unordered_map>
#include <functional>

namespace kvstore {

CommandDispatcher::CommandDispatcher(StorageEngine* storage, PersistenceEngine* aof)
    : storage_(storage), aof_(aof) {
}

void CommandDispatcher::recordAOF(const std::string& cmd, const std::vector<std::string>& argv) {
    if (!aof_) return;
    if (cmd == "PING" || cmd == "GET" || cmd == "STRLEN" || cmd == "EXISTS" ||
        cmd == "LRANGE" || cmd == "LLEN" || cmd == "HGET" || cmd == "HGETALL" ||
        cmd == "HKEYS" || cmd == "HLEN" || cmd == "SMEMBERS" || cmd == "SISMEMBER" ||
        cmd == "SCARD" || cmd == "ZRANGE" || cmd == "ZSCORE" || cmd == "ZRANK" ||
        cmd == "ZCARD" || cmd == "AUTH" || cmd == "TTL" || cmd == "INFO") return;
    aof_->recordWrite(AOFPersistenceEngine::formatAOFLine(argv));
}

std::vector<std::string> CommandDispatcher::flattenArgs(const std::shared_ptr<RESPObject>& request) {
    std::vector<std::string> args;
    if (!request) return args;
    if (request->type() == RESPType::Array) {
        auto arr = std::static_pointer_cast<RESPArray>(request);
        for (const auto& elem : arr->elements()) {
            if (elem->type() == RESPType::BulkString) {
                auto bs = std::static_pointer_cast<RESPBulkString>(elem);
                if (!bs->isNull()) {
                    args.push_back(bs->value());
                }
            }
        }
    }
    return args;
}

std::string CommandDispatcher::extractCommand(const std::shared_ptr<RESPObject>& request) {
    if (!request) return "";
    if (request->type() == RESPType::Array) {
        auto arr = std::static_pointer_cast<RESPArray>(request);
        if (arr->size() > 0 && arr->elements()[0]->type() == RESPType::BulkString) {
            auto cmd = std::static_pointer_cast<RESPBulkString>(arr->elements()[0]);
            if (!cmd->isNull()) {
                std::string upper = cmd->value();
                for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                return upper;
            }
        }
    } else if (request->type() == RESPType::SimpleString) {
        auto ss = std::static_pointer_cast<RESPSimpleString>(request);
        std::string upper = ss->value();
        for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return upper;
    }
    return "";
}

std::shared_ptr<RESPObject> CommandDispatcher::handlePing(const std::vector<std::string>& argv) {
    if (argv.size() <= 1) {
        return std::make_shared<RESPSimpleString>("PONG");
    }
    return std::make_shared<RESPBulkString>(argv[1]);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleSet(const std::vector<std::string>& argv) {
    if (argv.size() < 3) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'SET' command");
    }
    storage_->set(argv[1], argv[2]);
    recordAOF("SET", argv);
    return std::make_shared<RESPSimpleString>("OK");
}

std::shared_ptr<RESPObject> CommandDispatcher::handleGet(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'GET' command");
    }
    auto result = storage_->get(argv[1]);
    if (!result.has_value()) {
        return std::make_shared<RESPBulkString>(RESPBulkString::null());
    }
    return std::make_shared<RESPBulkString>(*result);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleDel(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'DEL' command");
    }
    int64_t count = 0;
    for (size_t i = 1; i < argv.size(); ++i) {
        if (storage_->del(argv[i])) {
            ++count;
        }
    }
    recordAOF("DEL", argv);
    return std::make_shared<RESPInteger>(count);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleIncr(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'INCR' command");
    }
    auto val = storage_->get(argv[1]);
    int64_t n = 0;
    if (val.has_value()) {
        try { n = std::stoll(*val); }
        catch (...) { return std::make_shared<RESPError>("ERR value is not an integer or out of range"); }
    }
    n += 1;
    storage_->set(argv[1], std::to_string(n));
    recordAOF("SET", {argv[0], argv[1], std::to_string(n)});
    return std::make_shared<RESPInteger>(n);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleDecr(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'DECR' command");
    }
    auto val = storage_->get(argv[1]);
    int64_t n = 0;
    if (val.has_value()) {
        try { n = std::stoll(*val); }
        catch (...) { return std::make_shared<RESPError>("ERR value is not an integer or out of range"); }
    }
    n -= 1;
    storage_->set(argv[1], std::to_string(n));
    recordAOF("SET", {argv[0], argv[1], std::to_string(n)});
    return std::make_shared<RESPInteger>(n);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleAppend(const std::vector<std::string>& argv) {
    if (argv.size() < 3) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'APPEND' command");
    }
    auto val = storage_->get(argv[1]);
    std::string s = val.has_value() ? *val : "";
    s += argv[2];
    storage_->set(argv[1], s);
    recordAOF("SET", {argv[0], argv[1], s});
    return std::make_shared<RESPInteger>(static_cast<int64_t>(s.size()));
}

std::shared_ptr<RESPObject> CommandDispatcher::handleStrlen(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'STRLEN' command");
    }
    auto val = storage_->get(argv[1]);
    int64_t len = val.has_value() ? static_cast<int64_t>(val->size()) : 0;
    return std::make_shared<RESPInteger>(len);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleExists(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'EXISTS' command");
    }
    int64_t count = 0;
    for (size_t i = 1; i < argv.size(); ++i) {
        if (storage_->exists(argv[i])) ++count;
    }
    return std::make_shared<RESPInteger>(count);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleLpush(const std::vector<std::string>& argv) {
    if (argv.size() < 3) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'LPUSH' command");
    }
    for (size_t i = 2; i < argv.size(); ++i) {
        storage_->lpush(argv[1], argv[i]);
    }
    recordAOF("LPUSH", argv);
    int64_t len = static_cast<int64_t>(storage_->llen(argv[1]));
    return std::make_shared<RESPInteger>(len);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleRpush(const std::vector<std::string>& argv) {
    if (argv.size() < 3) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'RPUSH' command");
    }
    for (size_t i = 2; i < argv.size(); ++i) {
        storage_->rpush(argv[1], argv[i]);
    }
    recordAOF("RPUSH", argv);
    int64_t len = static_cast<int64_t>(storage_->llen(argv[1]));
    return std::make_shared<RESPInteger>(len);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleLpop(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'LPOP' command");
    }
    auto val = storage_->lpop(argv[1]);
    if (!val.has_value()) return std::make_shared<RESPBulkString>(RESPBulkString::null());
    recordAOF("LPOP", argv);
    return std::make_shared<RESPBulkString>(*val);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleRpop(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'RPOP' command");
    }
    auto val = storage_->rpop(argv[1]);
    if (!val.has_value()) return std::make_shared<RESPBulkString>(RESPBulkString::null());
    recordAOF("RPOP", argv);
    return std::make_shared<RESPBulkString>(*val);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleLrange(const std::vector<std::string>& argv) {
    if (argv.size() < 4) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'LRANGE' command");
    }
    long start = 0, end = 0;
    try { start = std::stol(argv[2]); end = std::stol(argv[3]); }
    catch (...) { return std::make_shared<RESPError>("ERR value is not an integer or out of range"); }
    auto vals = storage_->lrange(argv[1], start, end);
    std::vector<std::shared_ptr<RESPObject>> elements;
    for (const auto& v : vals) {
        elements.push_back(std::make_shared<RESPBulkString>(v));
    }
    return std::make_shared<RESPArray>(std::move(elements));
}

std::shared_ptr<RESPObject> CommandDispatcher::handleLlen(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'LLEN' command");
    }
    int64_t len = static_cast<int64_t>(storage_->llen(argv[1]));
    return std::make_shared<RESPInteger>(len);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleHset(const std::vector<std::string>& argv) {
    if (argv.size() < 4 || (argv.size() - 2) % 2 != 0) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'HSET' command");
    }
    int64_t added = 0;
    for (size_t i = 2; i < argv.size(); i += 2) {
        if (!storage_->hexists(argv[1], argv[i])) ++added;
        storage_->hset(argv[1], argv[i], argv[i + 1]);
    }
    if (added == 0) added = 0; // Redis returns 0 if all fields already existed
    recordAOF("HSET", argv);
    return std::make_shared<RESPInteger>(added);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleHget(const std::vector<std::string>& argv) {
    if (argv.size() < 3) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'HGET' command");
    }
    auto val = storage_->hget(argv[1], argv[2]);
    if (!val.has_value()) return std::make_shared<RESPBulkString>(RESPBulkString::null());
    return std::make_shared<RESPBulkString>(*val);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleHdel(const std::vector<std::string>& argv) {
    if (argv.size() < 3) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'HDEL' command");
    }
    int64_t count = 0;
    for (size_t i = 2; i < argv.size(); ++i) {
        if (storage_->hdel(argv[1], argv[i])) ++count;
    }
    recordAOF("HDEL", argv);
    return std::make_shared<RESPInteger>(count);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleHgetall(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'HGETALL' command");
    }
    auto pairs = storage_->hgetall(argv[1]);
    std::vector<std::shared_ptr<RESPObject>> elements;
    for (const auto& [field, value] : pairs) {
        elements.push_back(std::make_shared<RESPBulkString>(field));
        elements.push_back(std::make_shared<RESPBulkString>(value));
    }
    return std::make_shared<RESPArray>(std::move(elements));
}

std::shared_ptr<RESPObject> CommandDispatcher::handleHkeys(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'HKEYS' command");
    }
    auto fields = storage_->hkeys(argv[1]);
    std::vector<std::shared_ptr<RESPObject>> elements;
    for (const auto& f : fields) {
        elements.push_back(std::make_shared<RESPBulkString>(f));
    }
    return std::make_shared<RESPArray>(std::move(elements));
}

std::shared_ptr<RESPObject> CommandDispatcher::handleHlen(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'HLEN' command");
    }
    return std::make_shared<RESPInteger>(static_cast<int64_t>(storage_->hlen(argv[1])));
}

std::shared_ptr<RESPObject> CommandDispatcher::handleSadd(const std::vector<std::string>& argv) {
    if (argv.size() < 3) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'SADD' command");
    }
    int64_t added = 0;
    for (size_t i = 2; i < argv.size(); ++i) {
        if (storage_->sadd(argv[1], argv[i])) ++added;
    }
    recordAOF("SADD", argv);
    return std::make_shared<RESPInteger>(added);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleSrem(const std::vector<std::string>& argv) {
    if (argv.size() < 3) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'SREM' command");
    }
    int64_t removed = 0;
    for (size_t i = 2; i < argv.size(); ++i) {
        if (storage_->srem(argv[1], argv[i])) ++removed;
    }
    recordAOF("SREM", argv);
    return std::make_shared<RESPInteger>(removed);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleSmembers(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'SMEMBERS' command");
    }
    auto members = storage_->smembers(argv[1]);
    std::vector<std::shared_ptr<RESPObject>> elements;
    for (const auto& m : members) {
        elements.push_back(std::make_shared<RESPBulkString>(m));
    }
    return std::make_shared<RESPArray>(std::move(elements));
}

std::shared_ptr<RESPObject> CommandDispatcher::handleSismember(const std::vector<std::string>& argv) {
    if (argv.size() < 3) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'SISMEMBER' command");
    }
    return std::make_shared<RESPInteger>(storage_->sismember(argv[1], argv[2]) ? 1 : 0);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleScard(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'SCARD' command");
    }
    return std::make_shared<RESPInteger>(static_cast<int64_t>(storage_->scard(argv[1])));
}

std::shared_ptr<RESPObject> CommandDispatcher::handleZadd(const std::vector<std::string>& argv) {
    if (argv.size() < 4 || (argv.size() - 2) % 2 != 0) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'ZADD' command");
    }
    int64_t count = 0;
    for (size_t i = 2; i < argv.size(); i += 2) {
        double score = 0;
        try { score = std::stod(argv[i]); }
        catch (...) { return std::make_shared<RESPError>("ERR value is not a valid float"); }
        storage_->zadd(argv[1], score, argv[i + 1]);
        ++count;
    }
    recordAOF("ZADD", argv);
    return std::make_shared<RESPInteger>(count);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleZrem(const std::vector<std::string>& argv) {
    if (argv.size() < 3) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'ZREM' command");
    }
    int64_t removed = 0;
    for (size_t i = 2; i < argv.size(); ++i) {
        if (storage_->zrem(argv[1], argv[i])) ++removed;
    }
    recordAOF("ZREM", argv);
    return std::make_shared<RESPInteger>(removed);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleZrange(const std::vector<std::string>& argv) {
    if (argv.size() < 4) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'ZRANGE' command");
    }
    long start = 0, end = 0;
    try { start = std::stol(argv[2]); end = std::stol(argv[3]); }
    catch (...) { return std::make_shared<RESPError>("ERR value is not an integer or out of range"); }
    auto pairs = storage_->zrange(argv[1], start, end);
    std::vector<std::shared_ptr<RESPObject>> elements;
    for (const auto& [score, member] : pairs) {
        elements.push_back(std::make_shared<RESPBulkString>(member));
    }
    return std::make_shared<RESPArray>(std::move(elements));
}

std::shared_ptr<RESPObject> CommandDispatcher::handleZscore(const std::vector<std::string>& argv) {
    if (argv.size() < 3) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'ZSCORE' command");
    }
    auto pairs = storage_->zrange(argv[1], 0, -1);
    for (const auto& [score, member] : pairs) {
        if (member == argv[2]) {
            return std::make_shared<RESPBulkString>(std::to_string(score));
        }
    }
    return std::make_shared<RESPBulkString>(RESPBulkString::null());
}

std::shared_ptr<RESPObject> CommandDispatcher::handleZrank(const std::vector<std::string>& argv) {
    if (argv.size() < 3) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'ZRANK' command");
    }
    auto pairs = storage_->zrange(argv[1], 0, -1);
    for (size_t i = 0; i < pairs.size(); ++i) {
        if (pairs[i].second == argv[2]) {
            return std::make_shared<RESPInteger>(static_cast<int64_t>(i));
        }
    }
    return std::make_shared<RESPBulkString>(RESPBulkString::null());
}

std::shared_ptr<RESPObject> CommandDispatcher::handleZcard(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'ZCARD' command");
    }
    return std::make_shared<RESPInteger>(static_cast<int64_t>(storage_->zcard(argv[1])));
}

std::shared_ptr<RESPObject> CommandDispatcher::handleAuth(const std::vector<std::string>& /*argv*/) {
    return std::make_shared<RESPError>("ERR AUTH handled by server layer");
}

std::shared_ptr<RESPObject> CommandDispatcher::handleInfo(const std::vector<std::string>& /*argv*/) {
    if (infoCallback_) {
        return std::make_shared<RESPBulkString>(infoCallback_());
    }
    return std::make_shared<RESPBulkString>("");
}

std::shared_ptr<RESPObject> CommandDispatcher::handleExpire(const std::vector<std::string>& argv) {
    if (argv.size() < 3) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'EXPIRE' command");
    }
    try {
        int64_t seconds = std::stoll(argv[2]);
        if (seconds < 0) seconds = 0;
        bool ok = storage_->expire(argv[1], seconds * 1000);
        recordAOF("EXPIRE", argv);
        return std::make_shared<RESPInteger>(ok ? 1 : 0);
    } catch (...) {
        return std::make_shared<RESPError>("ERR value is not an integer or out of range");
    }
}

std::shared_ptr<RESPObject> CommandDispatcher::handleTTL(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'TTL' command");
    }
    int64_t ms = storage_->ttl(argv[1]);
    if (ms <= 0) return std::make_shared<RESPInteger>(ms); // -1 or -2
    return std::make_shared<RESPInteger>((ms + 999) / 1000); // seconds
}

std::shared_ptr<RESPObject> CommandDispatcher::handlePersist(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'PERSIST' command");
    }
    bool ok = storage_->persist(argv[1]);
    recordAOF("PERSIST", argv);
    return std::make_shared<RESPInteger>(ok ? 1 : 0);
}

std::shared_ptr<RESPObject> CommandDispatcher::handleSlowlog(const std::vector<std::string>& argv) {
    if (argv.size() < 2) {
        return std::make_shared<RESPError>("ERR wrong number of arguments for 'SLOWLOG' command");
    }
    std::string sub = argv[1];
    // Convert to uppercase for comparison
    for (auto& c : sub) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    if (sub == "LEN") {
        return std::make_shared<RESPInteger>(static_cast<int64_t>(slowlog_.size()));
    }
    if (sub == "RESET") {
        resetSlowlog();
        return std::make_shared<RESPSimpleString>("OK");
    }
    if (sub == "GET") {
        int64_t count = 10;
        if (argv.size() >= 3) {
            try { count = std::stoll(argv[2]); } catch (...) {}
        }
        if (count < 0) count = 0;
        size_t n = static_cast<size_t>(count);
        if (n > slowlog_.size()) n = slowlog_.size();

        std::vector<std::shared_ptr<RESPObject>> arr;
        // Return from oldest to newest (like Redis)
        for (size_t i = slowlog_.size() - n; i < slowlog_.size(); ++i) {
            const auto& e = slowlog_[i];
            std::vector<std::shared_ptr<RESPObject>> entry;
            entry.push_back(std::make_shared<RESPInteger>(e.id));
            entry.push_back(std::make_shared<RESPInteger>(e.timestamp_us / 1000000)); // unix sec
            entry.push_back(std::make_shared<RESPInteger>(e.duration_us));
            entry.push_back(std::make_shared<RESPBulkString>(e.command));
            arr.push_back(std::make_shared<RESPArray>(std::move(entry)));
        }
        return std::make_shared<RESPArray>(std::move(arr));
    }
    return std::make_shared<RESPError>("ERR Unknown SLOWLOG subcommand '" + sub + "'");
}

// Static helpers for AUTH gating
std::string CommandDispatcher::extractCommandStatic(const std::shared_ptr<RESPObject>& request) {
    CommandDispatcher dummy(nullptr);
    return dummy.extractCommand(request);
}

std::vector<std::string> CommandDispatcher::flattenArgsStatic(const std::shared_ptr<RESPObject>& request) {
    return CommandDispatcher(nullptr).flattenArgs(request);
}

const std::unordered_map<std::string, CommandDispatcher::CmdHandler> CommandDispatcher::kDispatchTable = {
    {"PING",   &CommandDispatcher::handlePing},
    {"AUTH",   &CommandDispatcher::handleAuth},
    {"SET",    &CommandDispatcher::handleSet},
    {"GET",    &CommandDispatcher::handleGet},
    {"DEL",    &CommandDispatcher::handleDel},
    {"INCR",   &CommandDispatcher::handleIncr},
    {"DECR",   &CommandDispatcher::handleDecr},
    {"APPEND", &CommandDispatcher::handleAppend},
    {"STRLEN", &CommandDispatcher::handleStrlen},
    {"EXISTS", &CommandDispatcher::handleExists},
    {"LPUSH",  &CommandDispatcher::handleLpush},
    {"RPUSH",  &CommandDispatcher::handleRpush},
    {"LPOP",   &CommandDispatcher::handleLpop},
    {"RPOP",   &CommandDispatcher::handleRpop},
    {"LRANGE", &CommandDispatcher::handleLrange},
    {"LLEN",   &CommandDispatcher::handleLlen},
    {"HSET",   &CommandDispatcher::handleHset},
    {"HGET",   &CommandDispatcher::handleHget},
    {"HDEL",   &CommandDispatcher::handleHdel},
    {"HGETALL",&CommandDispatcher::handleHgetall},
    {"HKEYS",  &CommandDispatcher::handleHkeys},
    {"HLEN",   &CommandDispatcher::handleHlen},
    {"SADD",   &CommandDispatcher::handleSadd},
    {"SREM",   &CommandDispatcher::handleSrem},
    {"SMEMBERS",&CommandDispatcher::handleSmembers},
    {"SISMEMBER",&CommandDispatcher::handleSismember},
    {"SCARD",  &CommandDispatcher::handleScard},
    {"ZADD",   &CommandDispatcher::handleZadd},
    {"ZREM",   &CommandDispatcher::handleZrem},
    {"ZRANGE", &CommandDispatcher::handleZrange},
    {"ZSCORE", &CommandDispatcher::handleZscore},
    {"ZRANK",  &CommandDispatcher::handleZrank},
    {"ZCARD",  &CommandDispatcher::handleZcard},
    {"INFO",   &CommandDispatcher::handleInfo},
    {"EXPIRE", &CommandDispatcher::handleExpire},
    {"TTL",    &CommandDispatcher::handleTTL},
    {"PERSIST",&CommandDispatcher::handlePersist},
    {"SLOWLOG",&CommandDispatcher::handleSlowlog},
};

std::shared_ptr<RESPObject> CommandDispatcher::dispatch(const std::shared_ptr<RESPObject>& request) {
    std::string cmd = extractCommand(request);
    if (cmd.empty()) {
        return std::make_shared<RESPError>("ERR invalid request");
    }

    auto args = flattenArgs(request);
    auto t0 = std::chrono::high_resolution_clock::now();

    auto it = kDispatchTable.find(cmd);
    std::shared_ptr<RESPObject> response;
    if (it != kDispatchTable.end()) {
        response = (this->*(it->second))(args);
    } else {
        response = std::make_shared<RESPError>("ERR unknown command '" + cmd + "'");
    }

    if (slowlogThresholdUs_ >= 0) {
        auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - t0).count();
        if (dur >= slowlogThresholdUs_) {
            std::string fullCmd = cmd;
            for (size_t i = 1; i < args.size(); ++i) {
                fullCmd += " ";
                fullCmd += args[i];
            }
            slowlog_.push_back({slowlogId_++, std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count(), dur, fullCmd, ""});
            if (slowlog_.size() > slowlogMaxLen_) {
                slowlog_.erase(slowlog_.begin());
            }
        }
    }

    return response;
}

void CommandDispatcher::setSlowlogConfig(int64_t thresholdUs, size_t maxLen) {
    slowlogThresholdUs_ = thresholdUs;
    slowlogMaxLen_ = maxLen;
    if (slowlog_.size() > slowlogMaxLen_) {
        slowlog_.resize(slowlogMaxLen_);
    }
}

} // namespace kvstore
