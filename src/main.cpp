#include "network/Connection.h"
#include "network/ConnectionPool.h"
#include "network/InetAddress.h"
#include "protocol/RESPParser.h"
#include "commands/CommandDispatcher.h"
#include "storage/SegmentedMemoryStorageEngine.h"
#include "storage/PersistenceEngine.h"
#include "utils/Logging.h"
#include "utils/Config.h"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

static void parseCommandLine(int argc, char* argv[], kvstore::Config& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            config.set("server.port", argv[++i]);
        } else if (arg == "--config" && i + 1 < argc) {
            config.load(argv[++i]);
        } else if (arg == "--requirepass" && i + 1 < argc) {
            config.set("server.requirepass", argv[++i]);
        } else if (arg == "--maxmemory" && i + 1 < argc) {
            config.set("server.maxmemory", argv[++i]);
        } else if (arg == "--aof-policy" && i + 1 < argc) {
            config.set("aof.policy", argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            printf("kv-server v0.2.0\n\n");
            printf("Usage: kv-server [options]\n\n");
            printf("Options:\n");
            printf("  -p, --port PORT          Listen port (default: 6379)\n");
            printf("  --config FILE            Config file path\n");
            printf("  --requirepass PASSWORD   Require AUTH password\n");
            printf("  --maxmemory BYTES        Maximum memory before LRU eviction\n");
            printf("  --aof-policy POLICY      AOF policy: always/everysec/no\n");
            printf("  -h, --help               Show this help\n");
            exit(0);
        }
    }
}

#if defined(_WIN32)

#include "network/iocp/IOCPLoop.h"
#include <winsock2.h>
#include <ws2tcpip.h>

using namespace kvstore;

static IOCPLoop* g_loop = nullptr;

static BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT) {
        if (g_loop) {
            g_loop->quit();
        }
        return TRUE;
    }
    return FALSE;
}

class KvServer {
public:
    KvServer(IOCPLoop* loop, uint16_t port, Config& config)
        : loop_(loop)
        , aof_("kvstore.aof")
        , rdb_("kvstore.rdb")
        , dispatcher_(&storage_, &aof_)
        , requirepass_(config.get<string>("server.requirepass", ""))
        , authEnabled_(!requirepass_.empty())
        , maxmemory_(config.get<size_t>("server.maxmemory", 0))
        , startTime_(std::chrono::steady_clock::now()) {
        rdb_.load(&storage_);
        aof_.load(&storage_);

        std::string aofPolicy = config.get<string>("aof.policy", "");
        if (aofPolicy == "always") aof_.setPolicy(AOFPolicy::ALWAYS);
        else if (aofPolicy == "no") aof_.setPolicy(AOFPolicy::NO);

        size_t maxClients = config.get<size_t>("server.maxclients", 0);
        if (maxClients > 0) connPool_.setMaxConnections(maxClients);

        dispatcher_.setInfoCallback([this]() { return getServerInfo(); });

        int64_t slowlogThreshold = config.get<int64_t>("slowlog.threshold.us", 10000);
        size_t slowlogMaxLen = config.get<size_t>("slowlog.max.len", 128);
        dispatcher_.setSlowlogConfig(slowlogThreshold, slowlogMaxLen);

        listenSock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSock_ == INVALID_SOCKET) {
            throw std::runtime_error("KvServer: socket failed: " + std::to_string(WSAGetLastError()));
        }

        u_long mode = 1;
        if (ioctlsocket(listenSock_, FIONBIO, &mode) != 0) {
            int err = WSAGetLastError();
            closesocket(listenSock_);
            listenSock_ = INVALID_SOCKET;
            throw std::runtime_error("KvServer: ioctlsocket FIONBIO failed: " + std::to_string(err));
        }

        int on = 1;
        setsockopt(listenSock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        if (bind(listenSock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            int err = WSAGetLastError();
            closesocket(listenSock_);
            listenSock_ = INVALID_SOCKET;
            throw std::runtime_error("KvServer: bind failed: " + std::to_string(err));
        }
        if (::listen(listenSock_, SOMAXCONN) == SOCKET_ERROR) {
            int err = WSAGetLastError();
            closesocket(listenSock_);
            listenSock_ = INVALID_SOCKET;
            throw std::runtime_error("KvServer: listen failed: " + std::to_string(err));
        }
    }

    ~KvServer() {
        if (listenSock_ != INVALID_SOCKET) {
            closesocket(listenSock_);
            listenSock_ = INVALID_SOCKET;
        }
    }

    void start() {
        loop_->runEvery(0.05, [this]() { acceptLoop(); });
        loop_->runEvery(1.0, [this]() { aof_.syncAOF(); });
        loop_->runEvery(30.0, [this]() { rdb_.save(&storage_); });
        loop_->runEvery(0.1, [this]() { storage_.evictExpired(20); });
        loop_->runEvery(1.0, [this]() { storage_.tickLRUClock(); });
        loop_->runEvery(0.1, [this]() {
            if (maxmemory_ > 0) {
                size_t used = storage_.getMemoryUsage();
                if (used > maxmemory_) {
                    storage_.evictLRU(used - maxmemory_ + maxmemory_ / 10);
                }
            }
        });
    }

private:
    void acceptLoop() {
        if (listenSock_ == INVALID_SOCKET) {
            return;
        }
        // Accept all pending connections in one batch
        while (true) {
            sockaddr_in addr{};
            int addrlen = sizeof(addr);
            SOCKET connfd = ::accept(listenSock_, reinterpret_cast<sockaddr*>(&addr), &addrlen);
            if (connfd == INVALID_SOCKET) {
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK) break;
                if (err != WSAECONNRESET) {
                    LOG_WARN << "Accept error: " << err;
                }
                break;
            }

            std::string connName = "KV-" + std::to_string(nextConnId_);
            InetAddress peerAddr(addr);
            InetAddress localAddr;
            LOG_INFO << "New connection [" << connName << "] from " << peerAddr.toIpPort();

            auto conn = std::make_shared<Connection>(loop_, connName, connfd, localAddr, peerAddr);
            if (!connPool_.addConnection(conn)) {
                const char* err = "-ERR max number of clients reached\r\n";
                ::send(connfd, err, static_cast<int>(strlen(err)), 0);
                closesocket(connfd);
                continue;
            }

            conn->setConnectionCallback(
                std::bind(&KvServer::onConnection, this, std::placeholders::_1));
            conn->setMessageCallback(
                std::bind(&KvServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

            conn->connectEstablished();
            nextConnId_++;
        }
    }

    void onConnection(const std::shared_ptr<Connection>& conn) {
        if (conn->connected()) {
            LOG_INFO << "Connection [" << conn->name() << "] UP";
        } else {
            LOG_INFO << "Connection [" << conn->name() << "] DOWN";
            connPool_.removeConnection(conn);
        }
    }

    void onMessage(const std::shared_ptr<Connection>& conn, Buffer* buf, Timestamp) {
        while (buf->readableBytes() > 0) {
            std::shared_ptr<RESPObject> request;
            ParseResult result = parser_.parse(buf, &request);

            if (result == ParseResult::Incomplete) {
                return;
            }

            if (result == ParseResult::Error) {
                LOG_ERROR << "Parse error from [" << conn->name() << "], closing";
                conn->send(RESPError("ERR protocol error").toString());
                conn->shutdown();
                return;
            }

            // AUTH check
            if (authEnabled_ && !conn->isAuthenticated()) {
                std::string cmd = CommandDispatcher::extractCommandStatic(request);
                if (cmd == "AUTH") {
                    auto args = CommandDispatcher::flattenArgsStatic(request);
                    if (args.size() >= 2 && args[1] == requirepass_) {
                        conn->setAuthenticated(true);
                        conn->send("+OK\r\n");
                    } else {
                        conn->send(RESPError("ERR invalid password").toString());
                    }
                } else {
                    conn->send(RESPError("NOAUTH Authentication required.").toString());
                }
                continue;
            }

            std::shared_ptr<RESPObject> response = dispatcher_.dispatch(request);
            conn->send(response->toString());
        }
    }

public:
    void shutdown() {
        LOG_INFO << "KV-Store shutting down...";
        aof_.syncAOF();
        rdb_.save(&storage_);
        connPool_.removeAllConnections();
        LOG_INFO << "AOF synced, RDB saved, connections closed.";
    }

    std::string getServerInfo() const {
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();
        std::ostringstream oss;
        oss << "# Server\r\n";
        oss << "kvstore_version:0.2.0\r\n";
        oss << "uptime_in_seconds:" << uptime << "\r\n";
        oss << "\r\n# Clients\r\n";
        oss << "connected_clients:" << connPool_.size() << "\r\n";
        oss << "\r\n# Memory\r\n";
        oss << "used_memory:" << storage_.getMemoryUsage() << "\r\n";
        oss << "maxmemory:" << maxmemory_ << "\r\n";
        oss << "\r\n# Keyspace\r\n";
        oss << storage_.getKeyspaceInfo() << "\r\n";
        oss << "\r\n# Slowlog\r\n";
        oss << "slowlog_entries:" << dispatcher_.getSlowlogCount() << "\r\n";
        oss << "\r\n# Persistence\r\n";
        oss << "aof_enabled:1\r\n";
        oss << aof_.getStatus() << "\r\n";
        return oss.str();
    }

    IOCPLoop* loop_;
    SOCKET listenSock_ = INVALID_SOCKET;
    ConnectionPool connPool_;
    RESPParser parser_;
    SegmentedMemoryStorageEngine storage_;
    AOFPersistenceEngine aof_;
    RDBPersistenceEngine rdb_;
    CommandDispatcher dispatcher_;
    int nextConnId_ = 1;
    std::string requirepass_;
    bool authEnabled_ = false;
    size_t maxmemory_ = 0;
    std::chrono::steady_clock::time_point startTime_;
};

int main(int argc, char* argv[]) {
    LoggerImpl::instance().setLogLevel(INFO);

    SetConsoleCtrlHandler(consoleHandler, TRUE);

    Config config;
    config.set("server.port", "6379");
    parseCommandLine(argc, argv, config);
    int port = config.get<int>("server.port", 6379);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_FATAL << "WSAStartup failed";
        return 1;
    }

    LOG_INFO << "========================================";
    LOG_INFO << "  KV-Store Server v0.2.0 (Windows/IOCP)";
    LOG_INFO << "========================================";

    try {
        IOCPLoop loop;
        KvServer server(&loop, static_cast<uint16_t>(port), config);
        g_loop = &loop;
        server.start();

        LOG_INFO << "KV-Store listening on port " << port;

        loop.loop();
        server.shutdown();
    } catch (const std::exception& e) {
        LOG_ERROR << e.what();
        g_loop = nullptr;
        WSACleanup();
        return 1;
    }

    g_loop = nullptr;
    WSACleanup();
    LOG_INFO << "KV-Store stopped.";
    return 0;
}

#elif defined(__linux__)

#include "network/epoll/EpollLoop.h"
#include "network/Acceptor.h"

#include <signal.h>
#include <sys/socket.h>

using namespace kvstore;

static EpollLoop* g_loop = nullptr;

static void onSignal(int /*sig*/) {
    if (g_loop) {
        g_loop->quit();
    }
}

class KvServer {
public:
    KvServer(EpollLoop* loop, uint16_t port, Config& config)
        : loop_(loop)
        , acceptor_(loop, InetAddress(port), true)
        , aof_("kvstore.aof")
        , rdb_("kvstore.rdb")
        , dispatcher_(&storage_, &aof_)
        , requirepass_(config.get<string>("server.requirepass", ""))
        , authEnabled_(!requirepass_.empty())
        , maxmemory_(config.get<size_t>("server.maxmemory", 0))
        , startTime_(std::chrono::steady_clock::now()) {
        rdb_.load(&storage_);
        aof_.load(&storage_);
        acceptor_.setNewConnectionCallback(
            std::bind(&KvServer::onNewConnection, this, std::placeholders::_1, std::placeholders::_2));

        std::string aofPolicy = config.get<string>("aof.policy", "");
        if (aofPolicy == "always") aof_.setPolicy(AOFPolicy::ALWAYS);
        else if (aofPolicy == "no") aof_.setPolicy(AOFPolicy::NO);

        size_t maxClients = config.get<size_t>("server.maxclients", 0);
        if (maxClients > 0) connPool_.setMaxConnections(maxClients);

        dispatcher_.setInfoCallback([this]() { return getServerInfo(); });

        int64_t slowlogThreshold = config.get<int64_t>("slowlog.threshold.us", 10000);
        size_t slowlogMaxLen = config.get<size_t>("slowlog.max.len", 128);
        dispatcher_.setSlowlogConfig(slowlogThreshold, slowlogMaxLen);
    }

    void start() {
        acceptor_.listen();
        loop_->runEvery(1.0, [this]() { aof_.syncAOF(); });
        loop_->runEvery(30.0, [this]() { rdb_.save(&storage_); });
        loop_->runEvery(0.1, [this]() { storage_.evictExpired(20); });
        loop_->runEvery(1.0, [this]() { storage_.tickLRUClock(); });
        loop_->runEvery(0.1, [this]() {
            if (maxmemory_ > 0) {
                size_t used = storage_.getMemoryUsage();
                if (used > maxmemory_) {
                    storage_.evictLRU(used - maxmemory_ + maxmemory_ / 10);
                }
            }
        });
    }

private:
    void onNewConnection(socket_t sockfd, const InetAddress& peerAddr) {
        std::string connName = "KV-" + std::to_string(nextConnId_);
        InetAddress localAddr;
        sockaddr_in la{};
        socklen_t len = sizeof(la);
        if (::getsockname(sockfd, reinterpret_cast<sockaddr*>(&la), &len) == 0) {
            localAddr.setSockAddrInet(la);
        }
        LOG_INFO << "New connection [" << connName << "] from " << peerAddr.toIpPort();

        auto conn = std::make_shared<Connection>(loop_, connName, sockfd, localAddr, peerAddr);
        if (!connPool_.addConnection(conn)) {
            const char* err = "-ERR max number of clients reached\r\n";
            ::send(sockfd, err, strlen(err), 0);
            ::close(sockfd);
            return;
        }

        conn->setConnectionCallback(
            std::bind(&KvServer::onConnection, this, std::placeholders::_1));
        conn->setMessageCallback(
            std::bind(&KvServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        conn->connectEstablished();
        nextConnId_++;
    }

    void onConnection(const std::shared_ptr<Connection>& conn) {
        if (conn->connected()) {
            LOG_INFO << "Connection [" << conn->name() << "] UP";
        } else {
            LOG_INFO << "Connection [" << conn->name() << "] DOWN";
            connPool_.removeConnection(conn);
        }
    }

    void onMessage(const std::shared_ptr<Connection>& conn, Buffer* buf, Timestamp) {
        while (buf->readableBytes() > 0) {
            auto t0 = std::chrono::high_resolution_clock::now();

            std::shared_ptr<RESPObject> request;
            ParseResult result = parser_.parse(buf, &request);
            auto t1 = std::chrono::high_resolution_clock::now();

            if (result == ParseResult::Incomplete) {
                return;
            }

            if (result == ParseResult::Error) {
                LOG_ERROR << "Parse error from [" << conn->name() << "], closing";
                conn->send(RESPError("ERR protocol error").toString());
                conn->shutdown();
                return;
            }

            // AUTH check
            if (authEnabled_ && !conn->isAuthenticated()) {
                std::string cmd = CommandDispatcher::extractCommandStatic(request);
                if (cmd == "AUTH") {
                    auto args = CommandDispatcher::flattenArgsStatic(request);
                    if (args.size() >= 2 && args[1] == requirepass_) {
                        conn->setAuthenticated(true);
                        conn->send("+OK\r\n");
                    } else {
                        conn->send(RESPError("ERR invalid password").toString());
                    }
                } else {
                    conn->send(RESPError("NOAUTH Authentication required.").toString());
                }
                continue;
            }

            std::shared_ptr<RESPObject> response = dispatcher_.dispatch(request);
            auto t2 = std::chrono::high_resolution_clock::now();

            std::string wire = response->toString();
            auto t3 = std::chrono::high_resolution_clock::now();

            conn->send(wire);
            auto t4 = std::chrono::high_resolution_clock::now();

            // accumulate timing (in nanoseconds)
            parse_ns_   += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            dispatch_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
            serialize_ns_+= std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count();
            send_ns_    += std::chrono::duration_cast<std::chrono::nanoseconds>(t4 - t3).count();
            req_count_++;

            if (req_count_ % 10000 == 0) {
                double div = static_cast<double>(req_count_);
                LOG_INFO << "=== PERF BREAKDOWN (n=" << req_count_ << ") ===";
                LOG_INFO << "  parse:    " << (parse_ns_ / div / 1000.0) << " us/req";
                LOG_INFO << "  dispatch: " << (dispatch_ns_ / div / 1000.0) << " us/req";
                LOG_INFO << "  serialize:" << (serialize_ns_ / div / 1000.0) << " us/req";
                LOG_INFO << "  send:     " << (send_ns_ / div / 1000.0) << " us/req";
                LOG_INFO << "  TOTAL:    " << ((parse_ns_+dispatch_ns_+serialize_ns_+send_ns_) / div / 1000.0) << " us/req";
            }
        }
    }

public:
    void shutdown() {
        LOG_INFO << "KV-Store shutting down...";
        aof_.syncAOF();
        rdb_.save(&storage_);
        connPool_.removeAllConnections();
        LOG_INFO << "AOF synced, RDB saved, connections closed.";
    }

    std::string getServerInfo() const {
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();
        std::ostringstream oss;
        oss << "# Server\r\n";
        oss << "kvstore_version:0.2.0\r\n";
        oss << "uptime_in_seconds:" << uptime << "\r\n";
        oss << "\r\n# Clients\r\n";
        oss << "connected_clients:" << connPool_.size() << "\r\n";
        oss << "\r\n# Memory\r\n";
        oss << "used_memory:" << storage_.getMemoryUsage() << "\r\n";
        oss << "maxmemory:" << maxmemory_ << "\r\n";
        oss << "\r\n# Keyspace\r\n";
        oss << storage_.getKeyspaceInfo() << "\r\n";
        oss << "\r\n# Slowlog\r\n";
        oss << "slowlog_entries:" << dispatcher_.getSlowlogCount() << "\r\n";
        oss << "\r\n# Persistence\r\n";
        oss << "aof_enabled:1\r\n";
        oss << aof_.getStatus() << "\r\n";
        return oss.str();
    }

    EpollLoop* loop_;
    Acceptor acceptor_;
    ConnectionPool connPool_;
    RESPParser parser_;
    SegmentedMemoryStorageEngine storage_;
    AOFPersistenceEngine aof_;
    RDBPersistenceEngine rdb_;
    CommandDispatcher dispatcher_;
    int nextConnId_ = 1;
    std::string requirepass_;
    bool authEnabled_ = false;
    size_t maxmemory_ = 0;
    std::chrono::steady_clock::time_point startTime_;

    // perf counters
    int64_t req_count_ = 0;
    int64_t parse_ns_ = 0;
    int64_t dispatch_ns_ = 0;
    int64_t serialize_ns_ = 0;
    int64_t send_ns_ = 0;
};

int main(int argc, char* argv[]) {
    LoggerImpl::instance().setLogLevel(INFO);

    ::signal(SIGINT, onSignal);
    ::signal(SIGTERM, onSignal);
    ::signal(SIGPIPE, SIG_IGN);

    Config config;
    config.set("server.port", "6379");
    parseCommandLine(argc, argv, config);
    int port = config.get<int>("server.port", 6379);

    LOG_INFO << "========================================";
    LOG_INFO << "  KV-Store Server v0.2.0 (Linux/epoll)";
    LOG_INFO << "========================================";

    try {
        EpollLoop loop;
        g_loop = &loop;
        KvServer server(&loop, static_cast<uint16_t>(port), config);
        server.start();

        LOG_INFO << "KV-Store listening on port " << port;

        loop.loop();
        server.shutdown();
    } catch (const std::exception& e) {
        LOG_ERROR << e.what();
        g_loop = nullptr;
        return 1;
    }

    g_loop = nullptr;
    LOG_INFO << "KV-Store stopped.";
    return 0;
}

#else

#error "kv-server is only supported on Windows or Linux"

#endif
