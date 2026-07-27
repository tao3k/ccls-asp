cmake_policy(SET CMP0054 NEW)

if(NOT DEFINED CCLS_ASP)
  message(FATAL_ERROR "CCLS_ASP is required")
endif()

set(FIXTURES "${CMAKE_CURRENT_LIST_DIR}/fixtures")

function(assert_json_packet NAME EXPECTED_METHOD EXPECTED_SCHEMA)
  execute_process(
    COMMAND "${CCLS_ASP}" ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${NAME} failed (${result}): ${error}")
  endif()

  if(EXPECTED_METHOD STREQUAL "index/structural")
    string(JSON method ERROR_VARIABLE json_error GET "${output}" exportMethod)
  else()
    string(JSON method ERROR_VARIABLE json_error GET "${output}" method)
  endif()
  if(json_error OR NOT method STREQUAL EXPECTED_METHOD)
    message(FATAL_ERROR "${NAME} did not emit the expected JSON method: ${output}")
  endif()

  if(NOT EXPECTED_SCHEMA STREQUAL "")
    string(JSON schema_id ERROR_VARIABLE schema_error GET "${output}" schemaId)
    if(schema_error OR NOT schema_id STREQUAL EXPECTED_SCHEMA)
      message(FATAL_ERROR "${NAME} did not emit schema ${EXPECTED_SCHEMA}: ${output}")
    endif()
  endif()
endfunction()

assert_json_packet(
  guide guide ""
  --language cpp guide
)
assert_json_packet(
  ingest index/structural agent.semantic-protocols.semantic-structural-index
  --language cpp search ingest
  --workspace "${FIXTURES}/cpp"
)
assert_json_packet(
  exact-source query/exact-selector agent.semantic-protocols.semantic-query-packet
  --language cpp query --selector widget.cpp:1:4
  --workspace "${FIXTURES}/cpp" --code
)

execute_process(
  COMMAND "${CCLS_ASP}" --language cpp query --selector widget.cpp:1:4
          --workspace "${FIXTURES}/cpp" --code
  RESULT_VARIABLE source_result
  OUTPUT_VARIABLE source_output
  ERROR_VARIABLE source_error
)
if(NOT source_result EQUAL 0)
  message(FATAL_ERROR "exact-source failed (${source_result}): ${source_error}")
endif()
string(JSON output_mode ERROR_VARIABLE mode_error GET "${source_output}" outputMode)
string(JSON source ERROR_VARIABLE source_json_error GET "${source_output}" source)
if(mode_error OR source_json_error OR NOT output_mode STREQUAL "source")
  message(FATAL_ERROR "exact-source must stay inside the JSON packet: ${source_output}")
endif()
