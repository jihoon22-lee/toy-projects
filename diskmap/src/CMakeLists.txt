# The core is Qt-free so it links anywhere and unit-tests without a
# QApplication.
add_library(diskmap_core STATIC
    cleanup.cpp
    duplicates.cpp
    duplicates_access.cpp
    duplicates_evidence.cpp
    duplicates_hash.cpp
    format.cpp
    fs_node.cpp
    fs_source.cpp
    scanner.cpp
    snapshot.cpp
    snapshot_diff.cpp
    snapshot_io.cpp
    snapshot_json.cpp
    snapshot_json_dom.cpp
    snapshot_json_parser.cpp
    snapshot_write.cpp
    trash.cpp
    trash_history.cpp
    trash_info.cpp
    trash_linux_fs.cpp
    trash_linux_ops.cpp
    trash_linux_restore.cpp
    trash_metadata.cpp
    treemap.cpp
    view.cpp
    view_facts.cpp
)

target_include_directories(diskmap_core PUBLIC ${PROJECT_SOURCE_DIR}/include)

# main.cpp is the CLI entry point and stays out of the library so tests can
# link it without a second main().
add_executable(diskmap main.cpp storage_cli.cpp storage_cli.hpp)
target_include_directories(diskmap PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(diskmap PRIVATE diskmap_core)

add_subdirectory(gui)
