# KV-Store 测试文档

## 1. 测试体系概览

KV-Store 的测试分为三个层次：

```
tests/
├── unit/               # 单元测试 — 每个模块独立测试
├── integration/        # 集成测试 — 跨模块联合测试
├── <flat files>        # 性能测试 & 全系统测试
├── Benchmark.h         # 基准测试框架
└── StressTest.h        # 并发压力测试框架
```

---

## 2. 构建测试

### 2.1 全部构建

```bash
# 确保 KV_BUILD_TESTS=ON（默认）
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### 2.2 仅构建测试

```bash
cmake --build build --config Release --target test_buffer
```

### 2.3 关闭测试构建

```bash
cmake -B build -DKV_BUILD_TESTS=OFF
```

---

## 3. 运行测试

所有测试可执行文件输出到 `build/tests/` 目录。

### 3.1 一键运行全部测试

**Windows:**

```batch
cd build\tests
for %f in (test_*.exe) do @echo === %f === && %f && echo.
```

**Linux/macOS:**

```bash
cd build/tests
for f in test_*; do echo "=== $f ==="; ./$f || exit 1; done
```

返回值为 0 表示通过，非 0 表示失败。失败详情打印在 stderr。

### 3.2 运行单个测试

```bash
cd build/tests
./test_buffer.exe          # Buffer 单元测试
./test_resp.exe            # RESP 类型单元测试
./test_resp_parser.exe     # RESP 解析器单元测试
./test_config.exe          # Config 单元测试
./test_command_dispatcher.exe   # 命令分发器单元测试
./test_inet_address.exe    # InetAddress 单元测试
./test_persistence_roundtrip.exe  # 持久化集成测试
./test_multi_client.exe    # 多客户端并发集成测试
./test_integration.exe     # 全系统集成测试
./test_eventloop_timer.exe # 事件循环定时器测试
./test_storage_keys.exe    # 键类型语义测试
./test_resp_regression.exe # RESP 协议回归测试
./test_storage_performance.exe  # 存储层性能测试
./test_network_performance.exe  # 网络层性能测试
```

---

## 4. 单元测试详解

### 4.1 test_buffer — Buffer 缓冲区

**覆盖范围:** Buffer 类的全部核心操作

| 测试 | 说明 |
|------|------|
| `test_initial_state` | 初始状态：readableBytes/writableBytes/prependableBytes |
| `test_append_retrieve` | 追加和分步读取 |
| `test_retrieve_all_as_string` | 一次性读取全部数据 |
| `test_find_crlf` | CRLF 搜索（正确找到和未找到） |
| `test_large_append` | 大数据追加触发内部扩容（4096 字节） |
| `test_retrieve_until` | 按指针位置截取 |
| `test_prepend` | 前置插入（prependable 空间利用） |
| `test_to_string_piece` | StringPiece 视图 |
| `test_append_string_piece` | StringPiece 追加 |
| `test_retrieve_all` | retrieveAll 重置读写指针 |
| `test_multiple_cycles` | 100 次循环读写，验证内部重排 |
| `test_shrink` | shrink 压缩容量 |
| `test_ensure_writable` | 自动扩容 |

**预期输出:**
```
test_buffer: ALL TESTS PASSED
```

---

### 4.2 test_resp — RESP 类型编码

**覆盖范围:** RESP 五种数据类型的序列化

| 测试 | 说明 |
|------|------|
| `test_simple_string` | `+OK\r\n` 编码 |
| `test_error` | `-ERR ... \r\n` 编码 |
| `test_integer` | `:0\r\n`, `:42\r\n`, `:-7\r\n`, `:INT64_MAX\r\n` |
| `test_bulk_string` | `$n\r\n...\r\n` 编码，空串，null 串 |
| `test_array` | `*n\r\n...` 编码，空数组，null 数组 |
| `test_nested_array` | 嵌套数组编码 `*2\r\n*2\r\n+OK\r\n...` |
| `test_roundtrip_all_types` | 全部类型的 toString 正确性 |

**预期输出:**
```
test_resp: ALL TESTS PASSED
```

---

### 4.3 test_resp_parser — RESP 解析器

**覆盖范围:** RESP 协议的流式解析（最全面的测试之一，17 个用例）

| 测试 | 说明 |
|------|------|
| SimpleString/Error/Integer 解析 | 基本类型解析 |
| 负整数解析 | `:-1\r\n` |
| BulkString 解析 | 正常串、null 串 `$-1`、空串 `$0` |
| Array 解析 | 正常数组、空数组、null 数组 |
| 嵌套 Array 解析 | `*2\r\n*1\r\n:42\r\n$4\r\ntest\r\n` |
| 不完整数据 | 缺少 CRLF、BulkString body 不足 |
| Pipeline 解析 | 两个命令在一个 Buffer 中连续解析 |
| 错误输入 | 垃圾前缀 `!garbage`、非法长度 `$abc` |
| 错误消息含空格 | `-ERR wrong number of arguments\r\n` |

**预期输出:**
```
test_resp_parser: ALL TESTS PASSED
```

---

### 4.4 test_config — 配置类

**覆盖范围:** Config 的 set/get/load 以及文件读取

| 测试 | 说明 |
|------|------|
| `test_set_get_string` | set/get string，默认值 |
| `test_set_get_int` | get int 含负数，默认值 |
| `test_set_get_bool` | `true`/`false`/`1`/`0`/`yes` 全部取值 |
| `test_load_from_file` | 文件加载，注释跳过，空值处理 |
| `test_load_nonexistent_file` | 文件不存在时的行为 |
| `test_overwrite_value` | 重复赋值覆盖 |
| `test_trim` | 值两侧空格去除 |

**预期输出:**
```
test_config: ALL TESTS PASSED
```

---

### 4.5 test_command_dispatcher — 命令分发器

**覆盖范围:** 全部 31 条命令的路由和参数校验

| 测试 | 覆盖命令 |
|------|----------|
| `test_ping_no_arg` | PING → PONG |
| `test_ping_with_arg` | PING hello → hello |
| `test_set_get` | SET / GET / GET 不存在 |
| `test_del` | DEL 多 key 删除计数 |
| `test_incr_decr` | INCR / DECR 完整流程 |
| `test_incr_non_integer` | INCR 字符串 → Error |
| `test_append` | APPEND 返回值正确性 |
| `test_strlen` | STRLEN 存在/不存在 |
| `test_exists` | EXISTS 多 key 计数 |
| `test_list_commands` | LPUSH/RPUSH/LPOP/RPOP/LRANGE/LLEN |
| `test_hash_commands` | HSET/HGET/HDEL/HGETALL/HKEYS/HLEN |
| `test_set_commands` | SADD/SREM/SMEMBERS/SISMEMBER/SCARD |
| `test_zset_commands` | ZADD/ZRANGE/ZSCORE/ZRANK/ZCARD/ZREM |
| `test_unknown_command` | 未知命令 → Error |
| `test_wrong_arg_count` | 参数不足 → Error |
| `test_dispatcher_with_aof` | 带 AOF 持久化的写命令记录 |

**预期输出:**
```
test_command_dispatcher: ALL TESTS PASSED
```

---

### 4.6 test_inet_address — 网络地址

**覆盖范围:** InetAddress 的构造和格式化

| 测试 | 说明 |
|------|------|
| `test_port_only` | 仅端口构造的 toIp/toIpPort |
| `test_ip_port` | IP+端口构造的格式化输出 |
| `test_from_sockaddr` | 从 sockaddr_in 构造 |
| `test_to_ip_port_format` | `ip:port` 格式验证 |
| `test_default_port` | 端口 0 的边界情况 |
| `test_set_sockaddr` | setSockAddrInet 更新 |
| `test_get_sockaddr` | getSockAddr 返回有效指针 |

**预期输出:**
```
test_inet_address: ALL TESTS PASSED
```

---

## 5. 集成测试详解

### 5.1 test_persistence_roundtrip — 持久化往返

**覆盖范围:** AOF 和 RDB 的 save → load 完整往返

| 测试 | 说明 |
|------|------|
| `test_aof_roundtrip` | 全类型数据集 → AOF save → 加载到新引擎 → 逐项验证 |
| `test_rdb_roundtrip` | 全类型数据集 → RDB save → 加载到新引擎 → verifyData |
| `test_aof_rewrite` | AOF 文件保存后重新加载验证 |
| `test_rdb_aof_combined` | RDB 基准 + AOF 增量 → 合并恢复，验证增量数据 |
| `test_persistence_manager` | PersistenceManager 生命周期：manualSave/loadData/getStatus |
| `test_empty_persistence` | 空数据集的存储/加载 |

**数据涵盖 5 种类型：** String、Hash、List、Set、Sorted Set

**预期输出:**
```
test_persistence_roundtrip: ALL TESTS PASSED
```

---

### 5.2 test_multi_client — 多客户端并发

**覆盖范围:** 多线程对存储引擎的并发访问

| 测试 | 说明 |
|------|------|
| `test_concurrent_set_get` | 8 线程 x 500 次 SET/GET，验证全部成功 |
| `test_concurrent_mixed_types` | 6 线程分 3 组（String/Hash/List），各 300 次操作 |
| `test_concurrent_incr` | 10 线程并发 INCR，验证最终计数精确等于预期值 |
| `test_producer_consumer` | 1 生产者 + 3 消费者，RPUSH/LPOP 队列模式 1000 条消息 |
| `test_concurrent_with_persistence` | 4 线程写操作 + AOF 持久化，验证恢复后数据完整 |

**预期输出:**
```
test_multi_client: ALL TESTS PASSED
```

---

## 6. 性能测试

### 6.1 test_storage_performance

测试存储引擎的吞吐量，使用 `Benchmark.h` 报告 QPS。

```bash
./build/tests/test_storage_performance.exe
```

输出示例：
```
=== Storage Performance ===
String SET             100000 ops    0.123 s    813008 QPS
String GET             100000 ops    0.098 s   1020408 QPS
Hash HSET               50000 ops    0.056 s    892857 QPS
...
```

### 6.2 test_network_performance

测试 Buffer 操作、ConnectionPool 并发操作、模拟网络吞吐量。

```bash
./build/tests/test_network_performance.exe
```

---

## 7. 其他专项测试

### test_integration — 全系统集成

综合测试协议解析→命令路由→存储→持久化的完整链路，包括：
- RESP 编解码性能
- 合成 SET/GET 命令流
- 多命令复合工作流（String/Hash/List/Set）
- 10 线程 x 1000 次并发访问
- 1000 key 的 AOF save→load 循环（10 次）
- 错误处理（空值、10 万 key 批量写入）

### test_eventloop_timer — 事件循环定时器

验证 `runAfter` 的触发和 `cancel` 的阻止

### test_storage_keys — 键类型语义

验证键类型切换行为和 `keys("*")` 模式匹配

### test_resp_regression — RESP 回归

验证 RESP 解析器的边缘情况：不完整数据保留、null 值往返、流水线、嵌套数组

---

## 8. 测试框架说明

### 8.1 断言机制

KV-Store 测试不依赖任何第三方测试框架。每个测试使用独立的轻量检查函数：

- 单元测试使用 `check(bool, msg)` — 打印到 stderr，累计失败计数
- 部分测试使用 `expect(bool, msg)` — 同名变体
- 返回值 `0` = 全部通过，`1` = 有失败

### 8.2 Benchmark.h — 基准测试框架

```cpp
Benchmark benchmark;

// 简单计时
benchmark.run("TestName", 100000, []() {
    // 被测操作
});

// 带延迟测量
benchmark.runWithLatency("TestName", 50000, []() {
    // 被测操作
});

// 打印汇总
benchmark.printSummary();
```

输出自动计算：总时间、操作数、QPS、平均延迟。

### 8.3 StressTest.h — 并发压力测试

```cpp
StressTest stress;

auto result = stress.runConcurrentTest("Name", threads, opsPerThread, []() {
    // 每个线程的操作
    return true;  // 返回成功或失败
});
// result 包含：QPS、总操作数、成功/失败数、线程数
```

### 8.4 MemoryMonitor — 内存监控（仅 Windows）

```cpp
MemoryMonitor::printMemoryUsage("Label");
// 输出当前进程的 RSS 内存使用量（KB）
```

---

## 9. 测试覆盖矩阵

| 模块 | 单元测试 | 集成测试 | 性能测试 | 回归测试 |
|------|----------|----------|----------|----------|
| Buffer | `test_buffer` | — | `test_network_performance` | — |
| RESP 类型 | `test_resp` | — | — | — |
| RESP 解析器 | `test_resp_parser` | — | — | `test_resp_regression` |
| Config | `test_config` | — | — | — |
| CommandDispatcher | `test_command_dispatcher` | `test_multi_client` | — | — |
| InetAddress | `test_inet_address` | — | — | — |
| MemoryStorageEngine | — | `test_persistence_roundtrip` | `test_storage_performance` | `test_storage_keys` |
| AOF Persistence | — | `test_persistence_roundtrip` | `test_storage_performance` | — |
| RDB Persistence | — | `test_persistence_roundtrip` | `test_storage_performance` | — |
| ConnectionPool | — | — | `test_network_performance` | — |
| EventLoop | — | — | `test_eventloop_timer` | — |
| 全系统 | — | `test_integration` | — | — |

---

## 10. 添加新测试

### 10.1 新单元测试

1. 在 `tests/unit/` 创建 `test_xxx.cpp`
2. 遵循现有模式：`check()` 辅助函数 + 多个 `test_*()` 函数 + `main()` 返回 0/1
3. 在 `tests/CMakeLists.txt` 添加：

```cmake
add_executable(test_xxx unit/test_xxx.cpp)
target_link_libraries(test_xxx PRIVATE kvstore)
set_target_properties(test_xxx PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
```

### 10.2 新集成测试

1. 在 `tests/integration/` 创建文件
2. 同样的 CMake 配置模式

### 10.3 测试命名规范

- 文件名使用 `snake_case`
- 测试函数以 `test_` 开头
- 成功打印 `"test_name: ALL TESTS PASSED"`
- 失败打印 `"test_name: N FAILURE(S)"` 到 stderr
- 返回 0（成功）或 1（失败）
