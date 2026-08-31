cmake_minimum_required(VERSION 3.16)
project(buildscope VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/ui")

if(MSVC)
    add_compile_options(/W4 /WX /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

set(_qt_package_names Qt6 Qt5)
if(CMAKE_DISABLE_FIND_PACKAGE_Qt6)
    list(REMOVE_ITEM _qt_package_names Qt6)
endif()
if(CMAKE_DISABLE_FIND_PACKAGE_Qt5)
    list(REMOVE_ITEM _qt_package_names Qt5)
endif()
find_package(QT NAMES ${_qt_package_names} REQUIRED COMPONENTS Core Widgets Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Test)
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
message(STATUS "buildscope: using Qt${QT_VERSION_MAJOR} ${QT_VERSION}")

add_library(buildscope_contract STATIC
    src/core/contract.cpp
    include/buildscope/contract.hpp
)
target_include_directories(buildscope_contract PUBLIC include)
target_link_libraries(buildscope_contract PUBLIC Qt${QT_VERSION_MAJOR}::Core)

add_executable(buildscope-cli src/core/cli_main.cpp)
target_link_libraries(buildscope-cli PRIVATE buildscope_contract)
set_target_properties(buildscope-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/core"
)

add_library(buildscope_gui STATIC
    src/gui/main_window.cpp
    include/buildscope/main_window.hpp
    ui/main_window.ui
)
target_include_directories(buildscope_gui PUBLIC include)
target_link_libraries(buildscope_gui PUBLIC buildscope_contract Qt${QT_VERSION_MAJOR}::Widgets)

add_executable(buildscope-gui
    src/gui/main.cpp
    resources/buildscope.qrc
)
target_link_libraries(buildscope-gui PRIVATE buildscope_gui)
set_target_properties(buildscope-gui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/gui"
)

enable_testing()

add_test(
    NAME buildscope_python
    COMMAND ${CMAKE_COMMAND} -E env
        "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python"
        ${Python3_EXECUTABLE} -m unittest discover
        -s "${CMAKE_CURRENT_SOURCE_DIR}/tests/python"
        -p "test_*.py"
)

add_executable(test_contract tests/cpp/test_contract.cpp)
target_link_libraries(test_contract PRIVATE buildscope_contract Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME test_contract COMMAND test_contract)

add_executable(test_main_window tests/cpp/test_main_window.cpp)
target_link_libraries(test_main_window PRIVATE buildscope_gui Qt${QT_VERSION_MAJOR}::Test)
target_compile_definitions(test_main_window PRIVATE
    BUILDSCOPE_SAMPLE_SNAPSHOT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample.snapshot.json"
)
add_test(NAME test_main_window COMMAND test_main_window)
set_tests_properties(test_main_window PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(
    NAME test_hybrid_contract
    COMMAND ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
        -DPYTHON_PACKAGE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/python
        -DDATABASE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/compile_commands.json
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/tests/integration/snapshot.json
        -DCONSUMER=$<TARGET_FILE:buildscope-cli>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_hybrid_contract.cmake
)
