if(NOT DEFINED PYTHON_EXECUTABLE OR NOT DEFINED PYTHON_PACKAGE_ROOT)
    message(FATAL_ERROR "Python contract inputs are required")
endif()
if(NOT DEFINED DATABASE OR NOT DEFINED OUTPUT OR NOT DEFINED CONSUMER)
    message(FATAL_ERROR "integration paths are required")
endif()

get_filename_component(_output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_directory}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "PYTHONPATH=${PYTHON_PACKAGE_ROOT}"
        "${PYTHON_EXECUTABLE}" -m buildscope "${DATABASE}" --output "${OUTPUT}"
    RESULT_VARIABLE _producer_result
    OUTPUT_VARIABLE _producer_stdout
    ERROR_VARIABLE _producer_stderr
)
if(NOT _producer_result EQUAL 0)
    message(FATAL_ERROR
        "Python producer failed (${_producer_result}): ${_producer_stdout}${_producer_stderr}")
endif()

execute_process(
    COMMAND "${CONSUMER}" "${OUTPUT}"
    RESULT_VARIABLE _consumer_result
    OUTPUT_VARIABLE _consumer_stdout
    ERROR_VARIABLE _consumer_stderr
)
if(NOT _consumer_result EQUAL 0)
    message(FATAL_ERROR
        "C++ consumer failed (${_consumer_result}): ${_consumer_stdout}${_consumer_stderr}")
endif()
if(NOT _consumer_stdout MATCHES "Compilation entries: 2.+buildscope.snapshot/v2")
    message(FATAL_ERROR "unexpected consumer output: ${_consumer_stdout}")
endif()
