# ==============================================================================
# Performance and Integration Test Configuration
# ==============================================================================

# ---- Storage Performance Test ----
add_executable(test_storage_performance
    test_storage_performance.cpp
)
target_link_libraries(test_storage_performance PRIVATE kvstore)

# ---- Network Performance Test ----
add_executable(test_network_performance
    test_network_performance.cpp
)
target_link_libraries(test_network_performance PRIVATE kvstore)

# ---- System Integration Test ----
add_executable(test_integration
    test_integration.cpp
)
target_link_libraries(test_integration PRIVATE kvstore)

# ---- EventLoop Timer Test ----
add_executable(test_eventloop_timer
    test_eventloop_timer.cpp
)
target_link_libraries(test_eventloop_timer PRIVATE kvstore)

# ---- Storage Key Semantics Test ----
add_executable(test_storage_keys
    test_storage_keys.cpp
)
target_link_libraries(test_storage_keys PRIVATE kvstore)

# ---- RESP Protocol Regression Test ----
add_executable(test_resp_regression
    test_resp_regression.cpp
)
target_link_libraries(test_resp_regression PRIVATE kvstore)

# ==============================================================================
# Unit Tests
# ==============================================================================

# ---- Buffer Unit Test ----
add_executable(test_buffer_unit
    unit/test_buffer.cpp
)
target_link_libraries(test_buffer_unit PRIVATE kvstore)

# ---- RESP Types Unit Test ----
add_executable(test_resp_unit
    unit/test_resp.cpp
)
target_link_libraries(test_resp_unit PRIVATE kvstore)

# ---- RESP Parser Unit Test ----
add_executable(test_resp_parser
    unit/test_resp_parser.cpp
)
target_link_libraries(test_resp_parser PRIVATE kvstore)

# ---- Config Unit Test ----
add_executable(test_config
    unit/test_config.cpp
)
target_link_libraries(test_config PRIVATE kvstore)

# ---- Command Dispatcher Unit Test ----
add_executable(test_command_dispatcher
    unit/test_command_dispatcher.cpp
)
target_link_libraries(test_command_dispatcher PRIVATE kvstore)

# ---- InetAddress Unit Test ----
add_executable(test_inet_address
    unit/test_inet_address.cpp
)
target_link_libraries(test_inet_address PRIVATE kvstore)

# ==============================================================================
# Integration Tests
# ==============================================================================

# ---- Persistence Roundtrip Integration Test ----
add_executable(test_persistence_roundtrip
    integration/test_persistence_roundtrip.cpp
)
target_link_libraries(test_persistence_roundtrip PRIVATE kvstore)

# ---- Multi-Client Concurrency Integration Test ----
add_executable(test_multi_client
    integration/test_multi_client.cpp
)
target_link_libraries(test_multi_client PRIVATE kvstore)

# ---- Segmented Storage Benchmark + Correctness ----
add_executable(test_segmented_storage
    test_segmented_storage.cpp
)
target_link_libraries(test_segmented_storage PRIVATE kvstore)

# ==============================================================================
# Set output directory for all test targets
# ==============================================================================
set_target_properties(
    test_storage_performance
    test_network_performance
    test_integration
    test_eventloop_timer
    test_storage_keys
    test_resp_regression
    test_buffer_unit
    test_resp_unit
    test_resp_parser
    test_config
    test_command_dispatcher
    test_inet_address
    test_persistence_roundtrip
    test_multi_client
    test_segmented_storage
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# ==============================================================================
# CTest：按模块打标签，避免一次性跑完全部冗长性能用例
#
# 用法（在 build 目录）：
#   ctest -L fast              # 日常：除「性能四件套」外全部
#   ctest -L performance       # 仅四项重型性能/基准（日志最多）
#   ctest -L unit              # 仅单元
#   cmake --build . --target kv-test-performance
# ==============================================================================
set(_kv_test_wd ${CMAKE_BINARY_DIR}/tests)

# ---- fast：单元 + 集成 + 杂项（输出相对较少）----
add_test(NAME kv_unit_buffer           COMMAND test_buffer_unit           WORKING_DIRECTORY ${_kv_test_wd})
add_test(NAME kv_unit_resp             COMMAND test_resp_unit               WORKING_DIRECTORY ${_kv_test_wd})
add_test(NAME kv_unit_resp_parser      COMMAND test_resp_parser            WORKING_DIRECTORY ${_kv_test_wd})
add_test(NAME kv_unit_config           COMMAND test_config                 WORKING_DIRECTORY ${_kv_test_wd})
add_test(NAME kv_unit_command_dispatch COMMAND test_command_dispatcher      WORKING_DIRECTORY ${_kv_test_wd})
add_test(NAME kv_unit_inet_address     COMMAND test_inet_address           WORKING_DIRECTORY ${_kv_test_wd})
set_tests_properties(
    kv_unit_buffer kv_unit_resp kv_unit_resp_parser
    kv_unit_config kv_unit_command_dispatch kv_unit_inet_address
    PROPERTIES LABELS "unit;fast")

add_test(NAME kv_integ_persistence COMMAND test_persistence_roundtrip WORKING_DIRECTORY ${_kv_test_wd})
add_test(NAME kv_integ_multiclient COMMAND test_multi_client          WORKING_DIRECTORY ${_kv_test_wd})
set_tests_properties(kv_integ_persistence kv_integ_multiclient PROPERTIES LABELS "integration;fast")

add_test(NAME kv_misc_eventloop_timer COMMAND test_eventloop_timer WORKING_DIRECTORY ${_kv_test_wd})
add_test(NAME kv_misc_storage_keys    COMMAND test_storage_keys    WORKING_DIRECTORY ${_kv_test_wd})
add_test(NAME kv_misc_resp_regression COMMAND test_resp_regression WORKING_DIRECTORY ${_kv_test_wd})
set_tests_properties(kv_misc_eventloop_timer kv_misc_storage_keys kv_misc_resp_regression PROPERTIES LABELS "misc;fast")

# ---- performance：日志多、耗时长，单独跑 ----
add_test(NAME kv_perf_integration          COMMAND test_integration          WORKING_DIRECTORY ${_kv_test_wd})
add_test(NAME kv_perf_network              COMMAND test_network_performance  WORKING_DIRECTORY ${_kv_test_wd})
add_test(NAME kv_perf_storage              COMMAND test_storage_performance  WORKING_DIRECTORY ${_kv_test_wd})
add_test(NAME kv_perf_segmented_storage    COMMAND test_segmented_storage   WORKING_DIRECTORY ${_kv_test_wd})
set_tests_properties(
    kv_perf_integration kv_perf_network kv_perf_storage kv_perf_segmented_storage
    PROPERTIES LABELS "performance")

# 便捷目标（在 build 目录执行 cmake 后即可用）
add_custom_target(kv-test-fast
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_BINARY_DIR}" -L fast
    COMMENT "Run fast tests (unit + integration + misc), excluding heavy performance suite")
add_custom_target(kv-test-performance
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_BINARY_DIR}" -L performance
    COMMENT "Run heavy performance / benchmark tests only")
add_custom_target(kv-test-unit
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_BINARY_DIR}" -L unit
    COMMENT "Run unit tests only")
add_custom_target(kv-test-integration
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_BINARY_DIR}" -L integration
    COMMENT "Run integration tests only (persistence roundtrip + multi_client)")
