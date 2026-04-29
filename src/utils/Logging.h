#ifndef KVSTORE_UTILS_LOGGING_H
#define KVSTORE_UTILS_LOGGING_H

#include "kvstore/Types.h"
#include "utils/Timestamp.h"
#include <sstream>
#include <mutex>

namespace kvstore {

enum LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR_LEVEL = 4,
    FATAL = 5,
    NUM_LOG_LEVELS
};

class Logger {
public:
    Logger(const char* file, int line, LogLevel level, const char* func);
    ~Logger();

    std::ostream& stream() { return stream_; }
    const char* getLogLevelString(LogLevel level) const;

private:
    void formatTime();
    const char* basename(const char* file) const;

    std::ostringstream stream_;
    const char* file_;
    const int line_;
    const char* func_;
    LogLevel level_;
};

class LogStream {
public:
    LogStream() = default;
    ~LogStream() = default;

    LogStream& operator<<(bool v) {
        stream_ << (v ? "true" : "false");
        return *this;
    }

    LogStream& operator<<(short v) { stream_ << v; return *this; }
    LogStream& operator<<(unsigned short v) { stream_ << v; return *this; }
    LogStream& operator<<(int v) { stream_ << v; return *this; }
    LogStream& operator<<(unsigned int v) { stream_ << v; return *this; }
    LogStream& operator<<(long v) { stream_ << v; return *this; }
    LogStream& operator<<(unsigned long v) { stream_ << v; return *this; }
    LogStream& operator<<(long long v) { stream_ << v; return *this; }
    LogStream& operator<<(unsigned long long v) { stream_ << v; return *this; }
    LogStream& operator<<(float v) { stream_ << v; return *this; }
    LogStream& operator<<(double v) { stream_ << v; return *this; }

    LogStream& operator<<(const char* v) { if (v) stream_ << v; return *this; }
    LogStream& operator<<(const std::string& v) { stream_ << v; return *this; }

    LogStream& operator<<(const void* p) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%p", p);
        stream_ << buf;
        return *this;
    }

    std::string str() const { return stream_.str(); }

private:
    std::ostringstream stream_;
};

class LoggerImpl {
public:
    static LoggerImpl& instance();

    void setLogLevel(LogLevel level) { logLevel_ = level; }
    LogLevel getLogLevel() const { return logLevel_; }

    void log(const string& message);

private:
    LoggerImpl();
    ~LoggerImpl();

    LogLevel logLevel_;
    std::mutex mutex_;
};

} // namespace kvstore

// LOG宏定义
#define LOG_LEVEL(level) \
    kvstore::Logger(__FILE__, __LINE__, kvstore::level, __func__).stream()

#define LOG_TRACE LOG_LEVEL(TRACE)
#define LOG_DEBUG LOG_LEVEL(DEBUG)
#define LOG_INFO LOG_LEVEL(INFO)
#define LOG_WARN LOG_LEVEL(WARN)
#define LOG_ERROR LOG_LEVEL(ERROR_LEVEL)
#define LOG_FATAL LOG_LEVEL(FATAL)

// 条件日志
#define LOG_IF(level, condition) \
    if (!(condition)) {} \
    else LOG_LEVEL(level)

#endif // KVSTORE_UTILS_LOGGING_H
