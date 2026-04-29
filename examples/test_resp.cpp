#include <iostream>
#include <string>
#include <memory>

#include "network/Buffer.h"
#include "protocol/RESP.h"
#include "protocol/RESPParser.h"
#include "utils/Logging.h"

using namespace kvstore;

void testSimpleString() {
    LOG_INFO << "=== 测试 SimpleString ===";
    
    RESPSimpleString str("OK");
    LOG_INFO << "  编码: " << str.toString();
    
    Buffer buffer;
    buffer.append(str.toString());
    
    RESPParser parser;
    std::shared_ptr<RESPObject> out;
    ParseResult res = parser.parse(&buffer, &out);
    
    if (res == ParseResult::Success) {
        LOG_INFO << "  解析成功！类型: " << (int)out->type();
        LOG_INFO << "  内容: " << out->toString();
    }
}

void testBulkString() {
    LOG_INFO << "=== 测试 BulkString ===";
    
    RESPBulkString str("Hello World");
    LOG_INFO << "  编码: " << str.toString();
    
    Buffer buffer;
    buffer.append(str.toString());
    
    RESPParser parser;
    std::shared_ptr<RESPObject> out;
    ParseResult res = parser.parse(&buffer, &out);
    
    if (res == ParseResult::Success) {
        LOG_INFO << "  解析成功！类型: " << (int)out->type();
        LOG_INFO << "  内容: " << out->toString();
    }
}

void testInteger() {
    LOG_INFO << "=== 测试 Integer ===";
    
    RESPInteger num(12345);
    LOG_INFO << "  编码: " << num.toString();
    
    Buffer buffer;
    buffer.append(num.toString());
    
    RESPParser parser;
    std::shared_ptr<RESPObject> out;
    ParseResult res = parser.parse(&buffer, &out);
    
    if (res == ParseResult::Success) {
        LOG_INFO << "  解析成功！类型: " << (int)out->type();
        LOG_INFO << "  内容: " << out->toString();
    }
}

void testArray() {
    LOG_INFO << "=== 测试 Array ===";
    
    std::vector<std::shared_ptr<RESPObject>> elems;
    elems.push_back(std::make_shared<RESPBulkString>("SET"));
    elems.push_back(std::make_shared<RESPBulkString>("key"));
    elems.push_back(std::make_shared<RESPBulkString>("value"));
    RESPArray arr(std::move(elems));
    
    LOG_INFO << "  编码: " << arr.toString();
    
    Buffer buffer;
    buffer.append(arr.toString());
    
    RESPParser parser;
    std::shared_ptr<RESPObject> out;
    ParseResult res = parser.parse(&buffer, &out);
    
    if (res == ParseResult::Success) {
        LOG_INFO << "  解析成功！类型: " << (int)out->type();
        LOG_INFO << "  内容: " << out->toString();
    }
}

int main() {
    LoggerImpl::instance().setLogLevel(INFO);
    LOG_INFO << "=== RESP测试程序开始 ===";
    
    testSimpleString();
    testBulkString();
    testInteger();
    testArray();
    
    LOG_INFO << "=== RESP测试程序结束 ===";
    return 0;
}
