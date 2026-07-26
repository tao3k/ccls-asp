// SPDX-License-Identifier: Apache-2.0
#include "asp_parser.hh"

#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/SHA256.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using ccls_asp::Fact;
using ccls_asp::ParseResult;

namespace {

struct Options {
  std::string language = "cpp";
  std::string workspace = ".";
  std::string command;
  std::string search_view;
  std::vector<std::string> owners;
  std::string selector;
  std::string compilation_database;
  bool code = false;
};

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
    else if (arg == "--owner") {
      std::string owner;
      take(owner);
      options.owners.push_back(std::move(owner));
    } else if (arg == "--code")
      options.code = true;
    else if (!arg.starts_with("--"))
      positional.push_back(std::move(arg));
    else
      throw std::runtime_error("unsupported option: " + arg);
  }

  if (positional.empty())
    options.command = "guide";
  else {
    options.command = positional[0];
    if (options.command == "search") {
      if (positional.size() != 2)
        throw std::runtime_error("search requires exactly one provider operation: ingest");
      options.search_view = positional[1];
    } else if (positional.size() != 1)
      throw std::runtime_error(options.command + " does not accept positional arguments");
  }
  return options;
}

llvm::json::Object fields_for(const Fact &fact, const std::string &language) {
  llvm::json::Object fields;
  fields["languageId"] = language;
  fields["providerId"] = "ccls-asp";
  fields["semanticFactKind"] = fact.kind;
  fields["role"] = fact.role;
  fields["visibility"] = fact.visibility;
  fields["qualifiedName"] = fact.qualified_name;
  fields["sourceAuthority"] = "clang-ast";
  fields["factClass"] = fact.role == "reference" ? "occurrence" : (fact.role == "relation" ? "relation" : "item");
  fields["startColumn"] = fact.location.start_column;
  fields["endColumn"] = fact.location.end_column;
  fields["startOffset"] = static_cast<std::int64_t>(fact.location.start_offset);
  fields["endOffset"] = static_cast<std::int64_t>(fact.location.end_offset);
  if (!fact.symbol_id.empty())
    fields["symbolId"] = fact.symbol_id;
  if (!fact.type.empty())
    fields["type"] = fact.type;
  if (!fact.target.empty())
    fields["target"] = fact.target;
  if (!fact.target_symbol_id.empty())
    fields["targetSymbolId"] = fact.target_symbol_id;
  if (!fact.container_symbol_id.empty())
    fields["containerSymbolId"] = fact.container_symbol_id;
  return fields;
}

llvm::json::Object location_for(const Fact &fact) {
  llvm::json::Object location;
  location["path"] = fact.location.path;
  location["lineRange"] = std::to_string(fact.location.start_line) + ":" + std::to_string(fact.location.end_line);
  location["displayLineRange"] =
      std::to_string(fact.location.start_line) + ":" + std::to_string(fact.location.end_line);
  location["sourceLocatorHint"] = fact.location.path + ":" + std::to_string(fact.location.start_line) + ":" +
                                  std::to_string(fact.location.end_line);
  if (!fact.location.structural_selector.empty())
    location["structuralSelector"] = fact.location.structural_selector;
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

std::vector<const Fact *> all_facts(const ParseResult &result) {
  std::vector<const Fact *> facts;
  facts.reserve(result.facts.size());
  for (const auto &fact : result.facts) {
    facts.push_back(&fact);
  }
  return facts;
}

std::string sha256_file(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream content;
  content << input.rdbuf();
  llvm::SHA256 hash;
  hash.update(content.str());
  return llvm::toHex(hash.final(), true);
}

llvm::json::Array query_keys_for(const Fact &fact) {
  std::set<std::string> keys{fact.name, fact.qualified_name, fact.kind};
  if (!fact.symbol_id.empty())
    keys.insert(fact.symbol_id);
  if (!fact.target.empty())
    keys.insert(fact.target);
  if (!fact.target_symbol_id.empty())
    keys.insert(fact.target_symbol_id);
  if (!fact.container_symbol_id.empty())
    keys.insert(fact.container_symbol_id);
  if (!fact.location.structural_selector.empty())
    keys.insert(fact.location.structural_selector);
  llvm::json::Array result;
  for (const auto &key : keys) {
    if (!key.empty())
      result.push_back(key);
  }
  return result;
}

std::string syntax_kind_for(const Fact &fact) {
  if (fact.kind == "function" || fact.kind == "function-template" || fact.kind == "lambda")
    return "function";
  if (fact.kind == "method" || fact.kind == "constructor" || fact.kind == "destructor" ||
      fact.kind == "objc-instance-method" || fact.kind == "objc-class-method")
    return "method";
  if (fact.kind == "class" || fact.kind == "class-template")
    return "class";
  if (fact.kind == "struct" || fact.kind == "union")
    return "struct";
  if (fact.kind == "enum" || fact.kind == "enum-member")
    return "enum";
  if (fact.kind == "objc-interface" || fact.kind == "objc-protocol")
    return "interface";
  if (fact.kind == "field")
    return "field";
  if (fact.kind == "objc-property")
    return "property";
  if (fact.kind == "parameter")
    return "argument";
  if (fact.kind == "variable" || fact.kind == "local-variable")
    return "binding";
  if (fact.kind == "call" || fact.kind == "indirect-call" || fact.kind == "constructor-call" ||
      fact.kind == "objc-message")
    return "call";
  if (fact.kind == "type-alias" || fact.kind == "type-reference")
    return "type";
  if (fact.kind == "macro-definition" || fact.kind == "macro-expansion")
    return "macro";
  return "custom";
}

std::string syntax_relation_kind_for(const Fact &fact) {
  if (fact.kind == "call" || fact.kind == "indirect-call" || fact.kind == "constructor-call" ||
      fact.kind == "objc-message")
    return "calls";
  if (fact.kind == "override" || fact.kind == "objc-protocol-conformance" || fact.kind == "objc-protocol-inheritance")
    return "implements";
  if (fact.role == "reference")
    return "references";
  return "related";
}

llvm::json::Object syntax_fact_for(const Fact &fact) {
  llvm::json::Object syntax_fact;
  const std::string location_id = fact.location.path + ":" + std::to_string(fact.location.start_offset) + ":" +
                                  std::to_string(fact.location.end_offset);
  const std::string identity = !fact.symbol_id.empty()
                                   ? fact.symbol_id
                                   : (!fact.target_symbol_id.empty() ? fact.target_symbol_id : fact.qualified_name);
  syntax_fact["id"] = !fact.location.structural_selector.empty()
                          ? fact.location.structural_selector
                          : "clang-occurrence:" + fact.kind + ":" + identity + "@" + location_id;
  syntax_fact["kind"] = syntax_kind_for(fact);
  syntax_fact["source"] = "native-parser";
  syntax_fact["languageKind"] = fact.kind;
  syntax_fact["name"] = fact.name;
  if (!fact.qualified_name.empty())
    syntax_fact["qualifiedName"] = fact.qualified_name;
  syntax_fact["ownerPath"] = fact.location.path;
  syntax_fact["location"] = location_for(fact);
  syntax_fact["visibility"] = fact.visibility;
  syntax_fact["queryKeys"] = query_keys_for(fact);

  llvm::json::Object fields;
  fields["role"] = fact.role;
  if (!fact.type.empty())
    fields["type"] = fact.type;
  if (!fact.symbol_id.empty())
    fields["symbolId"] = fact.symbol_id;
  if (!fact.target_symbol_id.empty())
    fields["targetSymbolId"] = fact.target_symbol_id;
  if (!fact.container_symbol_id.empty())
    fields["containerSymbolId"] = fact.container_symbol_id;
  if (!fact.target.empty())
    fields["target"] = fact.target;
  syntax_fact["fields"] = std::move(fields);

  if (!fact.target_symbol_id.empty() || !fact.target.empty()) {
    llvm::json::Object relation;
    relation["kind"] = syntax_relation_kind_for(fact);
    relation["target"] = !fact.target_symbol_id.empty() ? fact.target_symbol_id : fact.target;
    if (!fact.target.empty() && !fact.target_symbol_id.empty()) {
      llvm::json::Object relation_fields;
      relation_fields["displayTarget"] = fact.target;
      relation["fields"] = std::move(relation_fields);
    }
    llvm::json::Array relations;
    relations.push_back(std::move(relation));
    syntax_fact["relations"] = std::move(relations);
  }
  return syntax_fact;
}

llvm::json::Array query_keys_for(const ccls_asp::DependencyUsage &usage) {
  llvm::json::Array result;
  for (const auto &key : usage.query_keys) {
    if (!key.empty())
      result.push_back(key);
  }
  return result;
}

void emit_ingest_packet(const ParseResult &result, const Options &options) {
  const auto selected = all_facts(result);
  const fs::path root = fs::weakly_canonical(options.workspace);
  std::set<std::string> owner_paths(result.translation_units.begin(), result.translation_units.end());
  for (const Fact *fact : selected)
    owner_paths.insert(fact->location.path);

  llvm::SHA256 generation_hash;
  generation_hash.update(options.language);
  std::map<std::string, std::string> compile_context_by_translation_unit;
  llvm::json::Array compile_contexts;
  for (const auto &context : result.compile_contexts) {
    generation_hash.update(context.translation_unit);
    generation_hash.update(context.digest);
    compile_context_by_translation_unit[context.translation_unit] = context.digest;
    llvm::json::Object compile_context;
    compile_context["translationUnit"] = context.translation_unit;
    compile_context["digest"] = context.digest;
    compile_contexts.push_back(std::move(compile_context));
  }
  llvm::json::Array file_hashes;
  for (const auto &path : owner_paths) {
    const std::string digest = sha256_file(root / path);
    generation_hash.update(path);
    generation_hash.update(digest);
    llvm::json::Object file_hash;
    file_hash["path"] = path;
    file_hash["sha256"] = digest;
    file_hash["source"] = "workspace";
    if (const auto context = compile_context_by_translation_unit.find(path);
        context != compile_context_by_translation_unit.end())
      file_hash["compileContextDigest"] = context->second;
    file_hashes.push_back(std::move(file_hash));
  }

  llvm::json::Array owners;
  for (const auto &path : owner_paths) {
    std::set<std::string> keys{path};
    for (const Fact *fact : selected) {
      if (fact->location.path != path)
        continue;
      keys.insert(fact->name);
      keys.insert(fact->qualified_name);
      keys.insert(fact->kind);
    }
    llvm::json::Array query_keys;
    for (const auto &key : keys) {
      if (!key.empty())
        query_keys.push_back(key);
    }
    llvm::json::Object owner;
    owner["ownerPath"] = path;
    owner["ownerKind"] = "source";
    owner["sourceAuthority"] = "clang-ast";
    owner["queryKeys"] = std::move(query_keys);
    owners.push_back(std::move(owner));
  }

  std::map<std::string, const Fact *> canonical_items;
  for (const Fact *fact : selected) {
    if (fact->role != "definition" && fact->role != "declaration")
      continue;
    const std::string key = !fact->location.structural_selector.empty()
                                ? fact->location.structural_selector
                                : fact->location.path + ":" + std::to_string(fact->location.start_offset) + ":" +
                                      fact->kind + ":" + fact->qualified_name;
    const auto [entry, inserted] = canonical_items.emplace(key, fact);
    if (!inserted && entry->second->role == "declaration" && fact->role == "definition")
      entry->second = fact;
  }

  llvm::json::Array symbols;
  for (const auto &entry : canonical_items) {
    const Fact *fact = entry.second;
    llvm::json::Object symbol;
    symbol["ownerPath"] = fact->location.path;
    symbol["name"] = fact->name;
    symbol["qualifiedName"] = fact->qualified_name;
    symbol["kind"] = fact->kind;
    symbol["visibility"] = fact->visibility;
    if (!fact->symbol_id.empty())
      symbol["symbolId"] = fact->symbol_id;
    if (!fact->target.empty())
      symbol["target"] = fact->target;
    if (!fact->target_symbol_id.empty())
      symbol["targetSymbolId"] = fact->target_symbol_id;
    if (!fact->container_symbol_id.empty())
      symbol["containerSymbolId"] = fact->container_symbol_id;
    if (!fact->location.structural_selector.empty())
      symbol["structuralSelector"] = fact->location.structural_selector;
    symbol["role"] = fact->role;
    symbol["queryKeys"] = query_keys_for(*fact);
    symbol["sourceLocator"] = fact->location.path + ":" + std::to_string(fact->location.start_line) + ":" +
                              std::to_string(fact->location.end_line);
    symbols.push_back(std::move(symbol));
  }

  llvm::json::Array occurrences;
  llvm::json::Array relations;
  for (const Fact *fact : selected) {
    if (fact->role != "reference" && fact->role != "relation")
      continue;
    llvm::json::Object projected;
    projected["id"] = "clang-occurrence:" + fact->kind + ":" +
                      (!fact->target_symbol_id.empty() ? fact->target_symbol_id : fact->qualified_name) + "@" +
                      fact->location.path + ":" + std::to_string(fact->location.start_offset) + ":" +
                      std::to_string(fact->location.end_offset);
    projected["ownerPath"] = fact->location.path;
    projected["name"] = fact->name;
    projected["kind"] = fact->kind;
    projected["sourceLocator"] = fact->location.path + ":" + std::to_string(fact->location.start_line) + ":" +
                                 std::to_string(fact->location.end_line);
    projected["startColumn"] = fact->location.start_column;
    projected["endColumn"] = fact->location.end_column;
    projected["startOffset"] = static_cast<std::int64_t>(fact->location.start_offset);
    projected["endOffset"] = static_cast<std::int64_t>(fact->location.end_offset);
    if (!fact->symbol_id.empty())
      projected["sourceSymbolId"] = fact->symbol_id;
    if (!fact->target_symbol_id.empty())
      projected["targetSymbolId"] = fact->target_symbol_id;
    if (!fact->container_symbol_id.empty())
      projected["containerSymbolId"] = fact->container_symbol_id;
    if (!fact->target.empty())
      projected["target"] = fact->target;
    projected["queryKeys"] = query_keys_for(*fact);
    if (fact->role == "reference")
      occurrences.push_back(std::move(projected));
    else
      relations.push_back(std::move(projected));
  }

  llvm::json::Object packet;
  packet["schemaId"] = "agent.semantic-protocols.semantic-structural-index";
  packet["schemaVersion"] = "1";
  packet["protocolId"] = "agent.semantic-protocols.semantic-language";
  packet["protocolVersion"] = "1";
  packet["generationId"] = "sha256:" + llvm::toHex(generation_hash.final(), true);
  packet["languageId"] = options.language;
  packet["providerId"] = "ccls-asp";
  packet["providerVersion"] = "0.1.0";
  packet["exportMethod"] = "index/structural";
  packet["projectRoot"] = options.workspace;
  packet["rawSourceStored"] = false;
  packet["sourceAuthority"] = "clang-ast";
  packet["compileContexts"] = std::move(compile_contexts);
  packet["fileHashes"] = std::move(file_hashes);
  packet["owners"] = std::move(owners);
  packet["symbols"] = std::move(symbols);
  packet["symbolTotal"] = static_cast<std::int64_t>(canonical_items.size());
  packet["occurrences"] = std::move(occurrences);
  packet["relations"] = std::move(relations);
  llvm::json::Array dependency_usages;
  for (const auto &usage : result.dependency_usages) {
    llvm::json::Object dependency;
    dependency["ownerPath"] = usage.owner_path;
    dependency["packageName"] = usage.package_name;
    dependency["importPath"] = usage.import_path;
    if (!usage.resolved_path.empty())
      dependency["resolvedPath"] = usage.resolved_path;
    dependency["includeKind"] = usage.angled ? "angle" : "quote";
    dependency["source"] = "clang-preprocessor";
    dependency["sourceLocator"] = usage.source_locator;
    dependency["queryKeys"] = query_keys_for(usage);
    dependency_usages.push_back(std::move(dependency));
  }
  packet["dependencyUsageTotal"] = static_cast<std::int64_t>(dependency_usages.size());
  packet["dependencyUsages"] = std::move(dependency_usages);
  llvm::json::Array syntax_facts;
  for (const Fact *fact : selected)
    syntax_facts.push_back(syntax_fact_for(*fact));
  packet["syntaxFacts"] = std::move(syntax_facts);

  for (const auto &error : result.errors)
    llvm::errs() << "ccls-asp: " << error << "\n";
  print_json(std::move(packet));
}

struct Selector {
  std::string path;
  std::uint32_t start = 1;
  std::uint32_t end = 0;
  std::string structural_selector;
};

Selector parse_selector(const std::string &selector) {
  const auto scheme = selector.find("://");
  const auto fragment = scheme == std::string::npos ? std::string::npos : selector.find('#', scheme + 3);
  if (scheme != std::string::npos && fragment != std::string::npos && fragment > scheme + 3)
    return {selector.substr(scheme + 3, fragment - scheme - 3), 1, 0, selector};

  Selector parsed{selector, 1, 0, {}};
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
    parsed = {selector, 1, 0, {}};
  }
  return parsed;
}

const Fact *canonical_fact_for_selector(const ParseResult &index, const std::string &selector) {
  const Fact *selected = nullptr;
  for (const auto &fact : index.facts) {
    if (fact.location.structural_selector != selector)
      continue;
    if (!selected || (selected->role == "declaration" && fact.role == "definition"))
      selected = &fact;
  }
  return selected;
}

Selector resolve_selector(const ParseResult &index, Selector selector) {
  if (selector.structural_selector.empty())
    return selector;
  const Fact *fact = canonical_fact_for_selector(index, selector.structural_selector);
  if (!fact)
    throw std::runtime_error("canonical selector did not resolve: " + selector.structural_selector);
  selector.path = fact->location.path;
  selector.start = fact->location.start_line;
  selector.end = fact->location.end_line;
  return selector;
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

void emit_query_packet(const ParseResult &index, const Options &options, const Selector &selector) {
  auto packet = packet_base(options, "query/exact-selector");
  packet["schemaId"] = "agent.semantic-protocols.semantic-query-packet";
  const std::string query = selector.structural_selector.empty() ? selector.path : selector.structural_selector;
  packet["query"] = query;
  packet["queryTerms"] = llvm::json::Array{query};
  packet["ownerPath"] = selector.path;
  packet["outputMode"] = options.code ? "source" : "outline";
  packet["truncated"] = false;
  llvm::json::Array matches;
  const Fact *canonical_fact =
      selector.structural_selector.empty() ? nullptr : canonical_fact_for_selector(index, selector.structural_selector);
  for (const auto &fact : index.facts) {
    if (fact.location.path != selector.path)
      continue;
    if (canonical_fact && &fact != canonical_fact)
      continue;
    if (selector.structural_selector.empty() && selector.end &&
        (fact.location.end_line < selector.start || fact.location.start_line > selector.end))
      continue;
    llvm::json::Object match;
    match["name"] = fact.name;
    match["kind"] = fact.kind;
    match["visibility"] = fact.visibility;
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
  packet["commands"] = llvm::json::Array{"search/ingest", "query/exact-selector"};
  print_json(std::move(packet));
}

std::vector<std::string> ingest_owners(const Options &options) {
  std::vector<std::string> owners = options.owners;
  if (isatty(STDIN_FILENO))
    return owners;

  std::string line;
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty())
      continue;
    std::string candidate = line;
    const auto separator = line.find(':');
    if (separator != std::string::npos) {
      const std::string prefix = line.substr(0, separator);
      if (fs::is_regular_file(fs::path(options.workspace) / prefix))
        candidate = prefix;
    }
    owners.push_back(std::move(candidate));
  }
  std::sort(owners.begin(), owners.end());
  owners.erase(std::unique(owners.begin(), owners.end()), owners.end());
  return owners;
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Options options = parse_options(argc, argv);
    if (!valid_language(options.language))
      throw std::runtime_error("--language must be c, cpp, or objective-c");
    if (options.command == "guide" || options.command == "help") {
      if (!options.selector.empty() || !options.owners.empty() || !options.compilation_database.empty() || options.code)
        throw std::runtime_error("guide does not accept query or ingest options");
      emit_guide_packet(options);
      return 0;
    }

    if (options.command == "query") {
      if (options.selector.empty())
        throw std::runtime_error("query requires --selector");
      if (!options.owners.empty())
        throw std::runtime_error("query does not accept --owner");
      const Selector requested_selector = parse_selector(options.selector);
      const std::optional<std::string> compilation_database =
          options.compilation_database.empty() ? std::nullopt
                                               : std::optional<std::string>(options.compilation_database);
      const auto result = ccls_asp::parse_translation_units(options.workspace, {requested_selector.path},
                                                            options.language, compilation_database);
      const Selector selector = resolve_selector(result, requested_selector);
      emit_query_packet(result, options, selector);
      return result.errors.empty() ? 0 : 1;
    }

    if (options.command == "search") {
      if (options.search_view != "ingest")
        throw std::runtime_error("provider search supports only ingest; use the asp language facade for search");
      if (!options.selector.empty() || options.code)
        throw std::runtime_error("search ingest does not accept query options");
      const std::optional<std::string> compilation_database =
          options.compilation_database.empty() ? std::nullopt
                                               : std::optional<std::string>(options.compilation_database);
      const auto result = ccls_asp::parse_translation_units(options.workspace, ingest_owners(options), options.language,
                                                            compilation_database);
      emit_ingest_packet(result, options);
      return result.errors.empty() ? 0 : 1;
    }

    throw std::runtime_error("unknown command: " + options.command);
  } catch (const std::exception &error) {
    std::cerr << "ccls-asp: " << error.what() << "\n";
    return 2;
  }
}
