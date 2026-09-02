execute_process(
    COMMAND "${LOGLENS}" "${INPUT}" --format auto
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "loglens CLI failed (${result}): ${error}")
endif()

foreach(expected IN ITEMS
        "3  ERROR  api  upstream timeout contacting billing"
        "    at Connection.read(conn.cpp:88)"
        "6  INFO  api  request 9999 served in 8ms"
        "9 / 9 line(s)")
    string(FIND "${output}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "missing expected CLI output '${expected}':\n${output}")
    endif()
endforeach()

execute_process(
    COMMAND "${LOGLENS}" "${INPUT}" --format auto --capacity 2
    RESULT_VARIABLE bounded_result
    OUTPUT_VARIABLE bounded_output
    ERROR_VARIABLE bounded_error
)

if(NOT bounded_result EQUAL 0)
    message(FATAL_ERROR "bounded loglens CLI failed (${bounded_result}): ${bounded_error}")
endif()

foreach(expected IN ITEMS
        "10  UNKNOWN  nginx  request served"
        "11  FATAL  api  shutting down after repeated failures"
        "2 / 2 line(s)"
        "9 seen, 7 dropped, lines 10-11, capacity 2")
    string(FIND "${bounded_output}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "missing bounded CLI output '${expected}':\n${bounded_output}")
    endif()
endforeach()

string(FIND "${bounded_output}" "upstream timeout contacting billing" stale_position)
if(NOT stale_position EQUAL -1)
    message(FATAL_ERROR "bounded CLI leaked an evicted record:\n${bounded_output}")
endif()

execute_process(
    COMMAND "${LOGLENS}" "${INPUT}" --format auto --capacity 2 --stats
    RESULT_VARIABLE stats_result
    OUTPUT_VARIABLE stats_output
    ERROR_VARIABLE stats_error
)

if(NOT stats_result EQUAL 0)
    message(FATAL_ERROR "bounded loglens stats failed (${stats_result}): ${stats_error}")
endif()

foreach(expected IN ITEMS
        "2 matching line(s)"
        "9 seen, 7 dropped, lines 10-11, capacity 2")
    string(FIND "${stats_output}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "missing bounded stats output '${expected}':\n${stats_output}")
    endif()
endforeach()

# Force the one-shot path across the default 1 MiB source chunk boundary. The
# fixed record window must retain the final logical rows while the CLI drains
# exactly the size observed by its first source snapshot.
string(REPEAT "raw record\n" 100000 large_contents)
set(large_input "${CMAKE_CURRENT_BINARY_DIR}/loglens-large-stream.log")
file(WRITE "${large_input}" "${large_contents}")
execute_process(
    COMMAND "${LOGLENS}" "${large_input}" --format auto --capacity 2
    RESULT_VARIABLE large_result
    OUTPUT_VARIABLE large_output
    ERROR_VARIABLE large_error
)
file(REMOVE "${large_input}")

if(NOT large_result EQUAL 0)
    message(FATAL_ERROR "multi-chunk loglens CLI failed (${large_result}): ${large_error}")
endif()

foreach(expected IN ITEMS
        "99999  UNKNOWN    raw record"
        "100000  UNKNOWN    raw record"
        "100000 seen, 99998 dropped, lines 99999-100000, capacity 2")
    string(FIND "${large_output}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "missing multi-chunk output '${expected}':\n${large_output}")
    endif()
endforeach()

foreach(invalid_capacity IN ITEMS 0 1000001 not-a-number)
    execute_process(
        COMMAND "${LOGLENS}" "${INPUT}" --capacity "${invalid_capacity}"
        RESULT_VARIABLE invalid_result
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(invalid_result EQUAL 0)
        message(FATAL_ERROR "invalid --capacity ${invalid_capacity} was accepted")
    endif()
endforeach()

execute_process(
    COMMAND "${LOGLENS}" "${INPUT}" --filter "level>=WARN extra"
    RESULT_VARIABLE invalid_filter_result
    OUTPUT_QUIET
    ERROR_VARIABLE invalid_filter_error
)
if(invalid_filter_result EQUAL 0)
    message(FATAL_ERROR "invalid filter was accepted")
endif()
string(FIND "${invalid_filter_error}"
       "fatal: bad filter at bytes [12,17): unexpected trailing input"
       invalid_filter_position)
if(invalid_filter_position EQUAL -1)
    message(FATAL_ERROR "missing CLI filter diagnostic range:\n${invalid_filter_error}")
endif()

string(REPEAT "x" 65540 oversized_record)
set(oversized_input "${CMAKE_CURRENT_BINARY_DIR}/loglens-oversized-record.log")
file(WRITE "${oversized_input}" "${oversized_record}\n")
execute_process(
    COMMAND "${LOGLENS}" "${oversized_input}" --format auto
    RESULT_VARIABLE oversized_result
    OUTPUT_VARIABLE oversized_output
    ERROR_VARIABLE oversized_error
)
file(REMOVE "${oversized_input}")
if(NOT oversized_result EQUAL 0)
    message(FATAL_ERROR "oversized record CLI failed (${oversized_result}): ${oversized_error}")
endif()
string(FIND "${oversized_output}" "[4 source byte(s) omitted]" omission_position)
if(omission_position EQUAL -1)
    message(FATAL_ERROR "oversized record omission was not surfaced")
endif()
