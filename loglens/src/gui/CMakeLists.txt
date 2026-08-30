# Included from the project root, which already defines loglens_core.
#
# The widgets live in a library rather than only in the executable so tests can
# link them. A Q_OBJECT class needs moc-generated sources, which is exactly what
# ici could not provide before the build adapter existed.
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_library(loglens_gui STATIC
    log_model.cpp
    main_window.cpp
    timeline_widget.cpp
)
target_include_directories(loglens_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(loglens_gui PUBLIC loglens_core Qt6::Widgets)

add_executable(loglens-gui gui_main.cpp)
target_link_libraries(loglens-gui PRIVATE loglens_gui)
