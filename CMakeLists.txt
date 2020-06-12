cmake_minimum_required(VERSION 3.12)

project(connectivity-monitor)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

include(FindCURL)
find_package(CURL REQUIRED)

include_directories(curlpp/include)
include_directories(include)

link_directories(curlpp/build)

add_executable(connectivity-monitor main.cpp)
target_link_libraries(connectivity-monitor curlpp)
target_link_libraries(connectivity-monitor pthread)
target_link_libraries(connectivity-monitor ${CURL_LIBRARIES})
