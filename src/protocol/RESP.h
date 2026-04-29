#ifndef KVSTORE_PROTOCOL_RESP_H
#define KVSTORE_PROTOCOL_RESP_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace kvstore {

// RESP协议类型
enum class RESPType {
    SimpleString, // +
    Error,        // -
    Integer,      // :
    BulkString,   // $
    Array         // *
};

// RESP对象基类
class RESPObject {
public:
    virtual ~RESPObject() = default;
    virtual RESPType type() const = 0;
    virtual std::string toString() const = 0;
};

// 简单字符串类型
class RESPSimpleString : public RESPObject {
public:
    explicit RESPSimpleString(std::string value);
    
    RESPType type() const override { return RESPType::SimpleString; }
    std::string toString() const override;
    const std::string& value() const { return value_; }
    
private:
    std::string value_;
};

// 错误类型
class RESPError : public RESPObject {
public:
    explicit RESPError(std::string message);
    
    RESPType type() const override { return RESPType::Error; }
    std::string toString() const override;
    const std::string& message() const { return message_; }
    
private:
    std::string message_;
};

// 整数类型
class RESPInteger : public RESPObject {
public:
    explicit RESPInteger(int64_t value);
    
    RESPType type() const override { return RESPType::Integer; }
    std::string toString() const override;
    int64_t value() const { return value_; }
    
private:
    int64_t value_;
};

// 批量字符串类型
class RESPBulkString : public RESPObject {
public:
    explicit RESPBulkString(std::string value);
    static RESPBulkString null();
    
    RESPType type() const override { return RESPType::BulkString; }
    std::string toString() const override;
    const std::string& value() const { return value_; }
    bool isNull() const { return isNull_; }
    
private:
    std::string value_;
    bool isNull_;
};

// 数组类型
class RESPArray : public RESPObject {
public:
    explicit RESPArray(std::vector<std::shared_ptr<RESPObject>> elements);
    static RESPArray null();
    
    RESPType type() const override { return RESPType::Array; }
    std::string toString() const override;
    const std::vector<std::shared_ptr<RESPObject>>& elements() const { return elements_; }
    size_t size() const { return elements_.size(); }
    bool isNull() const { return isNull_; }
    
private:
    std::vector<std::shared_ptr<RESPObject>> elements_;
    bool isNull_;
};

} // namespace kvstore

#endif // KVSTORE_PROTOCOL_RESP_H
