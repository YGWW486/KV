#include "../../src/utils/Config.h"
#include <string>
#include <cstdlib>
#include <cstdio>
#include <fstream>

using namespace kvstore;

static int g_failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) { ++g_failures; fprintf(stderr, "FAIL: %s\n", msg); }
}

// ---- set / get string ----
static bool test_set_get_string() {
    Config c;
    c.set("name", "KVStore");
    check(c.get<string>("name") == "KVStore", "get string set");
    check(c.get<string>("missing", "default") == "default", "get string default");
    return true;
}

// ---- set / get int ----
static bool test_set_get_int() {
    Config c;
    c.set("port", "6379");
    check(c.get<int>("port") == 6379, "get int value");
    check(c.get<int>("port_missing", 8080) == 8080, "get int default");
    c.set("negative", "-42");
    check(c.get<int>("negative") == -42, "get int negative");
    return true;
}

// ---- set / get bool ----
static bool test_set_get_bool() {
    Config c;
    c.set("debug", "true");
    check(c.get<bool>("debug") == true, "get bool true");
    c.set("verbose", "1");
    check(c.get<bool>("verbose") == true, "get bool '1'");
    c.set("dry_run", "yes");
    check(c.get<bool>("dry_run") == true, "get bool 'yes'");
    c.set("enabled", "false");
    check(c.get<bool>("enabled") == false, "get bool false");
    c.set("flag", "0");
    check(c.get<bool>("flag") == false, "get bool '0'");
    check(c.get<bool>("missing_bool", true) == true, "get bool default true");
    return true;
}

// ---- load from file ----
static bool test_load_from_file() {
    const char* fname = "_test_config_load.cfg";
    {
        std::ofstream ofs(fname);
        ofs << "# comment line\n";
        ofs << "server.port=8080\n";
        ofs << "server.host=localhost\n";
        ofs << "; another comment\n";
        ofs << "logging.level=debug\n";
        ofs << "\n";
        ofs << "empty=\n";
    }
    Config c(fname);
    check(c.get<int>("server.port") == 8080, "file load int");
    check(c.get<string>("server.host") == "localhost", "file load string");
    check(c.get<string>("logging.level") == "debug", "file load another string");
    check(c.get<string>("empty") == "", "file load empty value");
    std::remove(fname);
    return true;
}

// ---- load nonexistent file ----
static bool test_load_nonexistent_file() {
    Config c("_nonexistent_file_12345.cfg");
    check(c.get<string>("any") == "", "nonexistent file yields empty");
    return true;
}

// ---- overwrite ----
static bool test_overwrite_value() {
    Config c;
    c.set("key", "val1");
    c.set("key", "val2");
    check(c.get<string>("key") == "val2", "overwrite value");
    return true;
}

// ---- trim whitespace ----
static bool test_trim() {
    const char* fname = "_test_config_trim.cfg";
    {
        std::ofstream ofs(fname);
        ofs << "trimmed =  hello world  \n";
    }
    Config c(fname);
    check(c.get<string>("trimmed") == "hello world", "trim whitespace");
    std::remove(fname);
    return true;
}

int main() {
    test_set_get_string();
    test_set_get_int();
    test_set_get_bool();
    test_load_from_file();
    test_load_nonexistent_file();
    test_overwrite_value();
    test_trim();

    if (g_failures == 0) {
        fprintf(stdout, "test_config: ALL TESTS PASSED\n");
        return 0;
    }
    fprintf(stderr, "test_config: %d FAILURE(S)\n", g_failures);
    return 1;
}
