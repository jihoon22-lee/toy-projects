cmake_minimum_required(VERSION 3.16)
project(buildscope VERSION 0.1.0 LANGUAGES CXX)  # x-release-please-version

option(BUILDSCOPE_BUILD_BENCHMARKS "Build the 100k-entry model benchmark" OFF)

include(GNUInstallDirs)

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

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Core Widgets Test)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Test)
message(STATUS "buildscope: using Qt6 ${QT6_VERSION}")

add_library(buildscope_contract STATIC
    src/core/compilation_model.cpp
    src/core/contract.cpp
    src/core/contract_json_guard.hpp
    src/core/contract_json_guard.cpp
    src/core/contract_loader.hpp
    src/core/contract_parser.hpp
    src/core/contract_parser.cpp
    src/core/contract_parser_v2.cpp
    src/core/diff_model.cpp
    src/core/diff_parser.cpp
    src/core/diff_validation.cpp
    src/core/diff_validation.hpp
    include/buildscope/compilation_model.hpp
    include/buildscope/contract.hpp
    include/buildscope/diff.hpp
    include/buildscope/diff_model.hpp
)
target_include_directories(buildscope_contract PUBLIC include)
target_link_libraries(buildscope_contract PUBLIC Qt${QT_VERSION_MAJOR}::Core)

add_library(buildscope_native STATIC
    src/native/canonical_json.cpp
    src/native/canonical_json.hpp
    src/native/native_command.cpp
    src/native/native_command.hpp
    src/native/native_diff.cpp
    src/native/native_diff.hpp
    src/native/native_diff_policy.cpp
    src/native/native_diff_policy.hpp
    src/native/native_error.hpp
    src/native/native_glob.cpp
    src/native/native_glob.hpp
    src/native/native_include.cpp
    src/native/native_include.hpp
    src/native/native_io.cpp
    src/native/native_io.hpp
    src/native/native_metadata.cpp
    src/native/native_metadata.hpp
    src/native/native_normalize.cpp
    src/native/native_normalize.hpp
    src/native/native_paths.cpp
    src/native/native_paths.hpp
    src/native/native_replay.cpp
    src/native/native_replay.hpp
    src/native/native_snapshot.cpp
    src/native/native_snapshot.hpp
)
target_include_directories(buildscope_native PUBLIC src/native)
target_link_libraries(buildscope_native PUBLIC buildscope_contract Qt${QT_VERSION_MAJOR}::Core)
target_compile_definitions(buildscope_native PUBLIC
    BUILDSCOPE_VERSION="${PROJECT_VERSION}"
)

add_executable(buildscope-cli src/core/cli_main.cpp)
target_link_libraries(buildscope-cli PRIVATE buildscope_contract)
set_target_properties(buildscope-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/core"
)

add_executable(buildscope src/native/native_cli.cpp)
target_link_libraries(buildscope PRIVATE buildscope_native)
set_target_properties(buildscope PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/native"
)

add_library(buildscope_gui STATIC
    src/gui/diff_window.cpp
    src/gui/main_window.cpp
    include/buildscope/main_window.hpp
    resources/buildscope.qrc
    ui/main_window.ui
)
target_include_directories(buildscope_gui PUBLIC include)
target_link_libraries(buildscope_gui PUBLIC buildscope_contract Qt${QT_VERSION_MAJOR}::Widgets)

add_executable(buildscope-gui
    src/gui/main.cpp
)
target_link_libraries(buildscope-gui PRIVATE buildscope_gui)
set_target_properties(buildscope-gui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/gui"
)

install(TARGETS buildscope buildscope-cli buildscope-gui
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    BUNDLE DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES
    README.md
    docs/quickstart.md
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
install(DIRECTORY examples/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/buildscope/examples"
)
install(DIRECTORY schemas/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/buildscope/schemas"
)

enable_testing()

add_executable(test_contract tests/cpp/test_contract.cpp)
target_include_directories(test_contract PRIVATE src/core)
target_link_libraries(test_contract PRIVATE buildscope_contract Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME test_contract COMMAND test_contract)

add_executable(test_compilation_model tests/cpp/test_compilation_model.cpp)
target_link_libraries(test_compilation_model PRIVATE
    buildscope_contract
    Qt${QT_VERSION_MAJOR}::Test
)
add_test(NAME test_compilation_model COMMAND test_compilation_model)

add_executable(test_diff tests/cpp/test_diff.cpp)
target_link_libraries(test_diff PRIVATE buildscope_contract Qt${QT_VERSION_MAJOR}::Test)
target_compile_definitions(test_diff PRIVATE
    BUILDSCOPE_SAMPLE_DIFF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample-diff.json"
)
add_test(NAME test_diff COMMAND test_diff)

add_executable(test_diff_validation tests/cpp/test_diff_validation.cpp)
target_link_libraries(test_diff_validation PRIVATE
    buildscope_contract
    Qt${QT_VERSION_MAJOR}::Test
)
target_compile_definitions(test_diff_validation PRIVATE
    BUILDSCOPE_SAMPLE_DIFF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample-diff.json"
)
add_test(NAME test_diff_validation COMMAND test_diff_validation)

add_executable(test_main_window tests/cpp/test_main_window.cpp)
target_link_libraries(test_main_window PRIVATE buildscope_gui Qt${QT_VERSION_MAJOR}::Test)
target_compile_definitions(test_main_window PRIVATE
    BUILDSCOPE_SAMPLE_SNAPSHOT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample.snapshot.json"
    BUILDSCOPE_V2_SNAPSHOT="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample-v2.snapshot.json"
    BUILDSCOPE_SAMPLE_DIFF="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample-diff.json"
)
add_test(NAME test_main_window COMMAND test_main_window)
set_tests_properties(test_main_window PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test_native tests/cpp/test_native.cpp)
target_link_libraries(test_native PRIVATE buildscope_native Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME test_native COMMAND test_native)

add_test(
    NAME test_hybrid_contract
    COMMAND ${CMAKE_COMMAND}
        -DPRODUCER=$<TARGET_FILE:buildscope>
        -DDATABASE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/compile_commands.json
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/tests/integration/snapshot.json
        -DCONSUMER=$<TARGET_FILE:buildscope-cli>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_hybrid_contract.cmake
)

add_test(
    NAME test_hybrid_include_contract
    COMMAND ${CMAKE_COMMAND}
        -DPRODUCER=$<TARGET_FILE:buildscope>
        -DCOMPILER=${CMAKE_CXX_COMPILER}
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/tests/integration/include-trace
        -DCONSUMER=$<TARGET_FILE:buildscope-cli>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_hybrid_include_contract.cmake
)

add_test(
    NAME test_hybrid_diff_contract
    COMMAND ${CMAKE_COMMAND}
        -DPRODUCER=$<TARGET_FILE:buildscope>
        -DBEFORE_DATABASE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/diff-before.compile_commands.json
        -DAFTER_DATABASE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/diff-after.compile_commands.json
        -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample-diff.json
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/tests/integration/diff.json
        -DCONSUMER=$<TARGET_FILE:buildscope-cli>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_hybrid_diff_contract.cmake
)

if(BUILDSCOPE_BUILD_BENCHMARKS)
    add_executable(buildscope-model-benchmark benchmarks/model_benchmark.cpp)
    target_link_libraries(buildscope-model-benchmark PRIVATE buildscope_contract)
    add_test(
        NAME buildscope_model_benchmark_100k
        COMMAND buildscope-model-benchmark 100000 10000
    )
    set_tests_properties(buildscope_model_benchmark_100k PROPERTIES LABELS benchmark)
endif()
