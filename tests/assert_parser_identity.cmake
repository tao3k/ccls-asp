set(test_root "${CMAKE_CURRENT_BINARY_DIR}/ccls-asp-parser-identity")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}/base" "${test_root}/shifted")
set(fixture_source "int stable_item(int value) { return value + 1; }\n")
file(WRITE "${test_root}/empty-input" "")
file(WRITE "${test_root}/base/widget.cpp" "${fixture_source}")
file(WRITE "${test_root}/shifted/widget.cpp" "\n${fixture_source}")

function(run_ingest workspace extra_flag output_var)
  file(WRITE "${workspace}/compile_commands.json"
       "[\n"
       "  {\"directory\":\"${workspace}\","
       "\"command\":\"clang++ -std=c++17 -c widget.cpp\","
       "\"file\":\"${workspace}/widget.cpp\"}\n"
       "]\n")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "CCLS_ASP_EXTRA_CLANG_ARGS=${extra_flag}"
            "${CCLS_ASP}" --language cpp search ingest --workspace "${workspace}"
            --compilation-database "${workspace}"
    INPUT_FILE "${test_root}/empty-input"
    TIMEOUT 30
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "identity ingest failed: ${error}")
  endif()
  set("${output_var}" "${output}" PARENT_SCOPE)
endfunction()

run_ingest("${test_root}/base" "-DCONTEXT_ID=1" base_output)
run_ingest("${test_root}/shifted" "-DCONTEXT_ID=1" shifted_output)
run_ingest("${test_root}/base" "-DCONTEXT_ID=2" changed_context_output)

foreach(pair
        "base_output;base_selector;structuralSelector"
        "shifted_output;shifted_selector;structuralSelector"
        "base_output;base_generation;generationId"
        "changed_context_output;changed_generation;generationId")
  list(GET pair 0 packet_var)
  list(GET pair 1 result_var)
  list(GET pair 2 field)
  string(REGEX MATCH "\"${field}\": \"([^\"]+)\"" match "${${packet_var}}")
  if(NOT match)
    message(FATAL_ERROR "missing ${field} in ${packet_var}")
  endif()
  set("${result_var}" "${CMAKE_MATCH_1}")
endforeach()

if(NOT base_selector STREQUAL shifted_selector)
  message(FATAL_ERROR
          "canonical selector changed after a display-line shift: ${base_selector} != ${shifted_selector}")
endif()
if(base_generation STREQUAL changed_generation)
  message(FATAL_ERROR "generationId did not change with the effective compile context")
endif()
