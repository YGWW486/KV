#ifndef KVSTORE_PROTOCOL_RESPPARSER_H
#define KVSTORE_PROTOCOL_RESPPARSER_H

#include "network/Buffer.h"
#include "protocol/RESP.h"
#include <memory>

namespace kvstore {

// RESP解析结果
enum class ParseResult {
    Success,         // 解析成功
    Incomplete,      // 数据不完整，需要更多数据
    Error            // 解析错误
};

class RESPParser {
public:
    RESPParser() = default;
    ~RESPParser() = default;
    
    // 从Buffer中解析一个RESP对象
    ParseResult parse(Buffer* buffer, std::shared_ptr<RESPObject>* out);
    
private:
    // 解析各种类型的辅助方法
    ParseResult parseSimpleString(Buffer* buffer, std::shared_ptr<RESPObject>* out);
    ParseResult parseError(Buffer* buffer, std::shared_ptr<RESPObject>* out);
    ParseResult parseInteger(Buffer* buffer, std::shared_ptr<RESPObject>* out);
    ParseResult parseBulkString(Buffer* buffer, std::shared_ptr<RESPObject>* out);
    ParseResult parseArray(Buffer* buffer, std::shared_ptr<RESPObject>* out);
    
    // 读取一行（到\r\n为止）
    bool readLine(Buffer* buffer, std::string* line);
    // 查找\r\n的位置
    size_t findCRLF(const Buffer* buffer);
};

} // namespace kvstore

#endif // KVSTORE_PROTOCOL_RESPPARSER_H
