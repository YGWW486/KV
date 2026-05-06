# KV-Store 用户使用手册

## 1. 项目简介

KV-Store 是一个 **Redis 兼容的内存键值数据库**，使用 C++17 从零构建，实现了完整的 RESP（Redis Serialization Protocol）协议。它支持 Redis 的五种核心数据结构，并提供 AOF 和 RDB 两种持久化机制。

- **版本**: 0.1.0
- **协议**: RESP（与 Redis 客户端完全兼容）
- **平台**: Windows（IOCP 高性能异步网络）+ Linux/Unix 兼容
- **许可证**: 开源项目

---

## 2. 环境要求

| 要求 | 说明 |
|------|------|
| 操作系统 | Windows 10+ / Linux (Ubuntu 20.04+, CentOS 7+) |
| 编译器 | MSVC 2017+ 或 GCC 8+ 或 Clang 7+ |
| C++ 标准 | C++17（强制） |
| CMake | 3.15+ |
| 依赖 | 零外部依赖，全部自实现 |

---

## 3. 编译安装

### 3.1 获取源码

```bash
git clone https://github.com/YGWW486/KV.git
cd KV
```

### 3.2 编译（Windows / MSVC）

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### 3.3 编译（Linux / GCC）

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 3.4 编译选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `KV_BUILD_TESTS` | ON | 编译测试程序 |
| `KV_BUILD_EXAMPLES` | ON | 编译示例程序 |
| `KV_ENABLE_ASAN` | OFF | 启用 AddressSanitizer 内存检测 |

示例：仅编译服务器，不编译测试和示例

```bash
cmake -B build -DKV_BUILD_TESTS=OFF -DKV_BUILD_EXAMPLES=OFF
cmake --build build
```

### 3.5 编译产物

```
build/
├── bin/
│   ├── kv-server.exe          # 主服务器程序
│   ├── echo_server.exe        # Echo 示例服务端
│   └── echo_client.exe        # Echo 示例客户端
├── tests/
│   ├── test_integration.exe   # 集成测试
│   ├── test_storage_performance.exe
│   ├── test_network_performance.exe
│   └── ...
└── lib/
    └── kvstore.lib            # 静态库（可嵌入其他项目）
```

---

## 4. 快速启动

### 4.1 启动服务器

```bash
cd build/bin
./kv-server.exe
```

启动后输出示例：

```
[INFO] ========================================
[INFO]   KV-Store Server v0.1.0
[INFO] ========================================
[INFO] KV-Store listening on port 6379
```

服务器默认监听 **6379 端口**（与 Redis 默认端口一致）。

### 4.2 停止服务器

按 `Ctrl+C` 优雅关闭。服务器会输出：

```
[INFO] KV-Store shutting down...
[INFO] KV-Store stopped.
```

### 4.3 连接服务器

由于 KV-Store 使用标准 RESP 协议，可以使用任何 Redis 客户端连接：

**方式一：使用 redis-cli**

```bash
redis-cli -p 6379
127.0.0.1:6379> PING
PONG
127.0.0.1:6379> SET name KV-Store
OK
127.0.0.1:6379> GET name
"KV-Store"
```

**方式二：使用编程语言客户端（Python 示例）**

```python
import redis
r = redis.Redis(host='localhost', port=6379)
r.ping()         # True
r.set('foo', 'bar')
r.get('foo')     # b'bar'
```

---

## 5. 支持的命令

KV-Store 支持 **31 个 Redis 命令**，覆盖全部五种数据类型。

### 5.1 通用命令

#### PING
测试连接是否正常。

```
PING           → +PONG
PING "hello"   → $5\r\nhello
```

#### DEL
删除一个或多个 key。

```
DEL key1 key2 key3   → :2        # 返回成功删除的 key 数量
```

#### EXISTS
检查 key 是否存在。

```
EXISTS key1 key2     → :1        # 返回存在的 key 数量
```

---

### 5.2 字符串（String）

| 命令 | 语法 | 说明 |
|------|------|------|
| SET | `SET key value` | 设置字符串值 |
| GET | `GET key` | 获取字符串值，不存在返回 nil |
| INCR | `INCR key` | 整数自增 1，不存在则从 0 开始 |
| DECR | `DECR key` | 整数自减 1，不存在则从 0 开始 |
| APPEND | `APPEND key value` | 追加字符串，返回新长度 |
| STRLEN | `STRLEN key` | 返回字符串长度 |

**使用示例：**

```bash
127.0.0.1:6379> SET counter 10
OK
127.0.0.1:6379> INCR counter
(integer) 11
127.0.0.1:6379> DECR counter
(integer) 10
127.0.0.1:6379> APPEND greeting " World"
(integer) 11
127.0.0.1:6379> STRLEN greeting
(integer) 11
```

---

### 5.3 哈希（Hash）

| 命令 | 语法 | 说明 |
|------|------|------|
| HSET | `HSET key field value [field value ...]` | 设置字段，返回新增字段数 |
| HGET | `HGET key field` | 获取字段值 |
| HDEL | `HDEL key field [field ...]` | 删除字段，返回删除数 |
| HGETALL | `HGETALL key` | 获取所有字段和值 |
| HKEYS | `HKEYS key` | 获取所有字段名 |
| HLEN | `HLEN key` | 获取字段数量 |

**使用示例：**

```bash
127.0.0.1:6379> HSET user:1 name "Tom" age "25"
(integer) 2
127.0.0.1:6379> HGET user:1 name
"Tom"
127.0.0.1:6379> HGETALL user:1
1) "name"
2) "Tom"
3) "age"
4) "25"
127.0.0.1:6379> HLEN user:1
(integer) 2
```

---

### 5.4 列表（List）

| 命令 | 语法 | 说明 |
|------|------|------|
| LPUSH | `LPUSH key elem [elem ...]` | 左侧插入，返回列表长度 |
| RPUSH | `RPUSH key elem [elem ...]` | 右侧插入，返回列表长度 |
| LPOP | `LPOP key` | 左侧弹出元素 |
| RPOP | `RPOP key` | 右侧弹出元素 |
| LRANGE | `LRANGE key start end` | 获取范围内元素（-1 表示末尾） |
| LLEN | `LLEN key` | 获取列表长度 |

**使用示例：**

```bash
127.0.0.1:6379> RPUSH queue "task1" "task2" "task3"
(integer) 3
127.0.0.1:6379> LPOP queue
"task1"
127.0.0.1:6379> LRANGE queue 0 -1
1) "task2"
2) "task3"
127.0.0.1:6379> LLEN queue
(integer) 2
```

---

### 5.5 集合（Set）

| 命令 | 语法 | 说明 |
|------|------|------|
| SADD | `SADD key member [member ...]` | 添加元素，返回新增数 |
| SREM | `SREM key member [member ...]` | 移除元素，返回移除数 |
| SMEMBERS | `SMEMBERS key` | 获取所有元素 |
| SISMEMBER | `SISMEMBER key member` | 判断是否为成员（1/0） |
| SCARD | `SCARD key` | 获取元素数量 |

**使用示例：**

```bash
127.0.0.1:6379> SADD tags "redis" "database" "kv"
(integer) 3
127.0.0.1:6379> SADD tags "redis"
(integer) 0
127.0.0.1:6379> SISMEMBER tags "redis"
(integer) 1
127.0.0.1:6379> SMEMBERS tags
1) "kv"
2) "database"
3) "redis"
```

---

### 5.6 有序集合（Sorted Set）

| 命令 | 语法 | 说明 |
|------|------|------|
| ZADD | `ZADD key score member [score member ...]` | 添加成员，返回新增数 |
| ZREM | `ZREM key member [member ...]` | 移除成员，返回移除数 |
| ZRANGE | `ZRANGE key start end` | 按分数升序返回成员 |
| ZSCORE | `ZSCORE key member` | 获取成员分数 |
| ZRANK | `ZRANK key member` | 获取成员排名（0 起始） |
| ZCARD | `ZCARD key` | 获取成员数量 |

**使用示例：**

```bash
127.0.0.1:6379> ZADD leaderboard 100 "Alice" 85 "Bob" 95 "Charlie"
(integer) 3
127.0.0.1:6379> ZRANGE leaderboard 0 -1
1) "Bob"
2) "Charlie"
3) "Alice"
127.0.0.1:6379> ZSCORE leaderboard "Alice"
"100.000000"
127.0.0.1:6379> ZRANK leaderboard "Alice"
(integer) 2
127.0.0.1:6379> ZCARD leaderboard
(integer) 3
```

---

## 6. 持久化

KV-Store 同时支持两种持久化机制，服务器启动时自动加载已有的持久化文件。

### 6.1 AOF（Append-Only File）

- **文件**: `kvstore.aof`（工作目录下）
- **策略**: EVERYSEC（每秒同步一次）
- **机制**: 将每个写命令追加记录到文本文件，重启时重放命令恢复数据
- **自动同步**: 每 1 秒自动执行 sync
- **重写压缩**: 支持 AOF rewrite，从当前存储状态重建紧凑的 AOF 文件

### 6.2 RDB（快照）

- **文件**: `kvstore.rdb`（工作目录下）
- **格式**: 自定义文本格式 `KVSTORE_RDB_V1`
- **自动保存**: 每 30 秒自动保存一次快照
- **启动恢复**: 服务器启动时优先加载 RDB，再重放 AOF 增量数据

### 6.3 启动恢复流程

```
启动 → 加载 RDB 快照 → 重放 AOF 增量 → 就绪
```

### 6.4 关键约束

- 每个 key 只能属于一种数据类型，跨类型操作会失败
- 持久化文件默认在服务器工作目录生成，请确保有写入权限

---

## 7. 项目架构

```
┌──────────────────────────────────────────┐
│              Redis 客户端                  │
│        (redis-cli / 任意 Redis SDK)        │
└──────────────┬───────────────────────────┘
               │ RESP 协议 (TCP)
┌──────────────▼───────────────────────────┐
│          网络层 (IOCP / EventLoop)         │
│  Connection → Channel → Buffer → Acceptor │
└──────────────┬───────────────────────────┘
               │
┌──────────────▼───────────────────────────┐
│          协议层 (RESPParser)               │
│  解析 RESP 请求，编码 RESP 响应             │
└──────────────┬───────────────────────────┘
               │
┌──────────────▼───────────────────────────┐
│          命令层 (CommandDispatcher)        │
│  路由 31 条命令，参数校验，AOF 记录          │
└──────────────┬───────────────────────────┘
               │
┌──────────────▼───────────────────────────┐
│     存储层 (MemoryStorageEngine)           │
│  String / Hash / List / Set / ZSet        │
└──────────────┬───────────────────────────┘
               │
┌──────────────▼───────────────────────────┐
│      持久化层 (AOF + RDB)                   │
│  AOF: 命令日志    RDB: 定期快照              │
└──────────────────────────────────────────┘
```

### 核心模块

| 模块 | 目录 | 职责 |
|------|------|------|
| 网络 | `src/network/` | IOCP 事件循环、TCP 连接、缓冲区管理 |
| 协议 | `src/protocol/` | RESP 协议解析与编码 |
| 命令 | `src/commands/` | 命令路由与参数检查 |
| 存储 | `src/storage/` | 五种数据结构的 STL 实现 |
| 持久化 | `src/storage/` | AOF 写前日志 + RDB 快照 |
| 工具 | `src/utils/` | 日志、配置、时间戳 |

---

## 8. 配置

### 8.1 服务端口

编辑 `src/main.cpp` 中的端口配置：

```cpp
config.set("server.port", "6379");     // 修改此行改变端口
```

或后续通过 Config 文件配置（格式为 `key=value`，`#` 开头为注释）：

```ini
# kvstore.conf
server.port=6379
```

### 8.2 持久化文件名

编辑 `src/main.cpp`：

```cpp
KvServer(IOCPLoop* loop, uint16_t port)
    : loop_(loop),
      aof_("kvstore.aof"),       // 修改 AOF 文件名
      rdb_("kvstore.rdb"),       // 修改 RDB 文件名
      ...
```

### 8.3 持久化策略

- **AOF 同步间隔**: `loop_->runEvery(1.0, ...)` — 修改 `1.0` 调整秒数
- **RDB 快照间隔**: `loop_->runEvery(30.0, ...)` — 修改 `30.0` 调整秒数
- **AOF 策略**: 默认 `EVERYSEC`，可在代码中改为 `ALWAYS` 或 `NO`

---

## 9. 运行测试

```bash
# 进入构建目录
cd build

# 运行集成测试
./tests/test_integration.exe

# 运行存储性能测试
./tests/test_storage_performance.exe

# 运行网络性能测试
./tests/test_network_performance.exe

# 运行 RESP 回归测试
./tests/test_resp_regression.exe

# 运行键类型测试
./tests/test_storage_keys.exe
```

---

## 10. 示例程序

### Echo Server

```bash
./build/bin/echo_server.exe
# 监听端口，将客户端发送的内容原样返回
```

### Echo Client

```bash
./build/bin/echo_client.exe
# 连接到 echo_server，可交互式输入文本
```

---

## 11. 常见问题

**Q: 启动时报 `bind failed` 错误？**
A: 端口 6379 被占用。检查是否有其他程序（如 Redis）正在使用该端口。

**Q: 可以用正式 Redis 客户端连接吗？**
A: 可以。KV-Store 实现标准 RESP 协议，redis-cli、node-redis、redis-py 等均可连接。

**Q: 数据存在哪里？**
A: 内存中。持久化数据保存在工作目录下的 `kvstore.aof` 和 `kvstore.rdb` 文件中。

**Q: 支持多少并发连接？**
A: 采用 IOCP 异步模型，可处理数千并发连接，实际取决于系统资源。

**Q: 和 Redis 有什么差异？**
A: KV-Store 是学习项目，支持核心命令的子集（31 条），不含集群、主从复制、Lua 脚本、过期时间等高级特性。

**Q: key 的类型约束是什么？**
A: 一个 key 只能属于一种数据类型。例如，不能对已设置为 String 的 key 执行 LPUSH。

---

## 12. 技术支持

- 项目仓库: [https://github.com/YGWW486/KV](https://github.com/YGWW486/KV)
- 学习笔记: 参见 `learning-notes/` 目录，包含技术深潜和面试准备资料
