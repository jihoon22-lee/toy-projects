if(NOT DEFINED PYTHON_EXECUTABLE OR NOT DEFINED PYTHON_PACKAGE_ROOT)
    message(FATAL_ERROR "Python contract inputs are required")
endif()
if(NOT DEFINED COMPILER OR NOT DEFINED WORK OR NOT DEFINED CONSUMER)
    message(FATAL_ERROR "include-trace integration inputs are required")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY
    "${WORK}/build"
    "${WORK}/src"
    "${WORK}/include/first"
    "${WORK}/include/second"
)
file(WRITE "${WORK}/src/main.cpp"
    "#include <common.hpp>\n#if BUILDSCOPE_SELECTED_HEADER != 1\n#error wrong active header\n#endif\nint main() { return 0; }\n")
file(WRITE "${WORK}/include/first/common.hpp" "#define BUILDSCOPE_SELECTED_HEADER 1\n")
file(WRITE "${WORK}/include/second/common.hpp" "#define BUILDSCOPE_SELECTED_HEADER 2\n")

file(WRITE "${WORK}/build/compile_commands.json" "[{
  \"directory\": \"${WORK}/build\",
  \"file\": \"${WORK}/src/main.cpp\",
  \"arguments\": [
    \"${COMPILER}\", \"-std=c++17\",
    \"-I\", \"${WORK}/include/first\",
    \"-I\", \"${WORK}/include/second\",
    \"-c\", \"${WORK}/src/main.cpp\",
    \"-o\", \"main.o\"
  ]
}]\n")

set(_snapshot "${WORK}/snapshot-v3.json")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "PYTHONPATH=${PYTHON_PACKAGE_ROOT}"
        "${PYTHON_EXECUTABLE}" -m buildscope
        "${WORK}/build/compile_commands.json"
        --project-root "${WORK}"
        --include-analysis compiler
        --pretty
        --output "${_snapshot}"
    RESULT_VARIABLE _producer_result
    OUTPUT_VARIABLE _producer_stdout
    ERROR_VARIABLE _producer_stderr
)
if(NOT _producer_result EQUAL 0)
    message(FATAL_ERROR
        "Python include producer failed (${_producer_result}): ${_producer_stdout}${_producer_stderr}")
endif()

file(READ "${_snapshot}" _payload)
foreach(_required
        "\"schema_version\": \"buildscope.snapshot/v3\""
        "\"evidence\": \"compiler-measured\""
        "\"location_evidence\": \"source-scan\""
        "\"resolved\": \"include/first/common.hpp\""
        "\"include/second/common.hpp\"")
    string(FIND "${_payload}" "${_required}" _offset)
    if(_offset EQUAL -1)
        message(FATAL_ERROR "v3 snapshot is missing ${_required}")
    endif()
endforeach()

execute_process(
    COMMAND "${CONSUMER}" "${_snapshot}"
    RESULT_VARIABLE _consumer_result
    OUTPUT_VARIABLE _consumer_stdout
    ERROR_VARIABLE _consumer_stderr
)
if(NOT _consumer_result EQUAL 0)
    message(FATAL_ERROR
        "C++ v3 consumer failed (${_consumer_result}): ${_consumer_stdout}${_consumer_stderr}")
endif()
if(NOT _consumer_stdout MATCHES "Compilation entries: 1.+buildscope.snapshot/v3")
    message(FATAL_ERROR "unexpected v3 consumer output: ${_consumer_stdout}")
endif()
