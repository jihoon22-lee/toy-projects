cmake_minimum_required(VERSION 3.16)
project(loglens LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

# The core is Qt-free so it links anywhere; the GUI adds Qt on top.
add_library(loglens_core STATIC
    src/filter_expr.cpp
    src/highlight_rules.cpp
    src/log_parser.cpp
    src/log_record.cpp
    src/log_source.cpp
    src/log_stats.cpp
    src/ring_buffer.cpp
)
target_include_directories(loglens_core PUBLIC include)

add_executable(loglens src/main.cpp)
target_link_libraries(loglens PRIVATE loglens_core)

add_subdirectory(src/gui)

enable_testing()

add_test(
    NAME test_cli_stream
    COMMAND ${CMAKE_COMMAND}
        -DLOGLENS=$<TARGET_FILE:loglens>
        -DINPUT=${CMAKE_SOURCE_DIR}/tests/data/sample.log
        -P ${CMAKE_SOURCE_DIR}/tests/test_cli_stream.cmake
)

foreach(name IN ITEMS
        filter_expr highlight_rules log_parser log_record log_source log_stats ring_buffer)
    add_executable(test_${name} tests/test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE loglens_core)
    target_include_directories(test_${name} PRIVATE tests)
    # Always the project root. The tests read fixtures by project-relative path,
    # and ici fixed that contract in C-9 — the adapter must not undo it.
    add_test(NAME test_${name} COMMAND test_${name} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()

# The Qt model test links the widget library and Qt Test. Before ici 0.6.0 this
# could not be verified at all: moc never ran, so LogModel's vtable was
# unresolved and the test did not link.
add_executable(test_log_model tests/test_log_model.cpp)
target_link_libraries(test_log_model PRIVATE loglens_gui Qt6::Test)
target_include_directories(test_log_model PRIVATE tests)
add_test(NAME test_log_model COMMAND test_log_model WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
