execute_process(
  COMMAND "${CCLS_ASP}" --language cpp search ingest
          --workspace "${CMAKE_CURRENT_LIST_DIR}/fixtures/cpp"
  RESULT_VARIABLE ingest_status
  OUTPUT_VARIABLE ingest_output
  ERROR_VARIABLE ingest_error
)
if(NOT ingest_status EQUAL 0)
  message(FATAL_ERROR "C++ ingest failed: ${ingest_error}")
endif()

string(REGEX MATCH "\"structuralSelector\": \"([^\"]+)\"" selector_match "${ingest_output}")
if(NOT selector_match)
  message(FATAL_ERROR "C++ feed did not expose a canonical structural selector")
endif()
set(selector "${CMAKE_MATCH_1}")

execute_process(
  COMMAND "${CCLS_ASP}" --language cpp query
          --selector "${selector}"
          --workspace "${CMAKE_CURRENT_LIST_DIR}/fixtures/cpp"
          --code
  RESULT_VARIABLE query_status
  OUTPUT_VARIABLE query_output
  ERROR_VARIABLE query_error
)
if(NOT query_status EQUAL 0)
  message(FATAL_ERROR "canonical selector query failed: ${query_error}")
endif()

foreach(expected
        "\"query\": \"${selector}\""
        "\"structuralSelector\": \"${selector}\""
        "\"matchCount\": 1"
        "\"source\":")
  string(FIND "${query_output}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "canonical selector query missing ${expected}: ${query_output}")
  endif()
endforeach()
