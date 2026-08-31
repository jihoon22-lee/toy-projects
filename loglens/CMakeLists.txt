cmake_minimum_required(VERSION 3.16)
project(loglens LANGUAGES CXX)

option(LOGLENS_BUILD_BENCHMARKS "Build opt-in large-file benchmark tools" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Use whichever supported Qt major is installed. Keeping the package discovery
# versioned makes the same source build on a Qt 6 desktop and a Qt 5.15 host.
# CMake treats a disabled package as a hard error when it appears in a REQUIRED
# NAMES list, so remove either explicitly disabled name before discovery. This
# lets CI prove both directions without relying on search-path ordering.
set(_qt_package_names Qt6 Qt5)
if(CMAKE_DISABLE_FIND_PACKAGE_Qt6)
    list(REMOVE_ITEM _qt_package_names Qt6)
endif()
if(CMAKE_DISABLE_FIND_PACKAGE_Qt5)
    list(REMOVE_ITEM _qt_package_names Qt5)
endif()
find_package(QT NAMES ${_qt_package_names} REQUIRED COMPONENTS Widgets Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Test)
message(STATUS "loglens: using Qt${QT_VERSION_MAJOR} ${QT_VERSION}")

# The core is Qt-free so it links anywhere; the GUI adds Qt on top.
add_library(loglens_core STATIC
    src/filter_expr.cpp
    src/highlight_rules.cpp
    src/initial_load.cpp
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

if(LOGLENS_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

enable_testing()

add_test(
    NAME test_cli_stream
    COMMAND ${CMAKE_COMMAND}
        -DLOGLENS=$<TARGET_FILE:loglens>
        -DINPUT=${CMAKE_SOURCE_DIR}/tests/data/sample.log
        -P ${CMAKE_SOURCE_DIR}/tests/test_cli_stream.cmake
)

foreach(name IN ITEMS
        filter_expr highlight_rules initial_load log_parser log_record log_source log_stats ring_buffer)
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
target_link_libraries(test_log_model PRIVATE loglens_gui Qt${QT_VERSION_MAJOR}::Test)
target_include_directories(test_log_model PRIVATE tests)
add_test(NAME test_log_model COMMAND test_log_model WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# The loader's I/O and parser run on a dedicated QThread. This test exercises
# the real queued signal/ack protocol, rather than substituting a synchronous
# fake that could hide queued-batch or stale-job races.
add_executable(test_log_load_worker tests/test_log_load_worker.cpp)
target_link_libraries(test_log_load_worker PRIVATE loglens_gui Qt${QT_VERSION_MAJOR}::Test)
target_include_directories(test_log_load_worker PRIVATE tests)
add_test(NAME test_log_load_worker COMMAND test_log_load_worker WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# MainWindow owns the follow state machine and the source/model lifecycle. It
# links the same GUI library as the application so moc, widget ownership and
# timer connections are exercised rather than replaced with a test double.
add_executable(test_main_window tests/test_main_window.cpp)
target_link_libraries(test_main_window PRIVATE loglens_gui Qt${QT_VERSION_MAJOR}::Test)
target_include_directories(test_main_window PRIVATE tests)
add_test(NAME test_main_window COMMAND test_main_window WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Widget tests are intentionally headless in every environment. This keeps
# CTest reproducible even when a developer happens to have no display server.
set_tests_properties(test_log_model test_main_window PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
set_tests_properties(test_log_load_worker PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
