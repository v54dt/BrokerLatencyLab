cmake_minimum_required(VERSION 3.14)
project(TickToTrade CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../lib
                    ${CMAKE_CURRENT_SOURCE_DIR}/../src)

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0)
FetchContent_MakeAvailable(googletest)
FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG v3.4.0)
FetchContent_MakeAvailable(tomlplusplus)

add_executable(fingerprint_test tests/fingerprint_test.cpp)
target_link_libraries(fingerprint_test PRIVATE GTest::gtest_main)

add_executable(replay_test tests/replay_test.cpp round_engine.cpp)
target_link_libraries(replay_test PRIVATE GTest::gtest_main pthread)

add_executable(tick-to-trade main.cpp round_engine.cpp)
target_link_libraries(
  tick-to-trade PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../lib/concords_sdk.so
                        tomlplusplus::tomlplusplus pthread)

add_executable(cancel_all cancel_all.cpp)
target_link_libraries(
  cancel_all PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../lib/concords_sdk.so
                     tomlplusplus::tomlplusplus pthread)

enable_testing()
include(GoogleTest)
gtest_discover_tests(fingerprint_test)
gtest_discover_tests(replay_test)
