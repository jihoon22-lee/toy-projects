if(NOT DEFINED PYTHON_EXECUTABLE OR NOT DEFINED PYTHON_PACKAGE_ROOT OR
   NOT DEFINED BEFORE_DATABASE OR NOT DEFINED AFTER_DATABASE OR
   NOT DEFINED EXPECTED OR NOT DEFINED OUTPUT OR NOT DEFINED CONSUMER)
    message(FATAL_ERROR "hybrid diff test is missing a required argument")
endif()

get_filename_component(output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "PYTHONPATH=${PYTHON_PACKAGE_ROOT}"
            "${PYTHON_EXECUTABLE}" -m buildscope diff
            "${BEFORE_DATABASE}" "${AFTER_DATABASE}"
            --before-project-root /project --after-project-root /project
            --pretty --output "${OUTPUT}"
    RESULT_VARIABLE producer_result
    OUTPUT_VARIABLE producer_output
    ERROR_VARIABLE producer_error
)
if(NOT producer_result EQUAL 1)
    message(FATAL_ERROR
        "diff producer should report visible drift (1), got ${producer_result}\n"
        "stdout: ${producer_output}\nstderr: ${producer_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUTPUT}" "${EXPECTED}"
    RESULT_VARIABLE comparison_result
)
if(NOT comparison_result EQUAL 0)
    message(FATAL_ERROR "generated diff is not byte-identical to the contract fixture")
endif()

execute_process(
    COMMAND "${CONSUMER}" --diff "${OUTPUT}"
    RESULT_VARIABLE consumer_result
    OUTPUT_VARIABLE consumer_output
    ERROR_VARIABLE consumer_error
)
if(NOT consumer_result EQUAL 0)
    message(FATAL_ERROR
        "native diff consumer failed (${consumer_result})\n"
        "stdout: ${consumer_output}\nstderr: ${consumer_error}")
endif()
if(NOT consumer_output MATCHES "Configuration changes: 4 visible")
    message(FATAL_ERROR "native diff summary is unexpected: ${consumer_output}")
endif()

set(suppressed_output "${output_directory}/suppressed-diff.json")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "PYTHONPATH=${PYTHON_PACKAGE_ROOT}"
            "${PYTHON_EXECUTABLE}" -m buildscope diff
            "${BEFORE_DATABASE}" "${AFTER_DATABASE}"
            --before-project-root /project --after-project-root /project
            --suppress "standard:*.cpp" --output "${suppressed_output}"
    RESULT_VARIABLE suppressed_producer_result
    ERROR_VARIABLE suppressed_producer_error
)
if(NOT suppressed_producer_result EQUAL 1)
    message(FATAL_ERROR
        "suppressed producer should retain visible drift (1), got ${suppressed_producer_result}\n"
        "stderr: ${suppressed_producer_error}")
endif()
execute_process(
    COMMAND "${CONSUMER}" --diff "${suppressed_output}"
    RESULT_VARIABLE suppressed_consumer_result
    ERROR_VARIABLE suppressed_consumer_error
)
if(NOT suppressed_consumer_result EQUAL 0)
    message(FATAL_ERROR
        "native consumer rejected canonical basename suppression (${suppressed_consumer_result})\n"
        "stderr: ${suppressed_consumer_error}")
endif()
