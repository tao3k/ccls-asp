set(test_root "${CMAKE_CURRENT_BINARY_DIR}/ccls-asp-context-variants")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")
file(WRITE "${test_root}/empty-input" "")
file(WRITE "${test_root}/shared.h"
     "#pragma once\n"
     "#ifdef MODE_A\n"
     "using ContextType = int;\n"
     "#else\n"
     "using ContextType = float;\n"
     "#endif\n"
     "struct Shared { ContextType value; };\n")
file(WRITE "${test_root}/a.cpp" "#define MODE_A\n#include \"shared.h\"\nShared a;\n")
file(WRITE "${test_root}/b.cpp" "#include \"shared.h\"\nShared b;\n")
file(WRITE "${test_root}/compile_commands.json"
     "[\n"
     "  {\"directory\":\"${test_root}\","
     "\"command\":\"clang++ -std=c++17 -c a.cpp\","
     "\"file\":\"${test_root}/a.cpp\"},\n"
     "  {\"directory\":\"${test_root}\","
     "\"command\":\"clang++ -std=c++17 -c b.cpp\","
     "\"file\":\"${test_root}/b.cpp\"}\n"
     "]\n")

execute_process(
  COMMAND "${CCLS_ASP}" --language cpp search ingest
          --workspace "${test_root}" --compilation-database "${test_root}"
  INPUT_FILE "${test_root}/empty-input"
  TIMEOUT 30
  RESULT_VARIABLE status
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "context variant ingest failed: ${error}")
endif()

foreach(expected
        "\"schemaVersion\": \"1\""
        "\"translationUnit\": \"a.cpp\""
        "\"translationUnit\": \"b.cpp\""
        "\"compileContextDigest\":"
        "\"semanticVariantId\": \"sha256:")
  string(FIND "${output}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "context variant feed missing ${expected}: ${output}")
  endif()
endforeach()

string(REGEX MATCHALL "\"qualifiedName\": \"Shared\"" shared_matches "${output}")
list(LENGTH shared_matches shared_count)
if(shared_count LESS 2)
  message(FATAL_ERROR "shared header fact collapsed across translation units: count=${shared_count}")
endif()
