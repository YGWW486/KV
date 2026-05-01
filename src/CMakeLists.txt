
# ==============================================================================
# 源文件收集
# ==============================================================================

# 工具类
set(UTILS_SOURCES
    utils/Timestamp.cpp
    utils/Logging.cpp
    utils/Config.cpp
)

# 网络层（P1/P2阶段）
set(NETWORK_SOURCES
    network/Buffer.cpp
    network/EventLoop.cpp
    network/Channel.cpp
    network/Timer.cpp
    network/InetAddress.cpp
    network/Acceptor.cpp
    network/Connection.cpp
    network/ConnectionPool.cpp
)

# Windows IOCP实现
if(WIN32)
    list(APPEND NETWORK_SOURCES
        network/iocp/IOCPLoop.cpp
    )
endif()

# 协议层 (P3阶段添加)
set(PROTOCOL_SOURCES
    protocol/RESP.cpp
    protocol/RESPParser.cpp
)

# 存储层 (P4阶段添加)
set(STORAGE_SOURCES
    storage/MemoryStorageEngine.cpp
)

# 持久化层 (P5阶段添加)
set(PERSISTENCE_SOURCES
    storage/AOFPersistenceEngine.cpp
    storage/RDBPersistenceEngine.cpp
    storage/PersistenceManager.cpp
)

# 命令层 (P4阶段添加)
set(COMMANDS_SOURCES
    commands/CommandDispatcher.cpp
)

# ==============================================================================
# 创建 kvstore 库
# ==============================================================================
add_library(kvstore STATIC
    ${UTILS_SOURCES}
    ${NETWORK_SOURCES}
    ${PROTOCOL_SOURCES}
    ${STORAGE_SOURCES}
    ${PERSISTENCE_SOURCES}
    ${COMMANDS_SOURCES}
)

target_include_directories(kvstore
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

# Windows 上链接必要的库
if(WIN32)
    target_link_libraries(kvstore
        PRIVATE
            ws2_32
            mswsock
    )
else()
    target_link_libraries(kvstore
        PRIVATE
            pthread
    )
endif()

# ==============================================================================
# 主程序
# ==============================================================================
add_executable(kv-server
    main.cpp
)

target_link_libraries(kv-server
    PRIVATE
        kvstore
)
