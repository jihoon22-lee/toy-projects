# `--version` must answer before any input is required: a shipped binary has to
# be able to state its identity without being handed a log to read. The exact
# number lives in main.cpp, so this test asserts the shape and the
# short-circuit, not the literal.
execute_process(
    COMMAND "${LOGLENS}" --version
    RESULT_VARIABLE version_result
    OUTPUT_VARIABLE version_output
    ERROR_VARIABLE version_error
)

if(NOT version_result EQUAL 0)
    message(FATAL_ERROR "loglens --version failed (${version_result}): ${version_error}")
endif()

if(NOT version_output MATCHES "^loglens [0-9]+\\.[0-9]+\\.[0-9]+\n$")
    message(FATAL_ERROR "loglens --version did not report an identity:\n${version_output}")
endif()

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
       "fatal: bad --filter at bytes [12,17): unexpected trailing input"
       invalid_filter_position)
if(invalid_filter_position EQUAL -1)
    message(FATAL_ERROR "missing CLI filter diagnostic range:\n${invalid_filter_error}")
endif()

execute_process(
    COMMAND "${LOGLENS}" "${INPUT}" --level ERROR --filter "level>=WARN extra"
    RESULT_VARIABLE composed_filter_result
    OUTPUT_QUIET
    ERROR_VARIABLE composed_filter_error
)
if(composed_filter_result EQUAL 0)
    message(FATAL_ERROR "invalid --filter combined with --level was accepted")
endif()
string(FIND "${composed_filter_error}"
       "fatal: bad --filter at bytes [12,17): unexpected trailing input"
       composed_filter_position)
if(composed_filter_position EQUAL -1)
    message(FATAL_ERROR "combined option diagnostic did not use --filter byte offsets:\n${composed_filter_error}")
endif()

# A saved session must round-trip: save the effective options, then reload
# them without explicit flags. An explicit flag overrides the session value.
set(session_file "${CMAKE_CURRENT_BINARY_DIR}/loglens-session.json")
execute_process(
    COMMAND "${LOGLENS}" "${INPUT}" --level ERROR --format auto
            --save-session "${session_file}"
    RESULT_VARIABLE save_result
    OUTPUT_QUIET
    ERROR_VARIABLE save_error
)
if(NOT save_result EQUAL 0)
    message(FATAL_ERROR "loglens --save-session failed (${save_result}): ${save_error}")
endif()

execute_process(
    COMMAND "${LOGLENS}" --session "${session_file}"
    RESULT_VARIABLE session_result
    OUTPUT_VARIABLE session_output
    ERROR_VARIABLE session_error
)
file(REMOVE "${session_file}")
if(NOT session_result EQUAL 0)
    message(FATAL_ERROR "loglens --session failed (${session_result}): ${session_error}")
endif()
foreach(expected IN ITEMS
        "3  ERROR  api  upstream timeout contacting billing"
        "8  ERROR  db  deadlock detected on tx 77")
    string(FIND "${session_output}" "${expected}" session_position)
    if(session_position EQUAL -1)
        message(FATAL_ERROR "missing session-filtered output '${expected}':\n${session_output}")
    endif()
endforeach()
string(FIND "${session_output}" "6  INFO  api" session_info_position)
if(NOT session_info_position EQUAL -1)
    message(FATAL_ERROR "session --level ERROR leaked an INFO record:\n${session_output}")
endif()

execute_process(
    COMMAND "${LOGLENS}" --session "${CMAKE_CURRENT_BINARY_DIR}/missing-session.json"
    RESULT_VARIABLE missing_session_result
    OUTPUT_QUIET
    ERROR_VARIABLE missing_session_error
)
if(missing_session_result EQUAL 0)
    message(FATAL_ERROR "a missing session file was accepted")
endif()

execute_process(
    COMMAND "${LOGLENS}" "${INPUT}" --level BOGUS
    RESULT_VARIABLE invalid_level_result
    OUTPUT_QUIET
    ERROR_VARIABLE invalid_level_error
)
if(invalid_level_result EQUAL 0)
    message(FATAL_ERROR "invalid --level was accepted")
endif()
string(FIND "${invalid_level_error}"
       "fatal: bad --level at bytes [0,5): unknown level 'BOGUS'"
       invalid_level_position)
if(invalid_level_position EQUAL -1)
    message(FATAL_ERROR "--level diagnostic did not use argument byte offsets:\n${invalid_level_error}")
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

# A format plugin is one parsing choice with --format: combining them is an
# error, and --save-session must carry the plugin so --session reparses the
# log the same way instead of silently falling back to the built-in format.
set(plugin_file "${CMAKE_CURRENT_BINARY_DIR}/loglens-cli.format.json")
file(WRITE "${plugin_file}" [=[{"kind":"loglens.format/v1","name":"bracket",
"pattern":"^(\\S+) (\\w+) +\\[(\\w+)\\] (.*)$",
"fields":{"timestamp":1,"level":2,"source":3,"message":4}}]=])
execute_process(
    COMMAND "${LOGLENS}" "${INPUT}" --format plain --format-plugin "${plugin_file}"
    RESULT_VARIABLE conflict_result
    OUTPUT_VARIABLE conflict_output
    ERROR_VARIABLE conflict_error
)
if(conflict_result EQUAL 0 OR NOT conflict_error MATCHES "cannot be combined")
    message(FATAL_ERROR "--format with --format-plugin was not rejected (${conflict_result}): ${conflict_error}")
endif()

set(plugin_session "${CMAKE_CURRENT_BINARY_DIR}/loglens-plugin-session.json")
execute_process(
    COMMAND "${LOGLENS}" "${INPUT}" --format-plugin "${plugin_file}"
            --save-session "${plugin_session}"
    RESULT_VARIABLE plugin_result
    OUTPUT_VARIABLE plugin_output
    ERROR_VARIABLE plugin_error
)
if(NOT plugin_result EQUAL 0)
    message(FATAL_ERROR "loglens --format-plugin failed (${plugin_result}): ${plugin_error}")
endif()
file(READ "${plugin_session}" plugin_session_contents)
if(NOT plugin_session_contents MATCHES "\"format_plugin\":")
    message(FATAL_ERROR "saved session lost the format plugin:\n${plugin_session_contents}")
endif()
execute_process(
    COMMAND "${LOGLENS}" --session "${plugin_session}"
    RESULT_VARIABLE replay_result
    OUTPUT_VARIABLE replay_output
    ERROR_VARIABLE replay_error
)
execute_process(
    COMMAND "${LOGLENS}" "${INPUT}" --format auto
    OUTPUT_VARIABLE builtin_output
)
file(REMOVE "${plugin_session}" "${plugin_file}")
if(NOT replay_result EQUAL 0)
    message(FATAL_ERROR "loglens --session with a plugin failed (${replay_result}): ${replay_error}")
endif()
if(NOT replay_output STREQUAL plugin_output)
    message(FATAL_ERROR "session replay did not reparse with the plugin:\n${replay_output}\n--- expected ---\n${plugin_output}")
endif()
if(plugin_output STREQUAL builtin_output)
    message(FATAL_ERROR "plugin fixture is indistinguishable from the built-in parser")
endif()
