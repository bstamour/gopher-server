cmake_minimum_required(VERSION 3.30)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(gopher-server LANGUAGES CXX)

add_executable(gopher-server src/gopher_server.cxx)

find_package(Threads REQUIRED)
target_link_libraries(gopher-server PRIVATE Threads::Threads)

# Address sanitizer for debug builds.
target_compile_options(gopher-server PRIVATE
  $<$<CONFIG:Debug>:-fsanitize=address>
  $<$<CONFIG:Debug>:-fno-omit-frame-pointer>)

target_link_options(gopher-server PRIVATE
  $<$<CONFIG:Debug>:-fsanitize=address>)

target_compile_options(gopher-server PRIVATE
  $<$<CONFIG:Release>:-march=native>)
