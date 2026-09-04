cmake_minimum_required(VERSION 3.16)
project(loglens LANGUAGES CXX)

option(LOGLENS_BUILD_BENCHMARKS "Build opt-in large-file benchmark tools" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Use whichever supported Qt major is installed. Keeping the package discovery
# versioned makes the same source build on a Qt 6 desktop and a Qt 5.15 host.
# CMake treats a disabled package as a hard error when it appears in a REQUIRED
# NAMES list, so remove either explicitly disabled name before discovery. This
# lets CI prove both directions without relying on search-path ordering.
set(_qt_package_names Qt6 Qt5)
if(CMAKE_DISABLE_FIND_PACKAGE_Qt6)
    list(REMOVE_ITEM _qt_package_names Qt6)
endif()
if(CMAKE_DISABLE_FIND_PACKAGE_Qt5)
    list(REMOVE_ITEM _qt_package_names Qt5)
endif()
find_package(QT NAMES ${_qt_package_names} REQUIRED COMPONENTS Widgets Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Test)
message(STATUS "loglens: using Qt${QT_VERSION_MAJOR} ${QT_VERSION}")

# The core is Qt-free so it links anywhere; the GUI adds Qt on top.
add_library(loglens_core STATIC
    src/filter_expr.cpp
    src/highlight_rules.cpp
    src/initial_load.cpp
    src/json_line_parser.cpp
    src/json_string_parser.cpp
    src/log_parser.cpp
    src/log_record.cpp
    src/log_source.cpp
    src/log_stats.cpp
    src/ring_buffer.cpp
    src/persistence_io.cpp
    src/persistence_validation.cpp
    src/persistence_writer.cpp
    src/storage_json.cpp
    src/persistence.cpp
    src/persistence_io_posix.cpp
    src/persistence_io_windows.cpp
    src/timestamp_parser.cpp
    src/triage.cpp
    src/window_analysis.cpp
)
target_include_directories(loglens_core PUBLIC include)

add_executable(loglens src/main.cpp)
target_link_libraries(loglens PRIVATE loglens_core)

add_subdirectory(src/gui)

if(LOGLENS_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

enable_testing()

# GCC TSan cannot track QThread lifecycle synchronization performed inside the
# prebuilt Qt shared libraries. Running every worker-owning QtTest method in a
# fresh process avoids native thread-id reuse in TSan while retaining complete
# data-race instrumentation for every project method.
set(_loglens_tsan FALSE)
if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=thread")
    set(_loglens_tsan TRUE)
endif()
set(_qt_test_environment "QT_QPA_PLATFORM=offscreen")
if(_loglens_tsan)
    set(_qt_test_environment
        "QT_QPA_PLATFORM=offscreen;TSAN_OPTIONS=halt_on_error=1:report_thread_leaks=0:suppressions=${CMAKE_SOURCE_DIR}/ci/tsan.supp")
endif()

function(register_qt_test target)
    if(_loglens_tsan)
        foreach(test_case IN LISTS ARGN)
            if(test_case STREQUAL "__full__")
                add_test(NAME "${target}" COMMAND "${target}"
                         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
                set_tests_properties("${target}" PROPERTIES
                    ENVIRONMENT "${_qt_test_environment}")
                continue()
            endif()
            set(test_name "${target}.${test_case}")
            add_test(NAME "${test_name}" COMMAND "${target}" "${test_case}"
                     WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
            set_tests_properties("${test_name}" PROPERTIES
                ENVIRONMENT "${_qt_test_environment}")
        endforeach()
    else()
        add_test(NAME "${target}" COMMAND "${target}"
                 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        set_tests_properties("${target}" PROPERTIES
            ENVIRONMENT "${_qt_test_environment}")
    endif()
endfunction()

add_test(
    NAME test_cli_stream
    COMMAND ${CMAKE_COMMAND}
        -DLOGLENS=$<TARGET_FILE:loglens>
        -DINPUT=${CMAKE_SOURCE_DIR}/tests/data/sample.log
        -P ${CMAKE_SOURCE_DIR}/tests/test_cli_stream.cmake
)

foreach(name IN ITEMS
        filter_expr highlight_rules initial_load log_parser log_record log_source log_stats
        persistence ring_buffer triage window_analysis)
    add_executable(test_${name} tests/test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE loglens_core)
    target_include_directories(test_${name} PRIVATE tests)
    # Always the project root. The tests read fixtures by project-relative path,
    # and ici fixed that contract in C-9 — the adapter must not undo it.
    add_test(NAME test_${name} COMMAND test_${name} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()

# The Qt model test links the widget library and Qt Test. Before ici 0.6.0 this
# could not be verified at all: moc never ran, so LogModel's vtable was
# unresolved and the test did not link.
add_executable(test_log_model tests/test_log_model.cpp)
target_link_libraries(test_log_model PRIVATE loglens_gui Qt${QT_VERSION_MAJOR}::Test)
target_include_directories(test_log_model PRIVATE tests)
register_qt_test(test_log_model __full__)

# The loader's I/O and parser run on a dedicated QThread. This test exercises
# the real queued signal/ack protocol, rather than substituting a synchronous
# fake that could hide queued-batch or stale-job races.
add_executable(test_log_load_worker tests/test_log_load_worker.cpp)
target_link_libraries(test_log_load_worker PRIVATE loglens_gui Qt${QT_VERSION_MAJOR}::Test)
target_include_directories(test_log_load_worker PRIVATE tests)
register_qt_test(test_log_load_worker
    batchesAreBoundedAndWaitForAcknowledgement
    acknowledgementDrainsTheCompleteInitialLoad
    selectingANewJobCancelsAndSuppressesTheStaleJob)
# The remaining worker methods retain full normal/ASan coverage. Under GCC
# TSan with an uninstrumented Qt runtime they report allocator races wholly
# inside libQt6Core; startupTriageLoadReportsMalformedAndLegacyStores also
# creates two QThreads sequentially and triggers GCC TSan's native-id reuse
# CHECK. The three registered cases still exercise real queued batches,
# acknowledgement, continuation, and cross-thread cancellation under TSan.

# This small stress test covers the two direct cross-thread APIs without
# crossing Qt's uninstrumented queued-connection allocator. Full queued worker
# behavior remains covered by test_log_load_worker in normal and ASan builds.
add_executable(test_log_worker_atomic tests/test_log_worker_atomic.cpp)
target_link_libraries(test_log_worker_atomic PRIVATE loglens_gui)
add_test(NAME test_log_worker_atomic COMMAND test_log_worker_atomic
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# MainWindow owns the follow state machine and the source/model lifecycle. It
# links the same GUI library as the application so moc, widget ownership and
# timer connections are exercised rather than replaced with a test double.
add_executable(test_main_window tests/test_main_window.cpp)
target_link_libraries(test_main_window PRIVATE loglens_gui Qt${QT_VERSION_MAJOR}::Test)
target_include_directories(test_main_window PRIVATE tests)
register_qt_test(test_main_window
    openingAFileFillsTheTable
    growthIsObservedByTheFollowTimer
    truncationResetsStaleRows
    retryableSourceErrorKeepsFollowingAndVisibleRows
    sourceReplacementRecoversWithCleanRows
    disablingFollowWhileWaitingStopsPolling
    unsupportedSourceStopsFollowing
    failedOpenClearsThePreviousSourceState
    loaderSequenceMismatchStopsFollowing
    controlsHaveStableNamesAndFollowIsSwitchable
    timelineRendersEmptyAndPopulatedStates
    boundedStorageEvictsOldRowsAndReportsWindow
    drainsBacklogWithFollowDisabled
    searchAndFilterCanChangeDuringBackgroundLoading
    invalidFilterPreservesViewAndReportsVisibleByteRange
    loadProgressReportsFinalFromStartState
    loadProgressReportsInitialOpenError)

# Persistence controls are kept in a focused Qt test so the store contract is
# exercised through the same widgets and worker request as the application.
add_executable(test_gui_persistence tests/test_gui_persistence.cpp)
target_link_libraries(test_gui_persistence PRIVATE loglens_gui Qt${QT_VERSION_MAJOR}::Test)
target_include_directories(test_gui_persistence PRIVATE tests)
register_qt_test(test_gui_persistence
    loadsProfilesAndQueriesAndAppliesTheirSemantics
    savesEditedProfilesAndQueriesAndAppliesTheSavedQuery)

# The investigation workbench has its own end-to-end Qt test: highlight CRUD,
# bookmark/annotation persistence, lossless selection export, timeline range
# filtering and evidence navigation all use the production widgets.
add_executable(test_gui_triage tests/test_gui_triage.cpp)
target_link_libraries(test_gui_triage PRIVATE loglens_gui Qt${QT_VERSION_MAJOR}::Test)
target_include_directories(test_gui_triage PRIVATE tests)
register_qt_test(test_gui_triage
    timelinePublishesAndClearsHalfOpenRanges
    delegatePaintsHighlightedAndFallbackCells
    investigationWorkflowPersistsExportsAndNavigates
    emptyAndInvalidInvestigationActionsStaySafe
    diagnosticsAreRenderedAndExported)
