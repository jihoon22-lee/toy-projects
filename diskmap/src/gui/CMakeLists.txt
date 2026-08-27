cmake_minimum_required(VERSION 3.16)
project(diskmap_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

# The core is Qt-free so it can be unit-tested and linked anywhere; the GUI adds
# Qt on top. Both live under src/ and both are analysed by ici — only the link
# step differs, which is what project.cpp_external_build_dirs declares.
add_library(diskmap_core STATIC
    ../format.cpp
    ../fs_node.cpp
    ../fs_source.cpp
    ../scanner.cpp
    ../treemap.cpp
)
target_include_directories(diskmap_core PUBLIC ../../include)

add_executable(diskmap-gui
    gui_main.cpp
    treemap_widget.cpp
    main_window.cpp
)
target_link_libraries(diskmap-gui PRIVATE diskmap_core Qt6::Widgets Qt6::Concurrent)
