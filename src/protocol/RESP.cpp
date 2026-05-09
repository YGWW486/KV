#include "protocol/RESP.h"
#include <string>

namespace kvstore {

static std::string intToStr(int64_t n) {
    if (n == 0) return "0";
    std::string s;
    s.reserve(20);
    bool neg = n < 0;
    if (neg) n = -n;
    while (n > 0) {
        s.push_back(static_cast<char>('0' + (n % 10)));
        n /= 10;
    }
    if (neg) s.push_back('-');
    std::reverse(s.begin(), s.end());
    return s;
}

static std::string uintToStr(size_t n) {
    if (n == 0) return "0";
    std::string s;
    s.reserve(20);
    while (n > 0) {
        s.push_back(static_cast<char>('0' + (n % 10)));
        n /= 10;
    }
    std::reverse(s.begin(), s.end());
    return s;
}

// === RESPSimpleString ===
RESPSimpleString::RESPSimpleString(std::string value)
    : value_(std::move(value)) {}

std::string RESPSimpleString::toString() const {
    std::string r;
    r.reserve(3 + value_.size());
    r += '+';
    r += value_;
    r += "\r\n";
    return r;
}

// === RESPError ===
RESPError::RESPError(std::string message)
    : message_(std::move(message)) {}

std::string RESPError::toString() const {
    std::string r;
    r.reserve(3 + message_.size());
    r += '-';
    r += message_;
    r += "\r\n";
    return r;
}

// === RESPInteger ===
RESPInteger::RESPInteger(int64_t value)
    : value_(value) {}

std::string RESPInteger::toString() const {
    auto num = intToStr(value_);
    std::string r;
    r.reserve(3 + num.size());
    r += ':';
    r += num;
    r += "\r\n";
    return r;
}

// === RESPBulkString ===
RESPBulkString::RESPBulkString(std::string value)
    : value_(std::move(value)), isNull_(false) {}

RESPBulkString RESPBulkString::null() {
    RESPBulkString value("");
    value.isNull_ = true;
    return value;
}

std::string RESPBulkString::toString() const {
    if (isNull_) return "$-1\r\n";
    auto len = uintToStr(value_.size());
    std::string r;
    r.reserve(1 + len.size() + 2 + value_.size() + 2);
    r += '$';
    r += len;
    r += "\r\n";
    r += value_;
    r += "\r\n";
    return r;
}

// === RESPArray ===
RESPArray::RESPArray(std::vector<std::shared_ptr<RESPObject>> elements)
    : elements_(std::move(elements)), isNull_(false) {}

RESPArray RESPArray::null() {
    RESPArray array(std::vector<std::shared_ptr<RESPObject>>{});
    array.isNull_ = true;
    return array;
}

std::string RESPArray::toString() const {
    if (isNull_) return "*-1\r\n";
    auto cnt = uintToStr(elements_.size());
    std::string r;
    r.reserve(1 + cnt.size() + 2);
    r += '*';
    r += cnt;
    r += "\r\n";
    for (const auto& elem : elements_) {
        r += elem->toString();
    }
    return r;
}

} // namespace kvstore
