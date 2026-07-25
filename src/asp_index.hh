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

struct IndexResult {
  std::vector<Fact> facts;
  std::vector<std::string> compilation_units;
  std::vector<std::string> errors;
};

IndexResult build_index(const std::string &workspace, const std::optional<std::string> &owner,
                        const std::string &language,
                        const std::optional<std::string> &compilation_database = std::nullopt);

bool supports_source_path(const std::string &path, const std::string &language);

} // namespace ccls_asp
