cmake_minimum_required(VERSION 3.12)

set(CMAKE_CXX_STANDARD 17)

include(FindCURL)
find_package(CURL REQUIRED)

include_directories(curlpp/include)

link_directories(curlpp/build/)

add_executable(connectivity main.cpp)
target_link_libraries(connectivity ${CURL_LIBRARIES})
target_link_libraries(connectivity curlpp)
