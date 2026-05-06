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
