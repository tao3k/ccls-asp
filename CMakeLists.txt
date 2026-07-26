cmake_minimum_required(VERSION 3.20)
project(ccls_asp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Clang REQUIRED CONFIG)

execute_process(
  COMMAND ${CMAKE_CXX_COMPILER} --print-resource-dir
  OUTPUT_VARIABLE CCLS_ASP_CLANG_RESOURCE_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY
)

add_executable(ccls-asp
  src/asp_parser.cc
  src/main.cc
)

target_include_directories(ccls-asp SYSTEM PRIVATE
  ${LLVM_INCLUDE_DIRS}
  ${CLANG_INCLUDE_DIRS}
)
target_include_directories(ccls-asp PRIVATE
  src
)
target_compile_definitions(ccls-asp PRIVATE ${LLVM_DEFINITIONS})
target_compile_definitions(
  ccls-asp PRIVATE
  CCLS_ASP_CLANG_RESOURCE_DIR="${CCLS_ASP_CLANG_RESOURCE_DIR}"
)
string(REPLACE ";" "|" CCLS_ASP_CXX_IMPLICIT_INCLUDE_DIRS "${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES}")
target_compile_definitions(
  ccls-asp
  PRIVATE CCLS_ASP_CXX_IMPLICIT_INCLUDE_DIRS="${CCLS_ASP_CXX_IMPLICIT_INCLUDE_DIRS}"
)

target_compile_options(ccls-asp PRIVATE -Wall -Wextra -Wpedantic)

if(TARGET clang-cpp)
  target_link_libraries(ccls-asp PRIVATE clang-cpp LLVM)
else()
  target_link_libraries(ccls-asp PRIVATE
    clangAST
    clangBasic
    clangFrontend
    clangIndex
    clangSerialization
    clangTooling
    LLVMOption
    LLVMSupport
  )
endif()

install(TARGETS ccls-asp RUNTIME DESTINATION bin)

enable_testing()
add_test(NAME ccls-asp-guide COMMAND ccls-asp guide --language cpp)
add_test(
  NAME ccls-asp-c-ingest
  COMMAND ccls-asp --language c search ingest
          --workspace ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/c
)
add_test(
  NAME ccls-asp-cpp-ingest
  COMMAND ccls-asp --language cpp search ingest
          --workspace ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cpp
)
add_test(
  NAME ccls-asp-objective-c-ingest
  COMMAND ccls-asp --language objective-c search ingest
          --workspace ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/objective-c
)
add_test(
  NAME ccls-asp-exact-code
  COMMAND ccls-asp --language cpp query
          --selector widget.cpp:1:4
          --workspace ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cpp --code
)
add_test(
  NAME ccls-asp-json-only-contract
  COMMAND
    ${CMAKE_COMMAND} -DCCLS_ASP=$<TARGET_FILE:ccls-asp> -P
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_json_only.cmake
)
add_test(
  NAME ccls-asp-semantic-relations
  COMMAND
    ${CMAKE_COMMAND} -DCCLS_ASP=$<TARGET_FILE:ccls-asp> -P
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_semantic_relations.cmake
)
add_test(
  NAME ccls-asp-canonical-selector
  COMMAND
    ${CMAKE_COMMAND} -DCCLS_ASP=$<TARGET_FILE:ccls-asp> -P
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_canonical_selector.cmake
)
add_test(
  NAME ccls-asp-parser-identity
  COMMAND
    ${CMAKE_COMMAND} -DCCLS_ASP=$<TARGET_FILE:ccls-asp> -P
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_parser_identity.cmake
)
add_test(
  NAME ccls-asp-cli-contract
  COMMAND
    ${CMAKE_COMMAND} -DCCLS_ASP=$<TARGET_FILE:ccls-asp> -P
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_cli_contract.cmake
)
add_test(
  NAME ccls-asp-self-ingest
  COMMAND
    ccls-asp --language cpp search ingest --owner src/asp_parser.cc
    --workspace ${CMAKE_CURRENT_SOURCE_DIR}
    --compilation-database ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(
  ccls-asp-self-ingest
  PROPERTIES
    PASS_REGULAR_EXPRESSION "\"qualifiedName\": \"ccls_asp::parse_translation_units\""
    FAIL_REGULAR_EXPRESSION "parse-error|ClangTool failed|fatal error"
)
