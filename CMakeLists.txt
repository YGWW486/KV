cmake_minimum_required(VERSION 3.15)
project(KVStore VERSION 0.1.0 LANGUAGES CXX)

# ==============================================================================
# 基本配置
# ==============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ==============================================================================
# 平台特定配置
# ==============================================================================
if(WIN32)
    message(STATUS "Configuring for Windows")
    add_compile_definitions(_WIN32_WINNT=0x0601)
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
    add_compile_definitions(NOMINMAX)
    
    # MSVC 特定选项
    if(MSVC)
        add_compile_options(/W4)
        add_compile_options(/utf-8)
    endif()
else()
    message(STATUS "Configuring for Linux/Unix")
    # Linux 特定选项
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-Wall -Wextra -Wpedantic)
    endif()
endif()

# ==============================================================================
# 编译选项
# ==============================================================================
option(KV_BUILD_TESTS "Build unit tests" ON)
option(KV_BUILD_EXAMPLES "Build example programs" ON)
option(KV_ENABLE_ASAN "Enable AddressSanitizer" OFF)

# AddressSanitizer
if(KV_ENABLE_ASAN)
    if(MSVC)
        add_compile_options(/fsanitize=address)
        add_link_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()

# ==============================================================================
# 子目录
# ==============================================================================
add_subdirectory(src)

if(KV_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(KV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ==============================================================================
# 打印配置信息
# ==============================================================================
message(STATUS "")
message(STATUS "==========================================================")
message(STATUS "  KVStore Configuration Summary")
message(STATUS "==========================================================")
message(STATUS "  Version:              ${PROJECT_VERSION}")
message(STATUS "  C++ Standard:         ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:           ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Tests:          ${KV_BUILD_TESTS}")
message(STATUS "  Build Examples:       ${KV_BUILD_EXAMPLES}")
message(STATUS "  AddressSanitizer:     ${KV_ENABLE_ASAN}")
message(STATUS "==========================================================")
message(STATUS "")
