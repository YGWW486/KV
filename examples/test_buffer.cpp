#include "network/Buffer.h"
#include "utils/Logging.h"
#include <iostream>

using namespace kvstore;

int main() {
    LoggerImpl::instance().setLogLevel(DEBUG);
    
    LOG_INFO << "=== Buffer 测试 ===";
    
    Buffer buf;
    
    LOG_INFO << "初始状态: readable=" << buf.readableBytes() 
             << ", writable=" << buf.writableBytes()
             << ", prependable=" << buf.prependableBytes();
    
    // 测试 append
    std::string msg1 = "Hello, ";
    buf.append(msg1);
    LOG_INFO << "append(\"" << msg1 << "\"): readable=" << buf.readableBytes();
    
    std::string msg2 = "KV-Store!";
    buf.append(msg2);
    LOG_INFO << "append(\"" << msg2 << "\"): readable=" << buf.readableBytes();
    
    // 测试 retrieveAsString
    std::string result = buf.retrieveAllAsString();
    LOG_INFO << "retrieveAllAsString(): \"" << result << "\"";
    
    // 测试 prepend
    buf.append("World");
    buf.prependInt32(42);
    LOG_INFO << "append(\"World\") + prependInt32(42): readable=" << buf.readableBytes();
    
    int32_t x = buf.readInt32();
    LOG_INFO << "readInt32(): " << x << ", remaining: \"" << buf.retrieveAllAsString() << "\"";
    
    // 测试 CRLF 查找
    Buffer buf2;
    buf2.append("Line1\r\nLine2\r\nEnd");
    const char* crlf = buf2.findCRLF();
    if (crlf) {
        LOG_INFO << "findCRLF(): found at position " << (crlf - buf2.peek());
        LOG_INFO << "  first line: \"" << buf2.retrieveAsString(crlf - buf2.peek()) << "\"";
    }
    
    LOG_INFO << "=== Buffer 测试完成 ===";
    
    return 0;
}
