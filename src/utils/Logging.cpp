#include "utils/Logging.h"
#include <cstdlib>
#include <iostream>
#include <cstdio>

namespace kvstore {

LoggerImpl& LoggerImpl::instance() {
    static LoggerImpl logger;
    return logger;
}

LoggerImpl::LoggerImpl()
    : logLevel_(INFO)
{
}

LoggerImpl::~LoggerImpl() {
}

void LoggerImpl::log(const string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << message << std::endl;
}

Logger::Logger(const char* file, int line, LogLevel level, const char* func)
    : file_(file), line_(line), func_(func), level_(level)
{
    if (level < LoggerImpl::instance().getLogLevel()) {
        return;
    }
    formatTime();
    stream_ << " [" << getLogLevelString(level) << "] "
            << basename(file_) << ":" << line_ << " - "
            << func_ << " ";
}

Logger::~Logger() {
    if (level_ >= LoggerImpl::instance().getLogLevel()) {
        stream_ << "\n";
        LoggerImpl::instance().log(stream_.str());
    }
    if (level_ == FATAL) {
        std::abort();
    }
}

const char* Logger::getLogLevelString(LogLevel level) const {
    static const char* logLevelNames[NUM_LOG_LEVELS] = {
        "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
    };
    if (level >= 0 && level < NUM_LOG_LEVELS) {
        return logLevelNames[level];
    }
    return "UNKNOWN";
}

void Logger::formatTime() {
    Timestamp now = Timestamp::now();
    stream_ << now.toFormattedString();
}

const char* Logger::basename(const char* file) const {
    const char* slash = strrchr(file, '/');
    if (!slash) {
        slash = strrchr(file, '\\');
    }
    return slash ? slash + 1 : file;
}

} // namespace kvstore
