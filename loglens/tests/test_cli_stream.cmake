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
