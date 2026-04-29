#include "utils/Logging.h"
#include "utils/Timestamp.h"
#include "utils/Config.h"

#include <iostream>

using namespace kvstore;

int main() {
    LoggerImpl::instance().setLogLevel(DEBUG);
    
    LOG_INFO << "========================================";
    LOG_INFO << "  KV-Store Server v0.1.0";
    LOG_INFO << "  Starting...";
    LOG_INFO << "========================================";
    
    Timestamp start = Timestamp::now();
    LOG_DEBUG << "Current time: " << start.toFormattedString();
    
    Config config;
    config.set("server.port", "6379");
    config.set("server.name", "KV-Store");
    
    LOG_INFO << "Server name: " << config.get<string>("server.name", "Unknown");
    LOG_INFO << "Server port: " << config.get<int>("server.port", 6379);
    
    Timestamp end = Timestamp::now();
    double elapsed = timeDifference(end, start);
    
    LOG_INFO << "========================================";
    LOG_INFO << "  Initialization complete!";
    LOG_INFO << "  Elapsed time: " << elapsed << "s";
    LOG_INFO << "========================================";
    
    return 0;
}
