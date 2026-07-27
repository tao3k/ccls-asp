if(NOT DEFINED SCENARIO)
  message(FATAL_ERROR "SCENARIO is required")
endif()
if(NOT DEFINED WORKSPACE)
  message(FATAL_ERROR "WORKSPACE is required")
endif()
if(NOT DEFINED SNAPSHOT)
  message(FATAL_ERROR "SNAPSHOT is required")
endif()
if(NOT DEFINED OWNER)
  message(FATAL_ERROR "OWNER is required")
endif()
if(NOT DEFINED LANGUAGE)
  message(FATAL_ERROR "LANGUAGE is required")
endif()

execute_process(
  COMMAND
    "${SCENARIO}"
    "${WORKSPACE}"
    "${OWNER}"
    "${LANGUAGE}"
    -
  RESULT_VARIABLE scenario_status
  OUTPUT_VARIABLE scenario_output
  ERROR_VARIABLE performance_receipt
)
if(NOT scenario_status EQUAL 0)
  message(
    FATAL_ERROR
    "native library scenario failed (${scenario_status}): ${performance_receipt}"
  )
endif()

string(
  REGEX MATCH
  "SNAPSHOT[^\n]*\nSCENARIO[^\n]*\nCOUNTS[^\n]*\nDIGEST[^\n]*"
  actual_snapshot
  "${scenario_output}"
)
file(READ "${SNAPSHOT}" expected_snapshot)
string(STRIP "${actual_snapshot}" actual_snapshot)
string(STRIP "${expected_snapshot}" expected_snapshot)

if(NOT actual_snapshot STREQUAL expected_snapshot)
  message(
    FATAL_ERROR
    "native library snapshot mismatch\n"
    "expected:\n${expected_snapshot}\n"
    "actual:\n${actual_snapshot}\n"
    "update only after reviewing normalized fact changes"
  )
endif()

if(NOT performance_receipt MATCHES "providerProcessLaunches=0")
  message(
    FATAL_ERROR
    "linked scenario launched a provider process: ${performance_receipt}"
  )
endif()

message(STATUS "${performance_receipt}")
