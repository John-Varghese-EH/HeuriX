cmake_minimum_required(VERSION 3.20)
project(heurix-engine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TESTS "Build the doctest unit test suite" OFF)

# ------------------------------------------------------------------ sources

set(CORE_SOURCES
    src/core/entropy.cpp
    src/core/heuristic_engine.cpp
    src/core/event_bus.cpp
    src/core/api_server.cpp
    src/core/features.cpp
    src/core/ml_model.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_SOURCES
        src/platform/linux/fs_monitor.cpp
        src/platform/linux/process_mgr.cpp
        src/platform/linux/sys_stats.cpp
    )
    set(PLATFORM_LIBS pthread)
    find_package(OpenSSL REQUIRED)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PLATFORM_SOURCES
        src/platform/windows/fs_monitor.cpp
        src/platform/windows/process_mgr.cpp
        src/platform/windows/sys_stats.cpp
    )
    set(PLATFORM_LIBS ws2_32)
endif()

# ------------------------------------------------------------------ main engine

add_executable(heurix-engine
    src/main.cpp
    ${CORE_SOURCES}
    ${PLATFORM_SOURCES}
)
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

# ------------------------------------------------------------------ unit tests

if(BUILD_TESTS)
    # The test binary links the same core sources but uses its own main().
    add_executable(heurix-tests
        tests/test_heurix.cpp
        ${CORE_SOURCES}
        ${PLATFORM_SOURCES}
    )
    target_include_directories(heurix-tests PRIVATE include)
    target_link_libraries(heurix-tests PRIVATE ${PLATFORM_LIBS})

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(heurix-tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()

    if(MSVC)
        target_compile_options(heurix-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_definitions(heurix-tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    else()
        target_compile_options(heurix-tests PRIVATE -Wall -Wextra -Wpedantic -O2)
    endif()

    # Register with CTest so `ctest` works automatically.
    enable_testing()
    add_test(NAME HeuriXUnitTests COMMAND heurix-tests)
    set_tests_properties(HeuriXUnitTests PROPERTIES TIMEOUT 120)

    message(STATUS "Unit tests enabled. Build with: cmake --build <build_dir> -t heurix-tests")
endif()
