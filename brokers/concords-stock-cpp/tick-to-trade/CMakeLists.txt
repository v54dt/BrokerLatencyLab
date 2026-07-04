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

add_executable(fingerprint_test tests/fingerprint_test.cpp)
target_link_libraries(fingerprint_test PRIVATE GTest::gtest_main)

enable_testing()
include(GoogleTest)
gtest_discover_tests(fingerprint_test)
