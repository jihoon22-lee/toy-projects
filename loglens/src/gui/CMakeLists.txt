# Included from the project root, which already selected the supported Qt major
# and defines loglens_core. The widgets live in a library rather than only in
# the executable so tests can link them with the same moc-generated sources.

# The headers are listed explicitly because they sit under include/ rather than
# beside their .cpp. AUTOMOC finds a Q_OBJECT header on its own only when it
# shares a directory and a basename with a source in the target; anywhere else
# it has to be named here or moc never runs on it.
add_library(loglens_gui STATIC
    log_load_worker.cpp
    log_model.cpp
    main_window.cpp
    main_window_persistence.cpp
    timeline_widget.cpp
    ${CMAKE_SOURCE_DIR}/include/loglens/gui/log_load_worker.hpp
    ${CMAKE_SOURCE_DIR}/include/loglens/gui/log_model.hpp
    ${CMAKE_SOURCE_DIR}/include/loglens/gui/main_window.hpp
    ${CMAKE_SOURCE_DIR}/include/loglens/gui/timeline_widget.hpp
)
# Include paths come from loglens_core, which exports include/ publicly.
target_link_libraries(loglens_gui PUBLIC loglens_core Qt${QT_VERSION_MAJOR}::Widgets)

add_executable(loglens-gui gui_main.cpp)
target_link_libraries(loglens-gui PRIVATE loglens_gui)
