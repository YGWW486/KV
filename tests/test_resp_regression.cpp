#include "network/Buffer.h"
#include "protocol/RESP.h"
#include "protocol/RESPParser.h"
#include "utils/Logging.h"

#include <memory>

using namespace kvstore;

namespace {

bool check(bool condition, const char* message) {
    if (!condition) {
        LOG_ERROR << message;
        return false;
    }
    return true;
}

bool testIncompleteBulkStringPreservesBuffer() {
    Buffer buffer;
    buffer.append("$5\r\nhel");

    RESPParser parser;
    std::shared_ptr<RESPObject> out;
    ParseResult res = parser.parse(&buffer, &out);

    if (!check(res == ParseResult::Incomplete, "incomplete bulk string should return Incomplete")) {
        return false;
    }

    if (!check(buffer.retrieveAllAsString() == "$5\r\nhel", "incomplete parse should not consume input")) {
        return false;
    }

    return true;
}

bool testNullBulkStringRoundTrip() {
    Buffer buffer;
    buffer.append("$-1\r\n");

    RESPParser parser;
    std::shared_ptr<RESPObject> out;
    ParseResult res = parser.parse(&buffer, &out);

    if (!check(res == ParseResult::Success, "null bulk string should parse successfully")) {
        return false;
    }

    if (!check(out->toString() == "$-1\r\n", "null bulk string should round-trip as $-1")) {
        return false;
    }

    return true;
}

bool testNullArrayRoundTrip() {
    Buffer buffer;
    buffer.append("*-1\r\n");

    RESPParser parser;
    std::shared_ptr<RESPObject> out;
    ParseResult res = parser.parse(&buffer, &out);

    if (!check(res == ParseResult::Success, "null array should parse successfully")) {
        return false;
    }

    if (!check(out->toString() == "*-1\r\n", "null array should round-trip as *-1")) {
        return false;
    }

    return true;
}

bool testPipelineKeepsRemainingData() {
    Buffer buffer;
    buffer.append("+OK\r\n:42\r\n");

    RESPParser parser;
    std::shared_ptr<RESPObject> first;
    std::shared_ptr<RESPObject> second;

    ParseResult res1 = parser.parse(&buffer, &first);
    if (!check(res1 == ParseResult::Success, "first pipelined response should parse successfully")) {
        return false;
    }

    ParseResult res2 = parser.parse(&buffer, &second);
    if (!check(res2 == ParseResult::Success, "second pipelined response should parse successfully")) {
        return false;
    }

    if (!check(buffer.readableBytes() == 0, "pipeline parse should consume all data")) {
        return false;
    }

    return true;
}

bool testNestedArrayRoundTrip() {
    Buffer buffer;
    buffer.append("*2\r\n*2\r\n+OK\r\n:1\r\n$5\r\nhello\r\n");

    RESPParser parser;
    std::shared_ptr<RESPObject> out;
    ParseResult res = parser.parse(&buffer, &out);

    if (!check(res == ParseResult::Success, "nested array should parse successfully")) {
        return false;
    }

    if (!check(out->toString() == "*2\r\n*2\r\n+OK\r\n:1\r\n$5\r\nhello\r\n", "nested array should round-trip")) {
        return false;
    }

    return true;
}

bool testErrorFrameRoundTrip() {
    Buffer buffer;
    buffer.append("-ERR unknown command\r\n");

    RESPParser parser;
    std::shared_ptr<RESPObject> out;
    ParseResult res = parser.parse(&buffer, &out);

    if (!check(res == ParseResult::Success, "error frame should parse successfully")) {
        return false;
    }

    if (!check(out->toString() == "-ERR unknown command\r\n", "error frame should round-trip")) {
        return false;
    }

    return true;
}

} // namespace

int main() {
    LoggerImpl::instance().setLogLevel(INFO);

    if (!testIncompleteBulkStringPreservesBuffer()) {
        return 1;
    }

    if (!testNullBulkStringRoundTrip()) {
        return 1;
    }

    if (!testNullArrayRoundTrip()) {
        return 1;
    }

    if (!testPipelineKeepsRemainingData()) {
        return 1;
    }

    if (!testNestedArrayRoundTrip()) {
        return 1;
    }

    if (!testErrorFrameRoundTrip()) {
        return 1;
    }

    LOG_INFO << "RESP regression tests passed";
    return 0;
}
