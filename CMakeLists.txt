cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(gopher-server LANGUAGES CXX)

add_executable(gopher-server src/gopher_server.cxx)

find_package(Threads REQUIRED)
target_link_libraries(gopher-server PRIVATE Threads::Threads)
