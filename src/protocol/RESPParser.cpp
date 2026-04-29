#include "protocol/RESPParser.h"
#include <sstream>
#include <cctype>

namespace kvstore {

size_t RESPParser::findCRLF(const Buffer* buffer) {
    const char* data = buffer->peek();
    size_t len = buffer->readableBytes();
    
    for (size_t i = 0; i < len - 1; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            return i;
        }
    }
    return std::string::npos;
}

std::string RESPParser::readLine(Buffer* buffer) {
    size_t crlf_pos = findCRLF(buffer);
    if (crlf_pos == std::string::npos) {
        return ""; // 数据不完整
    }
    std::string line(buffer->peek(), crlf_pos);
    buffer->retrieve(crlf_pos + 2); // 跳过\r\n
    return line;
}

ParseResult RESPParser::parse(Buffer* buffer, std::shared_ptr<RESPObject>* out) {
    if (buffer->readableBytes() == 0) {
        return ParseResult::Incomplete;
    }
    
    char type = *buffer->peek();
    
    switch (type) {
        case '+':
            return parseSimpleString(buffer, out);
        case '-':
            return parseError(buffer, out);
        case ':':
            return parseInteger(buffer, out);
        case '$':
            return parseBulkString(buffer, out);
        case '*':
            return parseArray(buffer, out);
        default:
            return ParseResult::Error;
    }
}

ParseResult RESPParser::parseSimpleString(Buffer* buffer, std::shared_ptr<RESPObject>* out) {
    buffer->retrieve(1); // 跳过 '+'
    std::string line = readLine(buffer);
    if (line.empty()) {
        return ParseResult::Incomplete;
    }
    
    *out = std::make_shared<RESPSimpleString>(std::move(line));
    return ParseResult::Success;
}

ParseResult RESPParser::parseError(Buffer* buffer, std::shared_ptr<RESPObject>* out) {
    buffer->retrieve(1); // 跳过 '-'
    std::string line = readLine(buffer);
    if (line.empty()) {
        return ParseResult::Incomplete;
    }
    
    *out = std::make_shared<RESPError>(std::move(line));
    return ParseResult::Success;
}

ParseResult RESPParser::parseInteger(Buffer* buffer, std::shared_ptr<RESPObject>* out) {
    buffer->retrieve(1); // 跳过 ':'
    std::string line = readLine(buffer);
    if (line.empty()) {
        return ParseResult::Incomplete;
    }
    
    int64_t value = std::stoll(line);
    *out = std::make_shared<RESPInteger>(value);
    return ParseResult::Success;
}

ParseResult RESPParser::parseBulkString(Buffer* buffer, std::shared_ptr<RESPObject>* out) {
    buffer->retrieve(1); // 跳过 '$'
    std::string line = readLine(buffer);
    if (line.empty()) {
        return ParseResult::Incomplete;
    }
    
    int64_t len = std::stoll(line);
    if (len == -1) { // 特殊情况：NULL Bulk String
        // 这里简化处理，先返回空字符串
        *out = std::make_shared<RESPBulkString>("");
        return ParseResult::Success;
    }
    
    if (buffer->readableBytes() < (size_t)len + 2) { // 加上\r\n
        return ParseResult::Incomplete;
    }
    
    std::string value(buffer->peek(), len);
    buffer->retrieve(len + 2); // 跳过value和\r\n
    
    *out = std::make_shared<RESPBulkString>(std::move(value));
    return ParseResult::Success;
}

ParseResult RESPParser::parseArray(Buffer* buffer, std::shared_ptr<RESPObject>* out) {
    buffer->retrieve(1); // 跳过 '*'
    std::string line = readLine(buffer);
    if (line.empty()) {
        return ParseResult::Incomplete;
    }
    
    int64_t num_elements = std::stoll(line);
    if (num_elements == -1) { // NULL array
        *out = std::make_shared<RESPArray>(std::vector<std::shared_ptr<RESPObject>>());
        return ParseResult::Success;
    }
    
    std::vector<std::shared_ptr<RESPObject>> elements;
    for (int64_t i = 0; i < num_elements; ++i) {
        std::shared_ptr<RESPObject> elem;
        ParseResult res = parse(buffer, &elem);
        if (res != ParseResult::Success) {
            return res;
        }
        elements.push_back(std::move(elem));
    }
    
    *out = std::make_shared<RESPArray>(std::move(elements));
    return ParseResult::Success;
}

} // namespace kvstore
