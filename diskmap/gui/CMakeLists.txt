cmake_minimum_required(VERSION 3.16)
project(diskmap_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

# The core is deliberately Qt-free: it is what ici verifies, and keeping Qt out
# of it is what lets every engine run. The GUI links it as a plain static
# library. Note the sources are listed rather than globbed, so adding a file to
# the core is a conscious act here too.
add_library(diskmap_core STATIC
    ../src/format.cpp
    ../src/fs_node.cpp
    ../src/fs_source.cpp
    ../src/scanner.cpp
    ../src/treemap.cpp
)
target_include_directories(diskmap_core PUBLIC ../include)

add_executable(diskmap-gui
    main.cpp
    treemap_widget.cpp
    main_window.cpp
)
target_link_libraries(diskmap-gui PRIVATE diskmap_core Qt6::Widgets Qt6::Concurrent)
