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

add_library(ccls-asp-parser SHARED
  src/asp_parser.cc
  src/asp_c_api.cc
)
set_target_properties(
  ccls-asp-parser
  PROPERTIES
    OUTPUT_NAME ccls-asp-parser
    POSITION_INDEPENDENT_CODE ON
)

add_executable(ccls-asp
  src/main.cc
)

target_include_directories(ccls-asp-parser SYSTEM PRIVATE
  ${LLVM_INCLUDE_DIRS}
  ${CLANG_INCLUDE_DIRS}
)
target_include_directories(ccls-asp-parser PRIVATE
  src
)
target_include_directories(
  ccls-asp-parser
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(ccls-asp PRIVATE src)

target_compile_definitions(ccls-asp-parser PRIVATE ${LLVM_DEFINITIONS})
target_compile_definitions(
  ccls-asp-parser PRIVATE
  CCLS_ASP_CLANG_RESOURCE_DIR="${CCLS_ASP_CLANG_RESOURCE_DIR}"
)
string(REPLACE ";" "|" CCLS_ASP_CXX_IMPLICIT_INCLUDE_DIRS "${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES}")
target_compile_definitions(
  ccls-asp-parser
  PRIVATE CCLS_ASP_CXX_IMPLICIT_INCLUDE_DIRS="${CCLS_ASP_CXX_IMPLICIT_INCLUDE_DIRS}"
)

target_compile_options(ccls-asp-parser PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(ccls-asp PRIVATE -Wall -Wextra -Wpedantic)

if(TARGET clang-cpp)
  target_link_libraries(ccls-asp-parser PUBLIC clang-cpp LLVM)
else()
  target_link_libraries(ccls-asp-parser PUBLIC
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

target_link_libraries(ccls-asp PRIVATE ccls-asp-parser)

install(
  TARGETS ccls-asp ccls-asp-parser
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
install(FILES include/ccls_asp.h DESTINATION include)

enable_testing()
add_executable(ccls-asp-native-library-scenario
  tests/native_library_scenario.cc
)
target_include_directories(ccls-asp-native-library-scenario PRIVATE src)
target_compile_options(
  ccls-asp-native-library-scenario PRIVATE -Wall -Wextra -Wpedantic
)
target_link_libraries(
  ccls-asp-native-library-scenario PRIVATE ccls-asp-parser
)

add_executable(ccls-asp-native-c-abi-smoke
  tests/native_c_abi_smoke.c
)
set_target_properties(
  ccls-asp-native-c-abi-smoke PROPERTIES LINKER_LANGUAGE CXX
)
target_link_libraries(ccls-asp-native-c-abi-smoke PRIVATE ccls-asp-parser)

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
  NAME ccls-asp-context-variants
  COMMAND
    ${CMAKE_COMMAND} -DCCLS_ASP=$<TARGET_FILE:ccls-asp> -P
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_context_variants.cmake
)
add_test(
  NAME ccls-asp-cli-contract
  COMMAND
    ${CMAKE_COMMAND} -DCCLS_ASP=$<TARGET_FILE:ccls-asp> -P
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_cli_contract.cmake
)
add_test(
  NAME ccls-asp-native-library-cpp-snapshot
  COMMAND
    ${CMAKE_COMMAND}
    -DSCENARIO=$<TARGET_FILE:ccls-asp-native-library-scenario>
    -DWORKSPACE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cpp
    -DOWNER=widget.cpp
    -DLANGUAGE=cpp
    -DSNAPSHOT=${CMAKE_CURRENT_SOURCE_DIR}/tests/snapshots/cpp-native-library.v1.snapshot
    -P
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_native_library_snapshot.cmake
)
add_test(
  NAME ccls-asp-native-library-c-snapshot
  COMMAND
    ${CMAKE_COMMAND}
    -DSCENARIO=$<TARGET_FILE:ccls-asp-native-library-scenario>
    -DWORKSPACE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/c
    -DOWNER=widget.c
    -DLANGUAGE=c
    -DSNAPSHOT=${CMAKE_CURRENT_SOURCE_DIR}/tests/snapshots/c-native-library.v1.snapshot
    -P
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_native_library_snapshot.cmake
)
add_test(
  NAME ccls-asp-native-library-objective-c-snapshot
  COMMAND
    ${CMAKE_COMMAND}
    -DSCENARIO=$<TARGET_FILE:ccls-asp-native-library-scenario>
    -DWORKSPACE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/objective-c
    -DOWNER=Widget.m
    -DLANGUAGE=objective-c
    -DSNAPSHOT=${CMAKE_CURRENT_SOURCE_DIR}/tests/snapshots/objective-c-native-library.v1.snapshot
    -P
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_native_library_snapshot.cmake
)
add_test(
  NAME ccls-asp-native-c-abi
  COMMAND
    ccls-asp-native-c-abi-smoke
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cpp
)
add_test(
  NAME ccls-asp-native-library-performance
  COMMAND
    ccls-asp-native-library-scenario
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cpp
    widget.cpp
    cpp
    -
    5
)
set_tests_properties(
  ccls-asp-native-library-performance
  PROPERTIES
    LABELS "performance;snapshot"
    PASS_REGULAR_EXPRESSION "providerProcessLaunches=0"
)
