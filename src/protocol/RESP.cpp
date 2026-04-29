#include "protocol/RESP.h"
#include <sstream>
#include <iomanip>

namespace kvstore {

// === RESPSimpleString ===
RESPSimpleString::RESPSimpleString(std::string value) 
    : value_(std::move(value)) {}

std::string RESPSimpleString::toString() const {
    std::ostringstream oss;
    oss << "+" << value_ << "\r\n";
    return oss.str();
}

// === RESPError ===
RESPError::RESPError(std::string message) 
    : message_(std::move(message)) {}

std::string RESPError::toString() const {
    std::ostringstream oss;
    oss << "-" << message_ << "\r\n";
    return oss.str();
}

// === RESPInteger ===
RESPInteger::RESPInteger(int64_t value) 
    : value_(value) {}

std::string RESPInteger::toString() const {
    std::ostringstream oss;
    oss << ":" << value_ << "\r\n";
    return oss.str();
}

// === RESPBulkString ===
RESPBulkString::RESPBulkString(std::string value) 
    : value_(std::move(value)) {}

std::string RESPBulkString::toString() const {
    std::ostringstream oss;
    oss << "$" << value_.size() << "\r\n" << value_ << "\r\n";
    return oss.str();
}

// === RESPArray ===
RESPArray::RESPArray(std::vector<std::shared_ptr<RESPObject>> elements) 
    : elements_(std::move(elements)) {}

std::string RESPArray::toString() const {
    std::ostringstream oss;
    oss << "*" << elements_.size() << "\r\n";
    for (const auto& elem : elements_) {
        oss << elem->toString();
    }
    return oss.str();
}

} // namespace kvstore
