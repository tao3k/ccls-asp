// SPDX-License-Identifier: Apache-2.0
#include "asp_index.hh"

#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using ccls_asp::Fact;
using ccls_asp::IndexResult;

namespace {

struct Options {
  std::string language = "cpp";
  std::string workspace = ".";
  std::string command;
  std::string search_view;
  std::string owner;
  std::string query;
  std::string selector;
  std::string compilation_database;
  bool code = false;
};

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
  return value;
}

bool contains_case_insensitive(const std::string &value, const std::string &term) {
  return lower(value).find(lower(term)) != std::string::npos;
}

bool valid_language(const std::string &language) {
  return language == "c" || language == "cpp" || language == "objective-c";
}

Options parse_options(int argc, char **argv) {
  Options options;
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto take = [&](std::string &target) {
      if (i + 1 >= argc)
        throw std::runtime_error("missing value after " + arg);
      target = argv[++i];
    };
    if (arg == "--language")
      take(options.language);
    else if (arg == "--workspace")
      take(options.workspace);
    else if (arg == "--selector")
      take(options.selector);
    else if (arg == "--compilation-database")
      take(options.compilation_database);
    else if (arg == "--query")
      take(options.query);
    else if (arg == "--json") {
      // Compatibility flag. Provider stdout is always a JSON packet; ASP owns rendering.
    } else if (arg == "--code")
      options.code = true;
    else if (arg == "--view") {
      std::string ignored;
      take(ignored);
    } else if (arg == "--from-hook" || arg == "--surface") {
      std::string ignored;
      take(ignored);
    } else if (!arg.starts_with("--"))
      positional.push_back(std::move(arg));
  }

  if (positional.empty())
    options.command = "guide";
  else {
    options.command = positional[0];
    if (options.command == "search") {
      options.search_view = positional.size() > 1 ? positional[1] : "prime";
      if (options.search_view == "owner" && positional.size() > 2)
        options.owner = positional[2];
      if (options.search_view == "lexical" && positional.size() > 2)
        options.query = positional[2];
    } else if (options.command == "query" && options.selector.empty() && positional.size() > 1) {
      options.selector = positional[1];
    }
  }
  return options;
}

llvm::json::Object fields_for(const Fact &fact, const std::string &language) {
  llvm::json::Object fields;
  fields["languageId"] = language;
  fields["providerId"] = "ccls-asp";
  fields["semanticFactKind"] = fact.kind;
  fields["role"] = fact.role;
  fields["qualifiedName"] = fact.qualified_name;
  fields["sourceAuthority"] = "clang-ast";
  if (!fact.type.empty())
    fields["type"] = fact.type;
  if (!fact.target.empty())
    fields["target"] = fact.target;
  return fields;
}

llvm::json::Object location_for(const Fact &fact) {
  llvm::json::Object location;
  location["path"] = fact.location.path;
  location["lineRange"] = std::to_string(fact.location.start_line) + ":" + std::to_string(fact.location.end_line);
  return location;
}

std::string namespace_for(const std::string &language) {
  return "agent.semantic-protocols.languages." + language + ".ccls-asp";
}

llvm::json::Object packet_base(const Options &options, const std::string &method) {
  llvm::json::Object packet;
  packet["schemaVersion"] = "1";
  packet["protocolId"] = "agent.semantic-protocols.semantic-language";
  packet["protocolVersion"] = "1";
  packet["languageId"] = options.language;
  packet["providerId"] = "ccls-asp";
  packet["binary"] = "ccls-asp";
  packet["namespace"] = namespace_for(options.language);
  packet["method"] = method;
  packet["projectRoot"] = options.workspace;
  return packet;
}

void print_json(llvm::json::Object packet) {
  llvm::outs() << llvm::formatv("{0:2}", llvm::json::Value(std::move(packet))) << "\n";
}

std::vector<const Fact *> selected_facts(const IndexResult &index, const Options &options) {
  std::vector<const Fact *> selected;
  for (const auto &fact : index.facts) {
    if (!options.query.empty() && !contains_case_insensitive(fact.name, options.query) &&
        !contains_case_insensitive(fact.qualified_name, options.query) &&
        !contains_case_insensitive(fact.kind, options.query))
      continue;
    selected.push_back(&fact);
    if (selected.size() == 200)
      break;
  }
  return selected;
}

void emit_search_packet(const IndexResult &index, const Options &options) {
  const auto selected = selected_facts(index, options);
  auto packet = packet_base(options, "search/" + options.search_view);
  packet["schemaId"] = "agent.semantic-protocols.semantic-search-packet";
  packet["view"] = options.search_view;
  packet["renderMode"] = "seeds";
  llvm::json::Object header_fields;
  header_fields["languageId"] = options.language;
  header_fields["sourceAuthority"] = "clang-ast";
  header_fields["compilationUnitCount"] = static_cast<std::int64_t>(index.compilation_units.size());
  header_fields["factCount"] = static_cast<std::int64_t>(selected.size());
  llvm::json::Object header;
  header["kind"] = "search-" + options.language;
  header["fields"] = std::move(header_fields);
  packet["header"] = std::move(header);
  packet["nodes"] = llvm::json::Array();
  packet["edges"] = llvm::json::Array();
  packet["findings"] = llvm::json::Array();
  packet["nextActions"] = llvm::json::Array();

  llvm::json::Array owners;
  llvm::json::Array hits;
  llvm::json::Array items;
  std::vector<std::string> seen_owners;
  for (const Fact *fact : selected) {
    if (std::find(seen_owners.begin(), seen_owners.end(), fact->location.path) == seen_owners.end()) {
      seen_owners.push_back(fact->location.path);
      llvm::json::Object owner;
      owner["path"] = fact->location.path;
      owner["role"] = "source";
      owner["public"] = true;
      owner["fields"] = llvm::json::Object{{"languageId", options.language}, {"sourceAuthority", "clang-ast"}};
      owners.push_back(std::move(owner));
    }
    llvm::json::Object hit;
    hit["kind"] = fact->kind;
    hit["ownerPath"] = fact->location.path;
    hit["symbol"] = fact->qualified_name;
    hit["location"] = location_for(*fact);
    hit["score"] = 1.0;
    hit["reason"] = "clang-ast";
    hit["fields"] = fields_for(*fact, options.language);
    hits.push_back(std::move(hit));

    llvm::json::Object item;
    item["name"] = fact->name;
    item["kind"] = fact->kind;
    item["ownerPath"] = fact->location.path;
    item["location"] = location_for(*fact);
    item["fields"] = fields_for(*fact, options.language);
    items.push_back(std::move(item));
  }
  packet["owners"] = std::move(owners);
  packet["hits"] = std::move(hits);
  packet["items"] = std::move(items);

  llvm::json::Array notes;
  for (const auto &error : index.errors) {
    llvm::json::Object note;
    note["kind"] = "parse-error";
    note["message"] = error;
    notes.push_back(std::move(note));
  }
  packet["notes"] = std::move(notes);
  if (!options.query.empty())
    packet["query"] = options.query;
  print_json(std::move(packet));
}

struct Selector {
  std::string path;
  std::uint32_t start = 1;
  std::uint32_t end = 0;
};

Selector parse_selector(const std::string &selector) {
  Selector parsed{selector, 1, 0};
  const auto last = selector.rfind(':');
  if (last == std::string::npos)
    return parsed;
  const auto previous = selector.rfind(':', last - 1);
  if (previous == std::string::npos)
    return parsed;
  try {
    parsed.start = std::stoul(selector.substr(previous + 1, last - previous - 1));
    parsed.end = std::stoul(selector.substr(last + 1));
    parsed.path = selector.substr(0, previous);
  } catch (const std::exception &) {
    parsed = {selector, 1, 0};
  }
  return parsed;
}

std::string exact_source(const Options &options, const Selector &selector) {
  std::error_code ec;
  const fs::path root = fs::weakly_canonical(options.workspace, ec);
  fs::path target = selector.path;
  if (target.is_relative())
    target = root / target;
  target = fs::weakly_canonical(target, ec);
  const auto relative = fs::relative(target, root, ec);
  if (ec || relative.empty() || relative.native().starts_with(".."))
    throw std::runtime_error("selector escapes workspace");

  std::ifstream input(target);
  if (!input)
    throw std::runtime_error("cannot read selector " + selector.path);
  std::ostringstream output;
  std::string line;
  std::uint32_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line_number < selector.start)
      continue;
    if (selector.end && line_number > selector.end)
      break;
    output << line << "\n";
  }
  return output.str();
}

void emit_query_packet(const IndexResult &index, const Options &options, const Selector &selector) {
  auto packet = packet_base(options, "query/exact-selector");
  packet["schemaId"] = "agent.semantic-protocols.semantic-query-packet";
  packet["query"] = selector.path;
  packet["queryTerms"] = llvm::json::Array{selector.path};
  packet["ownerPath"] = selector.path;
  packet["outputMode"] = options.code ? "source" : "outline";
  packet["truncated"] = false;
  llvm::json::Array matches;
  for (const auto &fact : index.facts) {
    if (fact.location.path != selector.path)
      continue;
    if (selector.end && (fact.location.end_line < selector.start || fact.location.start_line > selector.end))
      continue;
    llvm::json::Object match;
    match["name"] = fact.name;
    match["kind"] = fact.kind;
    match["visibility"] = "unknown";
    match["doc"] = false;
    match["location"] = location_for(fact);
    match["read"] = fact.location.path + ":" + std::to_string(fact.location.start_line) + ":" +
                    std::to_string(fact.location.end_line);
    match["truncated"] = false;
    match["fields"] = fields_for(fact, options.language);
    matches.push_back(std::move(match));
  }
  packet["matchCount"] = static_cast<std::int64_t>(matches.size());
  packet["matches"] = std::move(matches);
  llvm::json::Object safety;
  safety["level"] = "read-safe";
  safety["reason"] = "Clang AST projection is navigation evidence; exact source remains the patch preimage";
  safety["exactRead"] = selector.path + ":" + std::to_string(selector.start) + ":" +
                        std::to_string(selector.end ? selector.end : selector.start);
  packet["patchSafety"] = std::move(safety);
  if (options.code)
    packet["source"] = exact_source(options, selector);
  print_json(std::move(packet));
}

void emit_guide_packet(const Options &options) {
  auto packet = packet_base(options, "guide");
  packet["sourceAuthority"] = "clang-ast";
  packet["commands"] = llvm::json::Array{"search/prime", "search/owner", "search/lexical", "query/exact-selector"};
  print_json(std::move(packet));
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Options options = parse_options(argc, argv);
    if (!valid_language(options.language))
      throw std::runtime_error("--language must be c, cpp, or objective-c");
    if (options.command == "guide" || options.command == "help") {
      emit_guide_packet(options);
      return 0;
    }

    if (options.command == "query") {
      if (options.selector.empty())
        throw std::runtime_error("query requires --selector");
      const Selector selector = parse_selector(options.selector);
      const std::optional<std::string> compilation_database =
          options.compilation_database.empty() ? std::nullopt
                                               : std::optional<std::string>(options.compilation_database);
      const auto index =
          ccls_asp::build_index(options.workspace, selector.path, options.language, compilation_database);
      emit_query_packet(index, options, selector);
      return index.errors.empty() ? 0 : 1;
    }

    if (options.command == "search") {
      std::optional<std::string> owner;
      if (!options.owner.empty())
        owner = options.owner;
      const std::optional<std::string> compilation_database =
          options.compilation_database.empty() ? std::nullopt
                                               : std::optional<std::string>(options.compilation_database);
      const auto index = ccls_asp::build_index(options.workspace, owner, options.language, compilation_database);
      emit_search_packet(index, options);
      return index.errors.empty() ? 0 : 1;
    }

    throw std::runtime_error("unknown command: " + options.command);
  } catch (const std::exception &error) {
    std::cerr << "ccls-asp: " << error.what() << "\n";
    return 2;
  }
}
