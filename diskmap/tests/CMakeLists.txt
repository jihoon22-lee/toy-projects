# Each test file is its own Qt Test executable, so every test reports under
# its own CTest name. Qt-free tests use QTEST_GUILESS_MAIN; widget tests use
# QTEST_MAIN.
set(QTLESS_TESTS
    test_cleanup
    test_cli_version
    test_duplicates
    test_format
    test_fs_node
    test_fs_source
    test_scanner
    test_scanner_real_safety
    test_scanner_safety
    test_snapshot
    test_storage_cli
    test_treemap
    test_trash
    test_view
)

foreach(test_name IN LISTS QTLESS_TESTS)
    add_executable(${test_name} ${test_name}.cpp)
    target_include_directories(${test_name} PRIVATE
        ${PROJECT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${test_name} PRIVATE diskmap_core Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# storage_cli.cpp lives next to the CLI entry point, not in the core library.
target_sources(test_storage_cli PRIVATE ${PROJECT_SOURCE_DIR}/src/storage_cli.cpp)
target_include_directories(test_storage_cli PRIVATE ${PROJECT_SOURCE_DIR}/src)

# Runs the linked CLI rather than linking its translation unit, so it needs
# the binary's path.
target_compile_definitions(test_cli_version PRIVATE
    DISKMAP_BINARY="$<TARGET_FILE:diskmap>")

set(GUI_TESTS
    test_portfolio_features
    test_main_window
    test_node_table_model
    test_storage_workbench
    test_treemap_widget
)

foreach(test_name IN LISTS GUI_TESTS)
    add_executable(${test_name} ${test_name}.cpp)
    target_include_directories(${test_name} PRIVATE
        ${PROJECT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${test_name} PRIVATE
        diskmap_gui diskmap_core Qt6::Widgets Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

target_link_libraries(test_main_window PRIVATE Qt6::Concurrent)
target_link_libraries(test_storage_workbench PRIVATE Qt6::Concurrent)
