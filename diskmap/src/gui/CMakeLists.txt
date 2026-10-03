# The widgets live in a library rather than only in the executable so tests
# can link them. Headers sit under include/diskmap/gui and are listed
# explicitly so AUTOMOC can rely on target sources alone.
add_library(diskmap_gui STATIC
    explorer_text.cpp
    explorer_text.hpp
    main_window.cpp
    main_window_cleanup.cpp
    main_window_filter_data.hpp
    main_window_projection.cpp
    main_window_storage.cpp
    main_window_ui.cpp
    node_table_model.cpp
    node_table_model_presentation.cpp
    treemap_widget.cpp
    ${PROJECT_SOURCE_DIR}/include/diskmap/gui/main_window.hpp
    ${PROJECT_SOURCE_DIR}/include/diskmap/gui/node_key_metatype.hpp
    ${PROJECT_SOURCE_DIR}/include/diskmap/gui/node_table_model.hpp
    ${PROJECT_SOURCE_DIR}/include/diskmap/gui/treemap_widget.hpp
)

target_include_directories(diskmap_gui PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(diskmap_gui
    PUBLIC Qt6::Widgets Qt6::Concurrent
    PRIVATE diskmap_core
)

add_executable(diskmap-gui gui_main.cpp)
target_link_libraries(diskmap-gui PRIVATE diskmap_gui diskmap_core)
