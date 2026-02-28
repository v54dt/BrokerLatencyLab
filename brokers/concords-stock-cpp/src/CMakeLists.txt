cmake_minimum_required(VERSION 3.14)
project(streambeaver C CXX)
get_filename_component(PROJECT_FOLDER "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_library(broker.concords-stock-cpp.src STATIC   "${PROJECT_FOLDER}/concords-stock-cpp/src/latency_measurement.cpp")

target_include_directories(broker.concords-stock-cpp.src PRIVATE ${PROJECT_FOLDER}/concords-stock-cpp/src)
target_include_directories(broker.concords-stock-cpp.src PRIVATE ${PROJECT_FOLDER}/concords-stock-cpp/lib/stock-client)
target_link_libraries(broker.concords-stock-cpp.src PRIVATE ${PROJECT_FOLDER}/concords-stock-cpp/lib/concords_api_stockclient.so)
