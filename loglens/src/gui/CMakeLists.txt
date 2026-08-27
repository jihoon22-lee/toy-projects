cmake_minimum_required(VERSION 3.16)
project(loglens_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# The core is Qt-free so it can be unit-tested and linked anywhere; the GUI adds
# Qt on top. Both live under src/ and both are analysed by ici — only the link
# step differs, which is what project.cpp_external_build_dirs declares.
add_library(loglens_core STATIC
    ../filter_expr.cpp
    ../highlight_rules.cpp
    ../log_parser.cpp
    ../log_record.cpp
    ../log_source.cpp
    ../log_stats.cpp
    ../ring_buffer.cpp
)
target_include_directories(loglens_core PUBLIC ../../include)

add_executable(loglens-gui
    gui_main.cpp
    log_model.cpp
    timeline_widget.cpp
    main_window.cpp
)
target_link_libraries(loglens-gui PRIVATE loglens_core Qt6::Widgets)
