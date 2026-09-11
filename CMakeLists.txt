cmake_minimum_required(VERSION 3.20)
project(heurix-engine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CORE_SOURCES src/core/entropy.cpp src/core/heuristic_engine.cpp src/core/event_bus.cpp)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_SOURCES src/platform/linux/fs_monitor.cpp src/platform/linux/process_mgr.cpp src/platform/linux/sys_stats.cpp)
    set(PLATFORM_LIBS pthread)

    # Find OpenSSL for SHA256 support
    find_package(OpenSSL REQUIRED)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PLATFORM_SOURCES src/platform/windows/fs_monitor.cpp src/platform/windows/process_mgr.cpp src/platform/windows/sys_stats.cpp)
    set(PLATFORM_LIBS)
endif()

add_executable(heurix-engine src/main.cpp ${CORE_SOURCES} ${PLATFORM_SOURCES})
target_include_directories(heurix-engine PRIVATE include)
target_link_libraries(heurix-engine PRIVATE ${PLATFORM_LIBS})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(heurix-engine PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

if(MSVC)
    target_compile_options(heurix-engine PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(heurix-engine PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
else()
    target_compile_options(heurix-engine PRIVATE -Wall -Wextra -Wpedantic -O2)
endif()