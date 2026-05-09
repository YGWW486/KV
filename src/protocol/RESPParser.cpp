#include "protocol/RESPParser.h"
#include <charconv>
#include <cctype>

namespace kvstore {

size_t RESPParser::findCRLF(const Buffer* buffer) {
    const char* data = buffer->peek();
    size_t len = buffer->readableBytes();

    if (len < 2) {
        return std::string::npos;
    }

    for (size_t i = 0; i + 1 < len; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            return i;
        }
    }
    return std::string::npos;
}

bool RESPParser::readLine(Buffer* buffer, std::string* line) {
    size_t crlf_pos = findCRLF(buffer);
    if (crlf_pos == std::string::npos) {
        return false;
    }
    *line = std::string(buffer->peek(), crlf_pos);
    buffer->retrieve(crlf_pos + 2); // 跳过\r\n
    return true;
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
    size_t crlf_pos = findCRLF(buffer);
    if (crlf_pos == std::string::npos) {
        return ParseResult::Incomplete;
    }

    if (*buffer->peek() != '+') {
        return ParseResult::Error;
    }

    std::string line(buffer->peek() + 1, crlf_pos - 1);
    buffer->retrieve(crlf_pos + 2);
    *out = std::make_shared<RESPSimpleString>(std::move(line));
    return ParseResult::Success;
}

ParseResult RESPParser::parseError(Buffer* buffer, std::shared_ptr<RESPObject>* out) {
    size_t crlf_pos = findCRLF(buffer);
    if (crlf_pos == std::string::npos) {
        return ParseResult::Incomplete;
    }

    if (*buffer->peek() != '-') {
        return ParseResult::Error;
    }

    std::string line(buffer->peek() + 1, crlf_pos - 1);
    buffer->retrieve(crlf_pos + 2);
    *out = std::make_shared<RESPError>(std::move(line));
    return ParseResult::Success;
}

ParseResult RESPParser::parseInteger(Buffer* buffer, std::shared_ptr<RESPObject>* out) {
    size_t crlf_pos = findCRLF(buffer);
    if (crlf_pos == std::string::npos) return ParseResult::Incomplete;
    if (*buffer->peek() != ':') return ParseResult::Error;

    const char* start = buffer->peek() + 1;
    const char* end = start + crlf_pos - 1;
    int64_t value = 0;
    auto [ptr, ec] = std::from_chars(start, end, value);
    if (ec != std::errc() || ptr != end) return ParseResult::Error;
    buffer->retrieve(crlf_pos + 2);
    *out = std::make_shared<RESPInteger>(value);
    return ParseResult::Success;
}

ParseResult RESPParser::parseBulkString(Buffer* buffer, std::shared_ptr<RESPObject>* out) {
    const char* data = buffer->peek();
    size_t len = buffer->readableBytes();
    if (len < 4 || data[0] != '$') {
        return len == 0 ? ParseResult::Incomplete : ParseResult::Error;
    }

    size_t crlf_pos = std::string::npos;
    for (size_t i = 1; i + 1 < len; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            crlf_pos = i;
            break;
        }
    }
    if (crlf_pos == std::string::npos) {
        return ParseResult::Incomplete;
    }

    int64_t bulk_len = 0;
    auto [ptr1, ec1] = std::from_chars(data + 1, data + crlf_pos, bulk_len);
    if (ec1 != std::errc()) return ParseResult::Error;

    const size_t header_len = crlf_pos + 2;
    if (bulk_len == -1) { // NULL Bulk String
        buffer->retrieve(header_len);
        *out = std::make_shared<RESPBulkString>(RESPBulkString::null());
        return ParseResult::Success;
    }

    if (bulk_len < 0) {
        return ParseResult::Error;
    }

    if (buffer->readableBytes() < header_len + static_cast<size_t>(bulk_len) + 2) {
        return ParseResult::Incomplete;
    }

    buffer->retrieve(header_len);
    std::string value(buffer->peek(), static_cast<size_t>(bulk_len));
    buffer->retrieve(static_cast<size_t>(bulk_len) + 2); // 跳过value和\r\n

    *out = std::make_shared<RESPBulkString>(std::move(value));
    return ParseResult::Success;
}

ParseResult RESPParser::parseArray(Buffer* buffer, std::shared_ptr<RESPObject>* out) {
    const char* data = buffer->peek();
    size_t len = buffer->readableBytes();
    if (len < 4 || data[0] != '*') {
        return len == 0 ? ParseResult::Incomplete : ParseResult::Error;
    }

    size_t crlf_pos = std::string::npos;
    for (size_t i = 1; i + 1 < len; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            crlf_pos = i;
            break;
        }
    }
    if (crlf_pos == std::string::npos) {
        return ParseResult::Incomplete;
    }

    int64_t num_elements = 0;
    auto [ptr2, ec2] = std::from_chars(data + 1, data + crlf_pos, num_elements);
    if (ec2 != std::errc()) return ParseResult::Error;

    buffer->retrieve(crlf_pos + 2);
    if (num_elements == -1) { // NULL array
        *out = std::make_shared<RESPArray>(RESPArray::null());
        return ParseResult::Success;
    }

    if (num_elements < -1) {
        return ParseResult::Error;
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
