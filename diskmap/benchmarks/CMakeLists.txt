add_executable(diskmap-scan-benchmark scan_benchmark.cpp)
target_include_directories(diskmap-scan-benchmark PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_options(diskmap-scan-benchmark PRIVATE -Wall -Wextra -Werror)
target_link_libraries(diskmap-scan-benchmark PRIVATE diskmap_core)
