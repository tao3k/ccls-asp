#include "asp_parser.hh"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

std::string clean(std::string value, const std::string &workspace) {
  if (value.starts_with(workspace)) {
    value.erase(0, workspace.size());
    if (value.starts_with('/')) {
      value.erase(0, 1);
    }
  }
  for (char &character : value) {
    if (character == '\t' || character == '\n' || character == '\r') {
      character = ' ';
    }
  }
  return value;
}

std::string context_label(const std::string &digest, const std::vector<ccls_asp::CompileContext> &compile_contexts) {
  std::vector<std::string> digests;
  digests.reserve(compile_contexts.size());
  for (const auto &context : compile_contexts) {
    digests.push_back(context.digest);
  }
  std::ranges::sort(digests);
  const auto unique_end = std::ranges::unique(digests).begin();
  digests.erase(unique_end, digests.end());
  const auto position = std::ranges::find(digests, digest);
  if (position == digests.end()) {
    return "context-unknown";
  }
  return "context-" + std::to_string(std::distance(digests.begin(), position));
}

std::string snapshot_digest(const std::vector<std::string> &rows) {
  std::uint64_t digest = UINT64_C(14695981039346656037);
  for (const auto &row : rows) {
    for (const unsigned char byte : row) {
      digest ^= byte;
      digest *= UINT64_C(1099511628211);
    }
    digest ^= static_cast<unsigned char>('\n');
    digest *= UINT64_C(1099511628211);
  }
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << digest;
  return output.str();
}

void print_snapshot(const ccls_asp::ParseResult &result, const std::string &workspace, const std::string &language,
                    const std::string &owner) {
  std::vector<std::string> rows;
  rows.reserve(result.facts.size() + result.dependency_usages.size() + result.compile_contexts.size() +
               result.errors.size());

  for (const auto &fact : result.facts) {
    rows.push_back("FACT\t" + clean(fact.kind, workspace) + "\t" + clean(fact.role, workspace) + "\t" +
                   clean(fact.qualified_name, workspace) + "\t" + clean(fact.location.path, workspace) + "\t" +
                   std::to_string(fact.location.start_line) + "\t" + std::to_string(fact.location.start_column) + "\t" +
                   clean(fact.translation_unit, workspace) + "\t" +
                   context_label(fact.compile_context_digest, result.compile_contexts));
  }
  for (const auto &dependency : result.dependency_usages) {
    rows.push_back("DEPENDENCY\t" + clean(dependency.owner_path, workspace) + "\t" +
                   clean(dependency.translation_unit, workspace) + "\t" + clean(dependency.import_path, workspace) +
                   "\t" + clean(dependency.resolved_path, workspace) + "\t" +
                   context_label(dependency.compile_context_digest, result.compile_contexts));
  }
  for (const auto &context : result.compile_contexts) {
    rows.push_back("CONTEXT\t" + clean(context.translation_unit, workspace) + "\t" +
                   context_label(context.digest, result.compile_contexts));
  }
  for (const auto &error : result.errors) {
    rows.push_back("ERROR\t" + clean(error, workspace));
  }

  std::ranges::sort(rows);
  std::cout << "SNAPSHOT\tccls-asp-native-library\t1\n";
  std::cout << "SCENARIO\t" << clean(language, workspace) << "\t" << clean(owner, workspace) << "\n";
  std::cout << "COUNTS\tfacts=" << result.facts.size() << "\tdependencies=" << result.dependency_usages.size()
            << "\tcontexts=" << result.compile_contexts.size() << "\terrors=" << result.errors.size() << "\n";
  std::cout << "DIGEST\t" << snapshot_digest(rows) << "\n";
  for (const auto &row : rows) {
    std::cout << row << "\n";
  }
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 5 || argc > 6) {
    std::cerr << "usage: native-library-scenario WORKSPACE OWNER LANGUAGE "
                 "COMPILATION_DATABASE [ITERATIONS]\n";
    return 2;
  }

  const std::string workspace = argv[1];
  const std::string owner = argv[2];
  const std::string language = argv[3];
  const std::optional<std::string> compilation_database =
      std::string_view(argv[4]) == "-" ? std::nullopt : std::optional<std::string>(argv[4]);
  const std::size_t iterations = argc == 6 ? static_cast<std::size_t>(std::strtoull(argv[5], nullptr, 10)) : 1;
  if (iterations == 0) {
    std::cerr << "iterations must be greater than zero\n";
    return 2;
  }

  ccls_asp::ParseResult result;
  std::vector<std::int64_t> iteration_micros;
  iteration_micros.reserve(iterations);
  const auto started = std::chrono::steady_clock::now();
  for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
    const auto iteration_started = std::chrono::steady_clock::now();
    if (compilation_database) {
      result =
          ccls_asp::parse_translation_units(workspace, std::vector<std::string>{owner}, language, compilation_database);
    } else {
      result = ccls_asp::parse_translation_units(workspace, std::vector<std::string>{owner}, language, std::nullopt,
                                                 std::vector<std::string>{});
    }
    iteration_micros.push_back(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - iteration_started)
            .count());
  }
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started);
  auto sorted_micros = iteration_micros;
  std::ranges::sort(sorted_micros);
  const auto median_micros = sorted_micros[sorted_micros.size() / 2];

  std::vector<std::string> objective_c_property_names;
  for (const auto &fact : result.facts) {
    if (fact.kind == "objc-property") {
      objective_c_property_names.push_back(fact.name);
    }
  }
  std::ranges::sort(objective_c_property_names);
  objective_c_property_names.erase(std::ranges::unique(objective_c_property_names).begin(),
                                   objective_c_property_names.end());
  std::erase_if(result.facts, [&](const ccls_asp::Fact &fact) {
    return fact.kind == "objc-message" && std::ranges::binary_search(objective_c_property_names, fact.name);
  });

  std::erase_if(result.facts, [](const ccls_asp::Fact &fact) { return fact.role == "reference"; });

  print_snapshot(result, workspace, language, owner);
  std::cerr << "PERF\tccls-asp-native-library\t1"
            << "\titerations=" << iterations << "\tproviderProcessLaunches=0"
            << "\ttranslationUnits=" << result.translation_units.size() << "\tcoldMicros=" << iteration_micros.front()
            << "\tmedianMicros=" << median_micros << "\ttotalMicros=" << elapsed.count() << "\n";
  return result.errors.empty() ? 0 : 1;
}
