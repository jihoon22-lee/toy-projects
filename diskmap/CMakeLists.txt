cmake_minimum_required(VERSION 3.16)
project(diskmap LANGUAGES CXX)

option(DISKMAP_BUILD_BENCHMARKS "Build the opt-in generated-source scan benchmark" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt6 only. The GUI links Widgets/Concurrent; tests additionally link Test.
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Test)
message(STATUS "diskmap: using Qt6 ${Qt6_VERSION}")

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
if(DISKMAP_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

include(GNUInstallDirs)
install(TARGETS diskmap diskmap-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS diskmap_core ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY schemas/ DESTINATION ${CMAKE_INSTALL_DATADIR}/diskmap/schemas FILES_MATCHING PATTERN "*.json")
install(FILES README.md CHANGELOG.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/diskmap)
install(FILES packaging/diskmap.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/diskmap.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

option(DISKMAP_SANITIZE "Enable address and undefined behavior sanitizers" OFF)
if(DISKMAP_SANITIZE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(diskmap_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(diskmap_core PUBLIC -fsanitize=address,undefined)
    target_compile_options(diskmap_gui PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(diskmap_gui PUBLIC -fsanitize=address,undefined)
endif()
