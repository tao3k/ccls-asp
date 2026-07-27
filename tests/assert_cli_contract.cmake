if(NOT DEFINED CCLS_ASP)
  message(FATAL_ERROR "CCLS_ASP is required")
endif()

function(assert_rejected NAME)
  execute_process(
    COMMAND "${CCLS_ASP}" ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if(result EQUAL 0)
    message(FATAL_ERROR "${NAME} unexpectedly succeeded: ${output}")
  endif()
  if(NOT output STREQUAL "")
    message(FATAL_ERROR "${NAME} wrote non-JSON error output to stdout: ${output}")
  endif()
endfunction()

assert_rejected(legacy-view --language cpp search ingest --view seeds)
assert_rejected(legacy-hook --language cpp query --from-hook direct-source-read)
assert_rejected(legacy-surface --language cpp query --surface hook)
assert_rejected(positional-selector --language cpp query widget.cpp:1:4)
assert_rejected(query-owner --language cpp query --selector widget.cpp:1:4 --owner widget.cpp)
assert_rejected(ingest-code --language cpp search ingest --code)

execute_process(
  COMMAND "${CCLS_ASP}" --language cpp query
          --selector widget.cpp:1:4
          --workspace "${CMAKE_CURRENT_LIST_DIR}/fixtures/cpp"
          --code --json
          --asp-provider-id ccls-asp
          --asp-parser-identity-digest blake3:test-parser
          --asp-query-pack-digest blake3:test-query-pack
          --source-snapshot-envelope "${CMAKE_CURRENT_LIST_FILE}"
  RESULT_VARIABLE internal_query_status
  OUTPUT_VARIABLE internal_query_output
  ERROR_VARIABLE internal_query_error
)
if(NOT internal_query_status EQUAL 0)
  message(FATAL_ERROR "ASP internal JSON query failed: ${internal_query_error}")
endif()
foreach(expected
        "\"schemaId\": \"agent.semantic-protocols.semantic-query-packet\""
        "\"outputMode\": \"source\""
        "\"source\":")
  string(FIND "${internal_query_output}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "ASP internal JSON query missing ${expected}: ${internal_query_output}")
  endif()
endforeach()
