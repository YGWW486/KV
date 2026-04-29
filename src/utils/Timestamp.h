#ifndef KVSTORE_UTILS_TIMESTAMP_H
#define KVSTORE_UTILS_TIMESTAMP_H

#include "kvstore/Types.h"
#include <string>

namespace kvstore {

class Timestamp {
public:
    Timestamp();
    explicit Timestamp(int64_t microSecondsSinceEpoch);
    ~Timestamp() = default;

    void swap(Timestamp& other);
    string toString() const;
    string toFormattedString(bool showMicroseconds = true) const;

    bool valid() const;
    int64_t microSecondsSinceEpoch() const;

    static Timestamp now();
    static Timestamp invalid();
    static const int64_t kMicroSecondsPerSecond = 1000 * 1000;

private:
    int64_t microSecondsSinceEpoch_;
};

inline bool operator<(Timestamp lhs, Timestamp rhs) {
    return lhs.microSecondsSinceEpoch() < rhs.microSecondsSinceEpoch();
}

inline bool operator==(Timestamp lhs, Timestamp rhs) {
    return lhs.microSecondsSinceEpoch() == rhs.microSecondsSinceEpoch();
}

inline double timeDifference(Timestamp high, Timestamp low) {
    int64_t diff = high.microSecondsSinceEpoch() - low.microSecondsSinceEpoch();
    return static_cast<double>(diff) / Timestamp::kMicroSecondsPerSecond;
}

inline Timestamp addTime(Timestamp timestamp, double seconds) {
    int64_t delta = static_cast<int64_t>(seconds * Timestamp::kMicroSecondsPerSecond);
    return Timestamp(timestamp.microSecondsSinceEpoch() + delta);
}

} // namespace kvstore

#endif // KVSTORE_UTILS_TIMESTAMP_H
