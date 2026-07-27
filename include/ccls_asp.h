#ifndef CCLS_ASP_H
#define CCLS_ASP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CCLS_ASP_ABI_VERSION_V1 UINT32_C(1)

typedef struct ccls_asp_result_v1 ccls_asp_result_v1;

typedef struct ccls_asp_source_range_v1 {
  const char *path;
  uint32_t start_line;
  uint32_t end_line;
  uint32_t start_column;
  uint32_t end_column;
  uint64_t start_offset;
  uint64_t end_offset;
  const char *structural_selector;
} ccls_asp_source_range_v1;

typedef struct ccls_asp_fact_v1 {
  const char *name;
  const char *qualified_name;
  const char *symbol_id;
  const char *semantic_variant_id;
  const char *kind;
  const char *role;
  const char *visibility;
  const char *type;
  const char *target;
  const char *target_symbol_id;
  const char *container_symbol_id;
  const char *translation_unit;
  const char *compile_context_digest;
  ccls_asp_source_range_v1 location;
} ccls_asp_fact_v1;

typedef struct ccls_asp_dependency_usage_v1 {
  const char *owner_path;
  const char *translation_unit;
  const char *compile_context_digest;
  const char *semantic_variant_id;
  const char *package_name;
  const char *import_path;
  const char *resolved_path;
  uint8_t angled;
  const char *source_locator;
} ccls_asp_dependency_usage_v1;

typedef struct ccls_asp_compile_context_v1 {
  const char *translation_unit;
  const char *digest;
} ccls_asp_compile_context_v1;

uint32_t ccls_asp_abi_version(void);

/*
 * Parses exactly one translation unit. String pointers are borrowed only for
 * the duration of this call. The returned result owns all output strings and
 * remains valid until ccls_asp_result_free_v1 is called.
 *
 * compilation_database may be NULL while the Rust-side compile-context
 * request is being migrated to explicit normalized compiler arguments.
 */
ccls_asp_result_v1 *ccls_asp_parse_translation_unit_v1(const char *workspace, const char *translation_unit,
                                                       const char *language, const char *compilation_database);

/*
 * Production entrypoint. normalized_compile_args are supplied by ASP Rust;
 * this path performs no compilation-database discovery and reads no compiler
 * argument environment variables.
 */
ccls_asp_result_v1 *ccls_asp_parse_translation_unit_with_args_v1(const char *workspace, const char *translation_unit,
                                                                 const char *language,
                                                                 const char *const *normalized_compile_args,
                                                                 size_t normalized_compile_arg_count);

void ccls_asp_result_free_v1(ccls_asp_result_v1 *result);

size_t ccls_asp_result_fact_count_v1(const ccls_asp_result_v1 *result);
uint8_t ccls_asp_result_fact_at_v1(const ccls_asp_result_v1 *result, size_t index, ccls_asp_fact_v1 *out);

size_t ccls_asp_result_dependency_count_v1(const ccls_asp_result_v1 *result);
uint8_t ccls_asp_result_dependency_at_v1(const ccls_asp_result_v1 *result, size_t index,
                                         ccls_asp_dependency_usage_v1 *out);
size_t ccls_asp_result_dependency_query_key_count_v1(const ccls_asp_result_v1 *result, size_t dependency_index);
const char *ccls_asp_result_dependency_query_key_at_v1(const ccls_asp_result_v1 *result, size_t dependency_index,
                                                       size_t query_key_index);

size_t ccls_asp_result_compile_context_count_v1(const ccls_asp_result_v1 *result);
uint8_t ccls_asp_result_compile_context_at_v1(const ccls_asp_result_v1 *result, size_t index,
                                              ccls_asp_compile_context_v1 *out);

size_t ccls_asp_result_translation_unit_count_v1(const ccls_asp_result_v1 *result);
const char *ccls_asp_result_translation_unit_at_v1(const ccls_asp_result_v1 *result, size_t index);

size_t ccls_asp_result_error_count_v1(const ccls_asp_result_v1 *result);
const char *ccls_asp_result_error_at_v1(const ccls_asp_result_v1 *result, size_t index);

#ifdef __cplusplus
}
#endif

#endif
