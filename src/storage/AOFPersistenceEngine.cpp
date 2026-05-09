#include "storage/PersistenceEngine.h"
#include "storage/StorageEngine.h"
#include "utils/Logging.h"
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <mutex>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <io.h>
#define fsync _commit
#define fdatasync _commit
#else
#include <unistd.h>
#endif

namespace kvstore {

AOFPersistenceEngine::AOFPersistenceEngine(const std::string& aof_path)
    : aof_path_(aof_path), policy_(AOFPolicy::EVERYSEC) {
    LOG_INFO << "AOF持久化引擎初始化，文件路径: " << aof_path_;
    startBgThread();
}

AOFPersistenceEngine::~AOFPersistenceEngine() {
    stopBgThread();
    closeAOFFile();
    LOG_INFO << "AOF持久化引擎销毁";
}

void AOFPersistenceEngine::openAOFFile() {
    if (aof_fp_) return;
#ifdef _WIN32
    aof_fp_ = _fsopen(aof_path_.c_str(), "a", _SH_DENYNO);
#else
    aof_fp_ = std::fopen(aof_path_.c_str(), "a");
#endif
    if (!aof_fp_) {
        LOG_ERROR << "无法打开AOF文件: " << aof_path_;
        return;
    }
#ifdef _WIN32
    aof_fd_ = _fileno(aof_fp_);
#else
    aof_fd_ = fileno(aof_fp_);
#endif
    // Disable stdio buffering — we manage flush ourselves
    std::setvbuf(aof_fp_, nullptr, _IONBF, 0);
}

void AOFPersistenceEngine::closeAOFFile() {
    if (aof_fp_) {
        std::fclose(aof_fp_);
        aof_fp_ = nullptr;
        aof_fd_ = -1;
    }
}

std::string AOFPersistenceEngine::formatAOFLine(const std::vector<std::string>& argv) {
    std::ostringstream oss;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i > 0) oss << ' ';
        const auto& arg = argv[i];
        bool needs_quote = arg.empty() ||
            std::any_of(arg.begin(), arg.end(), [](char c) {
                return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '"' || c == '\\';
            });
        if (needs_quote) {
            oss << '"';
            for (char c : arg) {
                if (c == '"' || c == '\\') oss << '\\';
                oss << c;
            }
            oss << '"';
        } else {
            oss << arg;
        }
    }
    return oss.str();
}

static std::vector<std::string> splitArgs(const std::string& s) {
    std::vector<std::string> args;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') ++i;
        if (i >= s.size()) break;
        if (s[i] == '"') {
            ++i;
            std::string arg;
            while (i < s.size()) {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    arg += s[i + 1];
                    i += 2;
                } else if (s[i] == '"') {
                    ++i;
                    break;
                } else {
                    arg += s[i++];
                }
            }
            args.push_back(std::move(arg));
        } else {
            size_t end = i;
            while (end < s.size() && s[end] != ' ') ++end;
            args.push_back(s.substr(i, end - i));
            i = end;
        }
    }
    return args;
}

bool AOFPersistenceEngine::load(StorageEngine* storage) {
    if (!storage) {
        LOG_ERROR << "存储引擎为空，无法加载AOF数据";
        return false;
    }

    std::ifstream file(aof_path_);
    if (!file.is_open()) {
        LOG_INFO << "AOF文件不存在，跳过加载: " << aof_path_;
        return true;
    }

    LOG_INFO << "开始加载AOF文件: " << aof_path_;

    std::string line;
    size_t line_count = 0;

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto args = splitArgs(line);
        if (args.empty()) continue;

        const std::string& cmd = args[0];
        bool ok = true;

        if (cmd == "SET" && args.size() >= 3) {
            ok = storage->set(args[1], args[2]);
        } else if (cmd == "DEL" && args.size() >= 2) {
            ok = storage->del(args[1]);
        } else if (cmd == "HSET" && args.size() >= 4) {
            ok = storage->hset(args[1], args[2], args[3]);
        } else if (cmd == "HDEL" && args.size() >= 3) {
            ok = storage->hdel(args[1], args[2]);
        } else if (cmd == "RPUSH" && args.size() >= 3) {
            for (size_t i = 2; i < args.size(); ++i)
                ok = storage->rpush(args[1], args[i]) && ok;
        } else if (cmd == "LPUSH" && args.size() >= 3) {
            for (size_t i = 2; i < args.size(); ++i)
                ok = storage->lpush(args[1], args[i]) && ok;
        } else if (cmd == "LPOP" && args.size() >= 2) {
            storage->lpop(args[1]);
        } else if (cmd == "RPOP" && args.size() >= 2) {
            storage->rpop(args[1]);
        } else if (cmd == "SADD" && args.size() >= 3) {
            ok = storage->sadd(args[1], args[2]);
        } else if (cmd == "SREM" && args.size() >= 3) {
            ok = storage->srem(args[1], args[2]);
        } else if (cmd == "ZADD" && args.size() >= 4) {
            double score = 0.0;
            try { score = std::stod(args[2]); } catch (...) { ok = false; }
            if (ok) ok = storage->zadd(args[1], score, args[3]);
        } else if (cmd == "ZREM" && args.size() >= 3) {
            ok = storage->zrem(args[1], args[2]);
        } else if (cmd == "INCR" && args.size() >= 2) {
            auto val = storage->get(args[1]);
            int64_t n = val.has_value() ? std::stoll(*val) : 0;
            storage->set(args[1], std::to_string(n + 1));
        } else if (cmd == "DECR" && args.size() >= 2) {
            auto val = storage->get(args[1]);
            int64_t n = val.has_value() ? std::stoll(*val) : 0;
            storage->set(args[1], std::to_string(n - 1));
        } else if (cmd == "APPEND" && args.size() >= 3) {
            auto val = storage->get(args[1]);
            std::string s = val.has_value() ? *val : "";
            s += args[2];
            storage->set(args[1], s);
        } else {
            continue;
        }

        if (!ok) {
            LOG_ERROR << "加载AOF数据失败: " << line;
            return false;
        }
        ++line_count;
    }

    file.close();
    LOG_INFO << "AOF文件加载完成，处理了 " << line_count << " 条命令";
    return true;
}

bool AOFPersistenceEngine::save(const StorageEngine* storage) {
    return rewriteAOF(storage);
}

void AOFPersistenceEngine::recordWrite(const std::string& command) {
    if (command.empty()) {
        LOG_WARN << "记录空命令到AOF";
        return;
    }

    openAOFFile();
    if (!aof_fp_) return;

    std::fwrite(command.c_str(), 1, command.size(), aof_fp_);
    std::fwrite("\n", 1, 1, aof_fp_);
    std::fflush(aof_fp_);

    if (policy_ == AOFPolicy::ALWAYS) {
        syncAOF();
    }

    LOG_DEBUG << "记录命令到AOF: " << command;
}

void AOFPersistenceEngine::setPolicy(AOFPolicy policy) {
    if (policy_ == AOFPolicy::EVERYSEC && policy != AOFPolicy::EVERYSEC) {
        stopBgThread();
    }
    policy_ = policy;
    if (policy == AOFPolicy::EVERYSEC) {
        startBgThread();
    }
    LOG_INFO << "设置AOF同步策略: " << static_cast<int>(policy);
}

std::string AOFPersistenceEngine::getStatus() const {
    std::string status = "AOF持久化引擎 - 策略: ";

    switch (policy_) {
        case AOFPolicy::ALWAYS: status += "ALWAYS"; break;
        case AOFPolicy::EVERYSEC: status += "EVERYSEC"; break;
        case AOFPolicy::NO: status += "NO"; break;
    }

    // Use stat to get file size without opening
    std::ifstream file(aof_path_, std::ios::ate | std::ios::binary);
    if (file.is_open()) {
        status += ", 文件大小: " + std::to_string(file.tellg()) + " 字节";
        file.close();
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

    // Close the live AOF file handle before rewriting
    closeAOFFile();

    std::string temp_path = aof_path_ + ".tmp";
    std::ofstream temp_file(temp_path);

    if (!temp_file.is_open()) {
        LOG_ERROR << "无法创建临时AOF文件: " << temp_path;
        openAOFFile();
        return false;
    }

    LOG_INFO << "开始重写AOF文件";

    auto* ms = const_cast<StorageEngine*>(storage);
    auto keys = ms->keys("*");
    size_t key_count = 0;

    for (const auto& key : keys) {
        KeyType type = ms->getType(key);
        switch (type) {
        case KeyType::String: {
            if (auto value = ms->get(key)) {
                temp_file << formatAOFLine({"SET", key, *value}) << "\n";
                ++key_count;
            }
            break;
        }
        case KeyType::Hash: {
            for (const auto& [field, value] : ms->hgetall(key)) {
                temp_file << formatAOFLine({"HSET", key, field, value}) << "\n";
            }
            ++key_count;
            break;
        }
        case KeyType::List: {
            auto elements = ms->lrange(key, 0, -1);
            for (const auto& elem : elements) {
                temp_file << formatAOFLine({"RPUSH", key, elem}) << "\n";
            }
            ++key_count;
            break;
        }
        case KeyType::Set: {
            for (const auto& member : ms->smembers(key)) {
                temp_file << formatAOFLine({"SADD", key, member}) << "\n";
            }
            ++key_count;
            break;
        }
        case KeyType::ZSet: {
            auto members = ms->zrange(key, 0, -1);
            for (const auto& [score, member] : members) {
                temp_file << formatAOFLine({"ZADD", key, std::to_string(score), member}) << "\n";
            }
            ++key_count;
            break;
        }
        case KeyType::None:
            break;
        }
    }

    temp_file.close();

    std::remove(aof_path_.c_str());
    if (std::rename(temp_path.c_str(), aof_path_.c_str()) != 0) {
        LOG_ERROR << "重写AOF文件失败，无法替换原文件";
        openAOFFile();
        return false;
    }

    openAOFFile();

    LOG_INFO << "AOF重写完成，压缩了 " << key_count << " 个键";
    return true;
}

void AOFPersistenceEngine::syncAOF() {
    std::lock_guard<std::mutex> lock(fd_mutex_);
    if (aof_fd_ >= 0) {
#ifdef _WIN32
        _commit(aof_fd_);
#else
        fdatasync(aof_fd_);
#endif
    }
}

void AOFPersistenceEngine::startBgThread() {
    if (thread_running_) return;
    thread_running_ = true;
    aof_thread_ = std::thread([this]() {
        LOG_INFO << "AOF background sync thread started (EVERYSEC)";
        while (thread_running_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!thread_running_) break;
            syncAOF();
        }
        LOG_INFO << "AOF background sync thread stopped";
    });
}

void AOFPersistenceEngine::stopBgThread() {
    if (!thread_running_) return;
    thread_running_ = false;
    if (aof_thread_.joinable()) {
        aof_thread_.join();
    }
}

void AOFPersistenceEngine::rewriteAOFAsync(const StorageEngine* storage) {
    if (rewriting_.exchange(true)) {
        LOG_WARN << "AOF rewrite already in progress, skipping";
        return;
    }
    LOG_INFO << "Starting async AOF rewrite...";
    rewrite_thread_ = std::thread([this, storage]() {
        std::string temp_path = aof_path_ + ".tmp";
        // Do the heavy work: scan storage, write compacted commands
        {
            std::ofstream temp_file(temp_path);
            if (!temp_file.is_open()) {
                LOG_ERROR << "Async rewrite: cannot create temp file " << temp_path;
                rewriting_ = false;
                return;
            }
            auto* ms = const_cast<StorageEngine*>(storage);
            auto keys = ms->keys("*");
            for (const auto& key : keys) {
                KeyType type = ms->getType(key);
                switch (type) {
                case KeyType::String: {
                    if (auto value = ms->get(key)) {
                        temp_file << formatAOFLine({"SET", key, *value}) << "\n";
                    }
                    break;
                }
                case KeyType::Hash: {
                    for (const auto& [field, value] : ms->hgetall(key)) {
                        temp_file << formatAOFLine({"HSET", key, field, value}) << "\n";
                    }
                    break;
                }
                case KeyType::List: {
                    for (const auto& elem : ms->lrange(key, 0, -1)) {
                        temp_file << formatAOFLine({"RPUSH", key, elem}) << "\n";
                    }
                    break;
                }
                case KeyType::Set: {
                    for (const auto& member : ms->smembers(key)) {
                        temp_file << formatAOFLine({"SADD", key, member}) << "\n";
                    }
                    break;
                }
                case KeyType::ZSet: {
                    for (const auto& [score, member] : ms->zrange(key, 0, -1)) {
                        temp_file << formatAOFLine({"ZADD", key, std::to_string(score), member}) << "\n";
                    }
                    break;
                }
                case KeyType::None: break;
                }
            }
        } // temp_file closed
        // Atomic swap under fd_mutex_
        {
            std::lock_guard<std::mutex> lock(fd_mutex_);
            closeAOFFile();
            std::remove(aof_path_.c_str());
            if (std::rename(temp_path.c_str(), aof_path_.c_str()) != 0) {
                LOG_ERROR << "Async rewrite: rename failed";
            }
            openAOFFile();
        }
        rewriting_ = false;
        LOG_INFO << "Async AOF rewrite completed";
    });
    rewrite_thread_.detach();
}

} // namespace kvstore
