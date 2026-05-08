# Examples build configuration

# Echo server
add_executable(echo_server
    echo_server.cpp
)

target_link_libraries(echo_server
    PRIVATE
        kvstore
)

# Echo client (Windows only)
if(WIN32)
add_executable(echo_client
    echo_client.cpp
)

target_link_libraries(echo_client
    PRIVATE
        kvstore
)
endif()

# RESP测试程序
add_executable(test_resp
    test_resp.cpp
)

target_link_libraries(test_resp
    PRIVATE
        kvstore
)

# Storage测试程序
add_executable(test_storage
    test_storage.cpp
)

target_link_libraries(test_storage
    PRIVATE
        kvstore
)

# Persistence测试程序
add_executable(test_persistence
    test_persistence.cpp
)

target_link_libraries(test_persistence
    PRIVATE
        kvstore
)
