cmake_minimum_required(VERSION 3.16)
project(buildscope-cmake-example VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(buildscope-cmake-example
    src/main.cpp
    src/message.cpp
)
target_include_directories(buildscope-cmake-example PRIVATE include)
target_compile_definitions(buildscope-cmake-example PRIVATE BUILDSCOPE_CMAKE_EXAMPLE=1)
