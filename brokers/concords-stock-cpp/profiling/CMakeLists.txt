cmake_minimum_required(VERSION 3.14)
project(PlaceOrderProfiler)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

add_executable(submit_order submit_order.cpp)

target_include_directories(submit_order PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(submit_order PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/concords_sdk.so
    pthread
    tomlplusplus::tomlplusplus
)
