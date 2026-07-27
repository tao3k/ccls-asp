#include "ccls_asp.h"

#include "asp_parser.hh"

#include <exception>
#include <new>
#include <optional>
#include <string>
#include <vector>

struct ccls_asp_result_v1 {
  ccls_asp::ParseResult value;
};

namespace {

const char *borrow(const std::string &value) { return value.c_str(); }

} // namespace

extern "C" {

uint32_t ccls_asp_abi_version(void) { return CCLS_ASP_ABI_VERSION_V1; }

ccls_asp_result_v1 *ccls_asp_parse_translation_unit_v1(const char *workspace, const char *translation_unit,
                                                       const char *language, const char *compilation_database) {
  auto *result = new (std::nothrow) ccls_asp_result_v1;
  if (result == nullptr) {
    return nullptr;
  }
  if (workspace == nullptr || translation_unit == nullptr || language == nullptr) {
    result->value.errors.emplace_back("workspace, translation_unit, and language are required");
    return result;
  }

  try {
    const std::optional<std::string> database =
        compilation_database == nullptr ? std::nullopt : std::optional<std::string>(compilation_database);
    result->value =
        ccls_asp::parse_translation_units(workspace, std::vector<std::string>{translation_unit}, language, database);
  } catch (const std::exception &error) {
    result->value.errors.emplace_back(error.what());
  } catch (...) {
    result->value.errors.emplace_back("unknown native parser failure");
  }
  return result;
}

ccls_asp_result_v1 *ccls_asp_parse_translation_unit_with_args_v1(const char *workspace, const char *translation_unit,
                                                                 const char *language,
                                                                 const char *const *normalized_compile_args,
                                                                 size_t normalized_compile_arg_count) {
  auto *result = new (std::nothrow) ccls_asp_result_v1;
  if (result == nullptr) {
    return nullptr;
  }
  if (workspace == nullptr || translation_unit == nullptr || language == nullptr ||
      (normalized_compile_arg_count != 0 && normalized_compile_args == nullptr)) {
    result->value.errors.emplace_back("workspace, translation_unit, language, and compiler args are "
                                      "required");
    return result;
  }

  try {
    std::vector<std::string> compiler_args;
    compiler_args.reserve(normalized_compile_arg_count);
    for (size_t index = 0; index < normalized_compile_arg_count; ++index) {
      if (normalized_compile_args[index] == nullptr) {
        result->value.errors.emplace_back("normalized compiler arguments must not contain null");
        return result;
      }
      compiler_args.emplace_back(normalized_compile_args[index]);
    }
    result->value = ccls_asp::parse_translation_units(workspace, std::vector<std::string>{translation_unit}, language,
                                                      std::nullopt, compiler_args);
  } catch (const std::exception &error) {
    result->value.errors.emplace_back(error.what());
  } catch (...) {
    result->value.errors.emplace_back("unknown native parser failure");
  }
  return result;
}

void ccls_asp_result_free_v1(ccls_asp_result_v1 *result) { delete result; }

size_t ccls_asp_result_fact_count_v1(const ccls_asp_result_v1 *result) {
  return result == nullptr ? 0 : result->value.facts.size();
}

uint8_t ccls_asp_result_fact_at_v1(const ccls_asp_result_v1 *result, size_t index, ccls_asp_fact_v1 *out) {
  if (result == nullptr || out == nullptr || index >= result->value.facts.size()) {
    return 0;
  }
  const auto &fact = result->value.facts[index];
  out->name = borrow(fact.name);
  out->qualified_name = borrow(fact.qualified_name);
  out->symbol_id = borrow(fact.symbol_id);
  out->semantic_variant_id = borrow(fact.semantic_variant_id);
  out->kind = borrow(fact.kind);
  out->role = borrow(fact.role);
  out->visibility = borrow(fact.visibility);
  out->type = borrow(fact.type);
  out->target = borrow(fact.target);
  out->target_symbol_id = borrow(fact.target_symbol_id);
  out->container_symbol_id = borrow(fact.container_symbol_id);
  out->translation_unit = borrow(fact.translation_unit);
  out->compile_context_digest = borrow(fact.compile_context_digest);
  out->location.path = borrow(fact.location.path);
  out->location.start_line = fact.location.start_line;
  out->location.end_line = fact.location.end_line;
  out->location.start_column = fact.location.start_column;
  out->location.end_column = fact.location.end_column;
  out->location.start_offset = fact.location.start_offset;
  out->location.end_offset = fact.location.end_offset;
  out->location.structural_selector = borrow(fact.location.structural_selector);
  return 1;
}

size_t ccls_asp_result_dependency_count_v1(const ccls_asp_result_v1 *result) {
  return result == nullptr ? 0 : result->value.dependency_usages.size();
}

uint8_t ccls_asp_result_dependency_at_v1(const ccls_asp_result_v1 *result, size_t index,
                                         ccls_asp_dependency_usage_v1 *out) {
  if (result == nullptr || out == nullptr || index >= result->value.dependency_usages.size()) {
    return 0;
  }
  const auto &dependency = result->value.dependency_usages[index];
  out->owner_path = borrow(dependency.owner_path);
  out->translation_unit = borrow(dependency.translation_unit);
  out->compile_context_digest = borrow(dependency.compile_context_digest);
  out->semantic_variant_id = borrow(dependency.semantic_variant_id);
  out->package_name = borrow(dependency.package_name);
  out->import_path = borrow(dependency.import_path);
  out->resolved_path = borrow(dependency.resolved_path);
  out->angled = dependency.angled ? 1 : 0;
  out->source_locator = borrow(dependency.source_locator);
  return 1;
}

size_t ccls_asp_result_dependency_query_key_count_v1(const ccls_asp_result_v1 *result, size_t dependency_index) {
  if (result == nullptr || dependency_index >= result->value.dependency_usages.size()) {
    return 0;
  }
  return result->value.dependency_usages[dependency_index].query_keys.size();
}

const char *ccls_asp_result_dependency_query_key_at_v1(const ccls_asp_result_v1 *result, size_t dependency_index,
                                                       size_t query_key_index) {
  if (result == nullptr || dependency_index >= result->value.dependency_usages.size()) {
    return nullptr;
  }
  const auto &keys = result->value.dependency_usages[dependency_index].query_keys;
  return query_key_index < keys.size() ? borrow(keys[query_key_index]) : nullptr;
}

size_t ccls_asp_result_compile_context_count_v1(const ccls_asp_result_v1 *result) {
  return result == nullptr ? 0 : result->value.compile_contexts.size();
}

uint8_t ccls_asp_result_compile_context_at_v1(const ccls_asp_result_v1 *result, size_t index,
                                              ccls_asp_compile_context_v1 *out) {
  if (result == nullptr || out == nullptr || index >= result->value.compile_contexts.size()) {
    return 0;
  }
  const auto &context = result->value.compile_contexts[index];
  out->translation_unit = borrow(context.translation_unit);
  out->digest = borrow(context.digest);
  return 1;
}

size_t ccls_asp_result_translation_unit_count_v1(const ccls_asp_result_v1 *result) {
  return result == nullptr ? 0 : result->value.translation_units.size();
}

const char *ccls_asp_result_translation_unit_at_v1(const ccls_asp_result_v1 *result, size_t index) {
  if (result == nullptr || index >= result->value.translation_units.size()) {
    return nullptr;
  }
  return borrow(result->value.translation_units[index]);
}

size_t ccls_asp_result_error_count_v1(const ccls_asp_result_v1 *result) {
  return result == nullptr ? 0 : result->value.errors.size();
}

const char *ccls_asp_result_error_at_v1(const ccls_asp_result_v1 *result, size_t index) {
  if (result == nullptr || index >= result->value.errors.size()) {
    return nullptr;
  }
  return borrow(result->value.errors[index]);
}

} // extern "C"
