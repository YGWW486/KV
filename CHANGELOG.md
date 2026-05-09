# Changelog

## 2026-05-09 — 性能优化与正确性修复

基于知识图谱分析 + 源码审查，实施 P0/P1 两级优化。

### P0 — 正确性缺陷 & 严重性能陷阱

**持久化（3 项）**
- **AOF 文件持开句柄**: `recordWrite()` 每条写命令 open/write/close → `FILE*` 长连接 + `fflush`，消除写路径 syscall 风暴 (`AOFPersistenceEngine.cpp`)
- **AOF 真正 fsync**: `syncAOF()` 空函数 → `fdatasync`(Linux) / `_commit`(Windows) 落盘，三种 AOF 策略从此生效 (`AOFPersistenceEngine.cpp`)
- **AOF 值转义**: key/value 含空格/换行/引号时 reload 数据损坏 → `formatAOFLine()` 双引号转义，`splitArgs()` 解析兼容新旧格式 (`AOFPersistenceEngine.cpp`, `CommandDispatcher.cpp`)

**存储（4 项）**
- **`zrevrange()` 全量物化**: 100 万元素取 top 10 拷贝全部 → reverse iterator 按需截断 (`MemoryStorageEngine.cpp`, `SegmentedMemoryStorageEngine.cpp`)
- **`lrange()` O(start) 遍历**: `std::list` + `std::advance` → `std::deque` O(1) 随机访问 (`MemoryStorageEngine.h`, `SegmentedMemoryStorageEngine.h`)
- **`lpop/rpop` 移动语义**: 拷贝 `std::string` → `std::move` (`MemoryStorageEngine.cpp`, `SegmentedMemoryStorageEngine.cpp`)
- **集合方法预分配**: `hkeys/hvals/hgetall/smembers` 加 `reserve()`，避免循环中 realloc

**事件循环（2 项）**
- **动态超时**: EpollLoop/IOCPLoop 固定 1000ms → `nextExpiration()` 驱动，空闲连接不再每秒唤醒 (`EpollLoop.cpp`, `IOCPLoop.cpp`, `EventLoop.h/cpp`)
- **Timer 有序存储**: `push_back` + 全量扫描 → `lower_bound` 有序插入 + 遇未过期即 break (`EventLoop.cpp`)

**Accept 机制**
- **IOCP 路径**: 500ms 定时器单次 accept → 50ms 定时器 + 循环排空，多连接 accept 延迟从 8s 降至 50ms (`main.cpp`)
- **Acceptor**: `handleRead()` 单次 accept → 循环排水 (`Acceptor.cpp`)

**存储引擎切换**
- `main.cpp` 从 `MemoryStorageEngine`（单一大锁）→ `SegmentedMemoryStorageEngine`（16 分段 + shared_mutex 读写锁）

### P1 — 热路径优化

- **命令分发**: 30 个串行 `if (cmd == ...)` → `unordered_map<string, CmdHandler>` O(1) 跳表 (`CommandDispatcher.cpp/h`)
- **RESP 序列化**: 每次 `toString()` 新建 `ostringstream` → 手工 `string::reserve` + 拼接 (`RESP.cpp`)
- **RESP 整数解析**: `std::stoll(temp_string)` → `std::from_chars` 零拷贝 (`RESPParser.cpp`)

### 基准测试演变（单连接 Pipeline=16, 空 DB）

| 阶段 | PING | SET | GET | Mixed |
|------|------|-----|-----|-------|
| 修改前 | 26,407 | 54,689 | 143,329 | 106,939 |
| P0 后 | 29,167 | 48,930 | 121,909 | 89,990 |
| P1 后 | **30,512** | **52,809** | **137,579** | **103,291** |
| 累积变化 | +16% | -3% | -4% | -3% |

QPS 微降源自 AOF 转义、真实 fsync、分段锁等正确性/架构改进的必要开销。并发扩展性从衰减 12~15% 提升至衰减 1~3%。

### 并发对比（16 连接）

修复前 16 连接比单连接慢 12~15%，修复后差距缩小到 1~3%。

### 修改文件清单

```
src/commands/CommandDispatcher.cpp
src/commands/CommandDispatcher.h
src/main.cpp
src/network/Acceptor.cpp
src/network/EventLoop.cpp
src/network/EventLoop.h
src/network/epoll/EpollLoop.cpp
src/network/iocp/IOCPLoop.cpp
src/network/iocp/IOCPLoop.h
src/protocol/RESP.cpp
src/protocol/RESPParser.cpp
src/storage/AOFPersistenceEngine.cpp
src/storage/MemoryStorageEngine.cpp
src/storage/MemoryStorageEngine.h
src/storage/PersistenceEngine.h
src/storage/SegmentedMemoryStorageEngine.cpp
src/storage/SegmentedMemoryStorageEngine.h
.gitignore
docs/PERF_OPTIMIZATION.md
CHANGELOG.md
```
