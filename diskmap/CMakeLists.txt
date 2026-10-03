cmake_minimum_required(VERSION 3.16)
project(diskmap LANGUAGES CXX)

option(DISKMAP_BUILD_BENCHMARKS "Build the opt-in generated-source scan benchmark" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt6 only. The GUI links Widgets/Concurrent; tests additionally link Test.
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Test)
message(STATUS "diskmap: using Qt6 ${Qt6_VERSION}")

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
if(DISKMAP_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
