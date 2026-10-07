cmake_minimum_required(VERSION 3.20)
project(heurix-engine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CORE_SOURCES src/core/entropy.cpp src/core/heuristic_engine.cpp src/core/event_bus.cpp src/core/grpc_server.cpp src/core/features.cpp src/core/ml_model.cpp)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_SOURCES src/platform/linux/fs_monitor.cpp src/platform/linux/process_mgr.cpp src/platform/linux/sys_stats.cpp)
    set(PLATFORM_LIBS pthread)
    find_package(OpenSSL REQUIRED)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PLATFORM_SOURCES src/platform/windows/fs_monitor.cpp src/platform/windows/process_mgr.cpp src/platform/windows/sys_stats.cpp)
    set(PLATFORM_LIBS)
endif()

# Find gRPC and Protobuf via vcpkg
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

# Protobuf and gRPC generation
set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/proto/heurix.proto")
set(PROTO_SRC "${CMAKE_CURRENT_BINARY_DIR}/heurix.pb.cc")
set(PROTO_HDR "${CMAKE_CURRENT_BINARY_DIR}/heurix.pb.h")
set(GRPC_SRC "${CMAKE_CURRENT_BINARY_DIR}/heurix.grpc.pb.cc")
set(GRPC_HDR "${CMAKE_CURRENT_BINARY_DIR}/heurix.grpc.pb.h")

add_custom_command(
    OUTPUT ${PROTO_SRC} ${PROTO_HDR} ${GRPC_SRC} ${GRPC_HDR}
    COMMAND protobuf::protoc
    ARGS --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
         --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
         -I ${CMAKE_CURRENT_SOURCE_DIR}/proto
         --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
         ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
)

add_executable(heurix-engine src/main.cpp ${CORE_SOURCES} ${PLATFORM_SOURCES} ${PROTO_SRC} ${GRPC_SRC})
target_include_directories(heurix-engine PRIVATE include ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(heurix-engine PRIVATE ${PLATFORM_LIBS} gRPC::grpc++ protobuf::libprotobuf)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(heurix-engine PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

if(MSVC)
    target_compile_options(heurix-engine PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(heurix-engine PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
else()
    target_compile_options(heurix-engine PRIVATE -Wall -Wextra -Wpedantic -O2)
endif()