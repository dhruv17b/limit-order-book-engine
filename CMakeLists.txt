cmake_minimum_required(VERSION 3.14)
project(OrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(ob_tests tests/matching_test.cpp)
target_link_libraries(ob_tests GTest::gtest_main)
target_include_directories(ob_tests PRIVATE include)

include(GoogleTest)
gtest_discover_tests(ob_tests)
add_executable(replay src/replay.cpp)
target_include_directories(replay PRIVATE include)
add_executable(benchmark src/benchmark.cpp)
target_include_directories(benchmark PRIVATE include)
add_executable(server src/server.cpp)
target_include_directories(server PRIVATE include)
add_executable(client src/client.cpp)
target_include_directories(client PRIVATE include)