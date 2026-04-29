#ifndef KVSTORE_UTILS_CONFIG_H
#define KVSTORE_UTILS_CONFIG_H

#include "kvstore/Types.h"
#include <map>

namespace kvstore {

class Config {
public:
    Config();
    explicit Config(const string& filename);
    ~Config();

    bool load(const string& filename);
    
    template<typename T>
    T get(const string& key, const T& defaultValue = T()) const;
    
    void set(const string& key, const string& value);

private:
    void parse(const string& content);
    string trim(const string& str) const;

    std::map<string, string> data_;
};

template<>
inline int Config::get<int>(const string& key, const int& defaultValue) const {
    auto it = data_.find(key);
    if (it != data_.end()) {
        return std::stoi(it->second);
    }
    return defaultValue;
}

template<>
inline string Config::get<string>(const string& key, const string& defaultValue) const {
    auto it = data_.find(key);
    if (it != data_.end()) {
        return it->second;
    }
    return defaultValue;
}

template<>
inline bool Config::get<bool>(const string& key, const bool& defaultValue) const {
    auto it = data_.find(key);
    if (it != data_.end()) {
        const string& val = it->second;
        return val == "true" || val == "1" || val == "yes";
    }
    return defaultValue;
}

} // namespace kvstore

#endif // KVSTORE_UTILS_CONFIG_H
