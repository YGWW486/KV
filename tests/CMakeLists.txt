# 性能测试和集成测试配置

# 存储层性能测试
add_executable(test_storage_performance
    test_storage_performance.cpp
)

target_link_libraries(test_storage_performance
    PRIVATE
        kvstore
)

# 网络层性能测试
add_executable(test_network_performance
    test_network_performance.cpp
)

target_link_libraries(test_network_performance
    PRIVATE
        kvstore
)

# 系统集成测试
add_executable(test_integration
    test_integration.cpp
)

# 事件循环定时器测试
add_executable(test_eventloop_timer
    test_eventloop_timer.cpp
)

# 存储键语义测试
add_executable(test_storage_keys
    test_storage_keys.cpp
)

# RESP 协议回归测试
add_executable(test_resp_regression
    test_resp_regression.cpp
)

target_link_libraries(test_integration
    PRIVATE
        kvstore
)

target_link_libraries(test_eventloop_timer
    PRIVATE
        kvstore
)

target_link_libraries(test_storage_keys
    PRIVATE
        kvstore
)

target_link_libraries(test_resp_regression
    PRIVATE
        kvstore
)

# 设置测试目标属性
set_target_properties(
    test_storage_performance
    test_network_performance
    test_integration
    test_eventloop_timer
    test_storage_keys
    test_resp_regression
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)
