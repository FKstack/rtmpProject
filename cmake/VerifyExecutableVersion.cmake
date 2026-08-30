if(NOT DEFINED EXECUTABLE_PATH OR EXECUTABLE_PATH STREQUAL "")
    message(FATAL_ERROR "EXECUTABLE_PATH is required")
endif()
if(NOT DEFINED EXPECTED_VERSION OR EXPECTED_VERSION STREQUAL "")
    message(FATAL_ERROR "EXPECTED_VERSION is required")
endif()
if(NOT EXISTS "${EXECUTABLE_PATH}")
    message(FATAL_ERROR "Executable does not exist")
endif()

if(NOT DEFINED WORKING_DIRECTORY OR WORKING_DIRECTORY STREQUAL "")
    get_filename_component(WORKING_DIRECTORY "${EXECUTABLE_PATH}" DIRECTORY)
endif()

# PowerShell does not reliably wait for Windows GUI-subsystem executables when
# they are invoked directly.  CMake's execute_process supplies redirected
# handles and waits, which keeps the package/version probe deterministic.
execute_process(
    COMMAND "${EXECUTABLE_PATH}" --version
    WORKING_DIRECTORY "${WORKING_DIRECTORY}"
    RESULT_VARIABLE version_result
    OUTPUT_VARIABLE version_stdout
    ERROR_VARIABLE version_stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
    TIMEOUT 5
)
set(version_output "${version_stdout}${version_stderr}")
string(FIND "${version_output}" "${EXPECTED_VERSION}" version_position)
if(NOT version_result EQUAL 0 OR version_position EQUAL -1)
    message(FATAL_ERROR
        "Executable version verification failed "
        "(result=${version_result})"
    )
endif()

message(STATUS "Executable version verified: ${EXPECTED_VERSION}")
