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

target_link_libraries(test_integration
    PRIVATE
        kvstore
)

# 设置测试目标属性
set_target_properties(
    test_storage_performance
    test_network_performance
    test_integration
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)