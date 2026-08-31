set(_benchmark_warnings -Wall -Wextra -Wpedantic -Werror)

add_library(loglens_benchmark_common STATIC EXCLUDE_FROM_ALL
    benchmark_common.cpp
)
target_include_directories(loglens_benchmark_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(loglens_benchmark_common PRIVATE ${_benchmark_warnings})

add_executable(loglens-bench-generate EXCLUDE_FROM_ALL generate.cpp)
target_compile_options(loglens-bench-generate PRIVATE ${_benchmark_warnings})

add_executable(loglens-bench-core EXCLUDE_FROM_ALL core_benchmark.cpp)
target_link_libraries(loglens-bench-core PRIVATE loglens_core loglens_benchmark_common)
target_compile_options(loglens-bench-core PRIVATE ${_benchmark_warnings})

add_executable(loglens-bench-gui EXCLUDE_FROM_ALL gui_benchmark.cpp)
target_link_libraries(loglens-bench-gui PRIVATE loglens_gui loglens_benchmark_common
                                                   Qt${QT_VERSION_MAJOR}::Widgets)
target_compile_options(loglens-bench-gui PRIVATE ${_benchmark_warnings})
