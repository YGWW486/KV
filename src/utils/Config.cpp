#include "utils/Config.h"
#include "utils/Logging.h"
#include <fstream>
#include <sstream>

namespace kvstore {

Config::Config() {
}

Config::Config(const string& filename) {
    load(filename);
}

Config::~Config() {
}

bool Config::load(const string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_WARN << "Failed to open config file: " << filename;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    parse(buffer.str());
    LOG_INFO << "Loaded config from " << filename;
    return true;
}

void Config::parse(const string& content) {
    std::istringstream ss(content);
    string line;
    
    while (std::getline(ss, line)) {
        line = trim(line);
        
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        size_t pos = line.find('=');
        if (pos != string::npos) {
            string key = trim(line.substr(0, pos));
            string value = trim(line.substr(pos + 1));
            data_[key] = value;
        }
    }
}

void Config::set(const string& key, const string& value) {
    data_[key] = value;
}

string Config::trim(const string& str) const {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

} // namespace kvstore
