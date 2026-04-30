#include "../src/storage/MemoryStorageEngine.h"
#include "../src/utils/Logging.h"

#include <algorithm>
#include <vector>

using namespace kvstore;

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        LOG_ERROR << message;
        return false;
    }
    return true;
}

} // namespace

int main() {
    LoggerImpl::instance().setLogLevel(INFO);

    MemoryStorageEngine storage;

    storage.set("shared", "string-value");
    storage.hset("shared", "field", "hash-value");

    auto value = storage.get("shared");
    if (!expect(!value.has_value(), "shared key should not stay as string after type switch")) {
        return 1;
    }

    auto hashValue = storage.hget("shared", "field");
    if (!expect(hashValue.has_value() && *hashValue == "hash-value", "shared key should behave as hash")) {
        return 1;
    }

    storage.set("user:1", "alice");
    storage.set("user:2", "bob");
    storage.set("session:1", "token");

    auto keys = storage.keys("user:*");
    std::sort(keys.begin(), keys.end());
    if (!expect(keys == std::vector<std::string>{"user:1", "user:2"}, "keys(pattern) should filter by pattern")) {
        return 1;
    }

    auto allKeys = storage.keys("*");
    std::sort(allKeys.begin(), allKeys.end());
    if (!expect(std::count(allKeys.begin(), allKeys.end(), "shared") == 1, "duplicate keys should not appear")) {
        return 1;
    }

    LOG_INFO << "Storage key semantics test passed";
    return 0;
}
