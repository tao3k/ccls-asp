execute_process(
  COMMAND "${CCLS_ASP}" --language c search ingest
          --workspace "${CMAKE_CURRENT_LIST_DIR}/fixtures/c"
  RESULT_VARIABLE c_status
  OUTPUT_VARIABLE c_output
  ERROR_VARIABLE c_error
)
if(NOT c_status EQUAL 0)
  message(FATAL_ERROR "C ingest failed: ${c_error}")
endif()
foreach(expected
        "\"kind\": \"declaration-reference\""
        "\"kind\": \"member-reference\""
        "\"kind\": \"parameter\""
        "\"kind\": \"macro-definition\""
        "\"kind\": \"macro-expansion\""
        "\"resolvedPath\": \"widget.h\""
        "\"includeKind\": \"quote\""
        "\"source\": \"native-parser\""
        "\"relations\":"
        "\"targetSymbolId\":")
  string(FIND "${c_output}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "C feed missing ${expected}: ${c_output}")
  endif()
endforeach()

execute_process(
  COMMAND "${CCLS_ASP}" --language cpp search ingest
          --workspace "${CMAKE_CURRENT_LIST_DIR}/fixtures/cpp"
  RESULT_VARIABLE cpp_status
  OUTPUT_VARIABLE cpp_output
  ERROR_VARIABLE cpp_error
)
if(NOT cpp_status EQUAL 0)
  message(FATAL_ERROR "C++ ingest failed: ${cpp_error}")
endif()
foreach(expected
        "\"kind\": \"inheritance\""
        "\"kind\": \"override\""
        "\"kind\": \"type-reference\""
        "\"kind\": \"member-reference\""
        "\"symbolId\":"
        "\"targetSymbolId\":")
  string(FIND "${cpp_output}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "C++ feed missing ${expected}: ${cpp_output}")
  endif()
endforeach()

execute_process(
  COMMAND "${CCLS_ASP}" --language objective-c search ingest
          --workspace "${CMAKE_CURRENT_LIST_DIR}/fixtures/objective-c"
  RESULT_VARIABLE objc_status
  OUTPUT_VARIABLE objc_output
  ERROR_VARIABLE objc_error
)
if(NOT objc_status EQUAL 0)
  message(FATAL_ERROR "Objective-C ingest failed: ${objc_error}")
endif()
foreach(expected
        "\"kind\": \"objc-inheritance\""
        "\"kind\": \"objc-protocol-conformance\""
        "\"kind\": \"objc-property-reference\""
        "\"targetSymbolId\":")
  string(FIND "${objc_output}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Objective-C feed missing ${expected}: ${objc_output}")
  endif()
endforeach()
