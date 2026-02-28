cmake_minimum_required(VERSION 3.14)
project(ConcordsStockLatency)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

add_library(latency_core STATIC
    src/latency_measurement.cpp
)

target_include_directories(latency_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
)

target_link_libraries(latency_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/concords_api_stockclient.so
    pthread
    tomlplusplus::tomlplusplus
)

add_executable(main
    cmd/main.cpp
)

target_link_libraries(main PRIVATE
    latency_core
)
