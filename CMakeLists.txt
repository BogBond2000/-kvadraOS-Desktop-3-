cmake_minimum_required(VERSION 3.16)
project(accelerometer_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/sensor_data.proto)

add_library(accel_proto ${PROTO_FILE})
target_link_libraries(accel_proto
        PUBLIC
        gRPC::grpc++
        protobuf::libprotobuf)
target_include_directories(accel_proto
        PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/proto)

get_target_property(GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin LOCATION)
protobuf_generate(TARGET accel_proto LANGUAGE cpp)
protobuf_generate(
        TARGET accel_proto
        LANGUAGE grpc
        GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
        PLUGIN "protoc-gen-grpc=${GRPC_CPP_PLUGIN}")

add_library(accel_common
        common/duplicate_filter.cpp
        common/module_logger.cpp
        common/security_config.cpp)
target_include_directories(accel_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(accel_common PUBLIC accel_proto)

add_library(accel_server_core
        server/service_impl.cpp
        server/mediator.cpp)
target_include_directories(accel_server_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(accel_server_core PUBLIC accel_common accel_proto)

add_library(accel_client_a_core
        client_a/sender_client.cpp)
target_include_directories(accel_client_a_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(accel_client_a_core PUBLIC accel_common accel_proto)

add_library(accel_client_b_core
        client_b/processor_client.cpp)
target_include_directories(accel_client_b_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(accel_client_b_core PUBLIC accel_common accel_proto)

add_executable(accel_server
        server/main.cpp)
target_link_libraries(accel_server PRIVATE accel_server_core)

add_executable(accel_client_a
        client_a/main.cpp)
target_link_libraries(accel_client_a PRIVATE accel_client_a_core)

add_executable(accel_client_b
        client_b/main.cpp)
target_link_libraries(accel_client_b PRIVATE accel_client_b_core)

include(CTest)

if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(accel_unit_tests
            tests/unit_tests.cpp)
    target_link_libraries(accel_unit_tests
            PRIVATE
            gtest_main
            accel_common)
    target_include_directories(accel_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME accel_unit_tests COMMAND accel_unit_tests)

    add_executable(accel_model_tests
            tests/model_tests.cpp)
    target_link_libraries(accel_model_tests
            PRIVATE
            gtest_main
            accel_server_core
            accel_client_a_core
            accel_client_b_core)
    target_include_directories(accel_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME accel_model_tests COMMAND accel_model_tests)

    add_executable(accel_tls_tests
            tests/tls_tests.cpp)
    target_link_libraries(accel_tls_tests
            PRIVATE
            gtest_main
            accel_server_core
            accel_client_a_core
            accel_client_b_core)
    target_include_directories(accel_tls_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(accel_tls_tests PRIVATE ACCEL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME accel_tls_tests COMMAND accel_tls_tests)
endif()
