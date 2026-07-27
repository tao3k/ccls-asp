// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ccls_asp {

struct SourceRange {
  std::string path;
  std::uint32_t start_line = 1;
  std::uint32_t end_line = 1;
  std::uint32_t start_column = 1;
  std::uint32_t end_column = 1;
  std::uint64_t start_offset = 0;
  std::uint64_t end_offset = 0;
  std::string structural_selector;
};

struct Fact {
  std::string name;
  std::string qualified_name;
  std::string symbol_id;
  std::string semantic_variant_id;
  std::string kind;
  std::string role;
  std::string visibility;
  std::string type;
  std::string target;
  std::string target_symbol_id;
  std::string container_symbol_id;
  std::string translation_unit;
  std::string compile_context_digest;
  SourceRange location;
};

struct CompileContext {
  std::string translation_unit;
  std::string digest;
};

struct DependencyUsage {
  std::string owner_path;
  std::string translation_unit;
  std::string compile_context_digest;
  std::string semantic_variant_id;
  std::string package_name;
  std::string import_path;
  std::string resolved_path;
  bool angled = false;
  std::string source_locator;
  std::vector<std::string> query_keys;
};

struct ParseResult {
  std::vector<Fact> facts;
  std::vector<DependencyUsage> dependency_usages;
  std::vector<CompileContext> compile_contexts;
  std::vector<std::string> translation_units;
  std::vector<std::string> errors;
};

ParseResult
parse_translation_units(const std::string &workspace, const std::vector<std::string> &owners,
                        const std::string &language,
                        const std::optional<std::string> &compilation_database = std::nullopt,
                        const std::optional<std::vector<std::string>> &normalized_compile_args = std::nullopt);

bool supports_source_path(const std::string &path, const std::string &language);

} // namespace ccls_asp
