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
};

struct Fact {
  std::string name;
  std::string qualified_name;
  std::string kind;
  std::string role;
  std::string type;
  std::string target;
  SourceRange location;
};

struct DependencyUsage {
  std::string owner_path;
  std::string package_name;
  std::string import_path;
  std::string source_locator;
  std::vector<std::string> query_keys;
};

struct ParseResult {
  std::vector<Fact> facts;
  std::vector<DependencyUsage> dependency_usages;
  std::vector<std::string> translation_units;
  std::vector<std::string> errors;
};

ParseResult parse_translation_units(const std::string &workspace, const std::vector<std::string> &owners,
                                    const std::string &language,
                                    const std::optional<std::string> &compilation_database = std::nullopt);

bool supports_source_path(const std::string &path, const std::string &language);

} // namespace ccls_asp
