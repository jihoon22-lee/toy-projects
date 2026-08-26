cmake_minimum_required(VERSION 3.16)
project(loglens_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# The core is deliberately Qt-free — that is what lets ici verify it — so the
# GUI links it as a plain static library. Sources are listed rather than
# globbed so adding one to the core is a conscious act here too.
add_library(loglens_core STATIC
    ../src/filter_expr.cpp
    ../src/highlight_rules.cpp
    ../src/log_parser.cpp
    ../src/log_record.cpp
    ../src/log_source.cpp
    ../src/log_stats.cpp
    ../src/ring_buffer.cpp
)
target_include_directories(loglens_core PUBLIC ../include)

add_executable(loglens-gui
    main.cpp
    log_model.cpp
    timeline_widget.cpp
    main_window.cpp
)
target_link_libraries(loglens-gui PRIVATE loglens_core Qt6::Widgets)
