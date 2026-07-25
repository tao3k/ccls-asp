// SPDX-License-Identifier: Apache-2.0
#include "asp_parser.hh"

#include <clang/AST/ASTConsumer.h>
#include <clang/Tooling/ArgumentsAdjusters.h>

#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclObjC.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <cstdlib>
#include <iomanip>
#include <llvm/Support/Path.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <tuple>

namespace fs = std::filesystem;

namespace ccls_asp {
namespace {

struct CollectorState {
  fs::path workspace;
  std::string language;
  std::vector<Fact> facts;
  std::set<std::string> fact_keys;
  std::vector<DependencyUsage> dependency_usages;
  std::set<std::string> dependency_keys;
};

std::string normalize_path(const fs::path &path) { return path.lexically_normal().generic_string(); }

std::optional<std::string> project_path(const clang::SourceManager &sm, clang::SourceLocation loc,
                                        const fs::path &workspace) {
  if (loc.isInvalid())
    return std::nullopt;
  const auto presumed = sm.getPresumedLoc(sm.getExpansionLoc(loc));
  if (!presumed.isValid())
    return std::nullopt;

  std::error_code ec;
  fs::path absolute = fs::weakly_canonical(fs::path(presumed.getFilename()), ec);
  if (ec)
    absolute = fs::absolute(fs::path(presumed.getFilename()), ec);
  const fs::path relative = fs::relative(absolute, workspace, ec);
  if (ec || relative.empty() || relative.native().starts_with(".."))
    return std::nullopt;
  return normalize_path(relative);
}

std::uint32_t line_for(const clang::SourceManager &sm, clang::SourceLocation loc) {
  if (loc.isInvalid())
    return 1;
  const auto presumed = sm.getPresumedLoc(sm.getExpansionLoc(loc));
  return presumed.isValid() ? presumed.getLine() : 1;
}

class FactVisitor : public clang::RecursiveASTVisitor<FactVisitor> {
public:
  FactVisitor(clang::ASTContext &context, CollectorState &state)
      : source_manager_(context.getSourceManager()), state_(state) {}

  bool VisitFunctionDecl(clang::FunctionDecl *decl) {
    if (decl->isImplicit())
      return true;
    std::string kind = "function";
    if (llvm::isa<clang::CXXConstructorDecl>(decl))
      kind = "constructor";
    else if (llvm::isa<clang::CXXDestructorDecl>(decl))
      kind = "destructor";
    else if (llvm::isa<clang::CXXMethodDecl>(decl))
      kind = "method";
    add_named(decl, kind, decl->isThisDeclarationADefinition() ? "definition" : "declaration",
              decl->getType().getAsString());
    return true;
  }

  bool VisitRecordDecl(clang::RecordDecl *decl) {
    if (decl->isImplicit())
      return true;
    std::string kind = decl->isUnion() ? "union" : "struct";
    if (const auto *cxx = llvm::dyn_cast<clang::CXXRecordDecl>(decl); cxx && cxx->isClass())
      kind = "class";
    add_named(decl, kind, decl->isThisDeclarationADefinition() ? "definition" : "declaration");
    return true;
  }

  bool VisitCXXRecordDecl(clang::CXXRecordDecl *decl) {
    if (!decl->isThisDeclarationADefinition() || decl->isImplicit())
      return true;
    for (const auto &base : decl->bases()) {
      const auto base_name = base.getType().getAsString();
      add_named(decl, "inheritance", "relation", {}, base_name);
    }
    return true;
  }

  bool VisitEnumDecl(clang::EnumDecl *decl) {
    add_named(decl, "enum", decl->isThisDeclarationADefinition() ? "definition" : "declaration");
    return true;
  }

  bool VisitEnumConstantDecl(clang::EnumConstantDecl *decl) {
    add_named(decl, "enum-member", "definition", decl->getType().getAsString());
    return true;
  }

  bool VisitFieldDecl(clang::FieldDecl *decl) {
    add_named(decl, "field", "definition", decl->getType().getAsString());
    return true;
  }

  bool VisitVarDecl(clang::VarDecl *decl) {
    if (llvm::isa<clang::ParmVarDecl>(decl) || decl->isImplicit())
      return true;
    add_named(decl, decl->isLocalVarDecl() ? "local-variable" : "variable",
              decl->isThisDeclarationADefinition() ? "definition" : "declaration", decl->getType().getAsString());
    return true;
  }

  bool VisitTypedefNameDecl(clang::TypedefNameDecl *decl) {
    add_named(decl, "type-alias", "definition", decl->getUnderlyingType().getAsString());
    return true;
  }

  bool VisitNamespaceDecl(clang::NamespaceDecl *decl) {
    add_named(decl, "namespace", "definition");
    return true;
  }

  bool VisitCallExpr(clang::CallExpr *expr) {
    if (const auto *callee = expr->getDirectCallee())
      add_at(callee->getNameAsString(), callee->getQualifiedNameAsString(), "call", "reference", expr->getSourceRange(),
             {}, callee->getQualifiedNameAsString());
    return true;
  }

  bool VisitObjCInterfaceDecl(clang::ObjCInterfaceDecl *decl) {
    add_named(decl, "objc-interface", decl->isThisDeclarationADefinition() ? "definition" : "declaration");
    return true;
  }

  bool VisitObjCProtocolDecl(clang::ObjCProtocolDecl *decl) {
    add_named(decl, "objc-protocol", decl->isThisDeclarationADefinition() ? "definition" : "declaration");
    return true;
  }

  bool VisitObjCCategoryDecl(clang::ObjCCategoryDecl *decl) {
    add_named(decl, "objc-category", "definition");
    return true;
  }

  bool VisitObjCMethodDecl(clang::ObjCMethodDecl *decl) {
    add_named(decl, decl->isInstanceMethod() ? "objc-instance-method" : "objc-class-method",
              decl->isThisDeclarationADefinition() ? "definition" : "declaration", decl->getReturnType().getAsString());
    return true;
  }

  bool VisitObjCPropertyDecl(clang::ObjCPropertyDecl *decl) {
    add_named(decl, "objc-property", "definition", decl->getType().getAsString());
    return true;
  }

  bool VisitObjCMessageExpr(clang::ObjCMessageExpr *expr) {
    add_at(expr->getSelector().getAsString(), expr->getSelector().getAsString(), "objc-message", "reference",
           expr->getSourceRange(), {}, expr->getSelector().getAsString());
    return true;
  }

private:
  void add_named(const clang::NamedDecl *decl, std::string kind, std::string role, std::string type = {},
                 std::string target = {}) {
    if (!decl || decl->getNameAsString().empty())
      return;
    add_at(decl->getNameAsString(), decl->getQualifiedNameAsString(), std::move(kind), std::move(role),
           decl->getSourceRange(), std::move(type), std::move(target));
  }

  void add_at(std::string name, std::string qualified_name, std::string kind, std::string role,
              clang::SourceRange range, std::string type, std::string target) {
    auto path = project_path(source_manager_, range.getBegin(), state_.workspace);
    if (!path || !supports_source_path(*path, state_.language))
      return;
    const auto start = line_for(source_manager_, range.getBegin());
    const auto end = std::max(start, line_for(source_manager_, range.getEnd()));
    const std::string key = *path + ":" + std::to_string(start) + ":" + std::to_string(end) + ":" + kind + ":" +
                            qualified_name + ":" + target;
    if (!state_.fact_keys.insert(key).second)
      return;
    state_.facts.push_back({std::move(name),
                            std::move(qualified_name),
                            std::move(kind),
                            std::move(role),
                            std::move(type),
                            std::move(target),
                            {*path, start, end}});
  }

  clang::SourceManager &source_manager_;
  CollectorState &state_;
};

class FactConsumer : public clang::ASTConsumer {
public:
  FactConsumer(clang::ASTContext &context, CollectorState &state) : visitor_(context, state) {}

  void HandleTranslationUnit(clang::ASTContext &context) override {
    visitor_.TraverseDecl(context.getTranslationUnitDecl());
  }

private:
  FactVisitor visitor_;
};

class DependencyCallbacks : public clang::PPCallbacks {
public:
  DependencyCallbacks(clang::SourceManager &source_manager, CollectorState &state)
      : source_manager_(source_manager), state_(state) {}

  void InclusionDirective(clang::SourceLocation hash_location, const clang::Token &, llvm::StringRef file_name, bool,
                          clang::CharSourceRange, clang::OptionalFileEntryRef, llvm::StringRef, llvm::StringRef,
                          const clang::Module *, bool, clang::SrcMgr::CharacteristicKind) override {
    const auto owner_path = project_path(source_manager_, hash_location, state_.workspace);
    if (!owner_path || !supports_source_path(*owner_path, state_.language))
      return;
    const std::string import_path = file_name.str();
    if (import_path.empty())
      return;
    const auto separator = import_path.find('/');
    const std::string package_name = import_path.substr(0, separator);
    const auto line = line_for(source_manager_, hash_location);
    const std::string key = *owner_path + ":" + std::to_string(line) + ":" + import_path;
    if (!state_.dependency_keys.insert(key).second)
      return;
    std::set<std::string> keys{package_name, import_path};
    const fs::path include_path(import_path);
    keys.insert(include_path.filename().string());
    keys.insert(include_path.stem().string());
    state_.dependency_usages.push_back({
        *owner_path,
        package_name,
        import_path,
        *owner_path + ":" + std::to_string(line) + ":" + std::to_string(line),
        {keys.begin(), keys.end()},
    });
  }

private:
  clang::SourceManager &source_manager_;
  CollectorState &state_;
};

class FactAction : public clang::ASTFrontendAction {
public:
  explicit FactAction(CollectorState &state) : state_(state) {}

  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef) override {
    compiler.getPreprocessor().addPPCallbacks(
        std::make_unique<DependencyCallbacks>(compiler.getSourceManager(), state_));
    return std::make_unique<FactConsumer>(compiler.getASTContext(), state_);
  }

private:
  CollectorState &state_;
};

class FactActionFactory : public clang::tooling::FrontendActionFactory {
public:
  explicit FactActionFactory(CollectorState &state) : state_(state) {}

  std::unique_ptr<clang::FrontendAction> create() override { return std::make_unique<FactAction>(state_); }

private:
  CollectorState &state_;
};

std::vector<std::string> source_files(const fs::path &workspace, const std::string &language) {
  std::vector<std::string> files;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(workspace, fs::directory_options::skip_permission_denied, ec), end;
       it != end; it.increment(ec)) {
    if (ec) {
      ec.clear();
      continue;
    }
    if (it->is_directory()) {
      const auto name = it->path().filename().string();
      if (name == ".git" || name == ".cache" || name == "build" || name == "target")
        it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file() && supports_source_path(it->path().string(), language))
      files.push_back(it->path().string());
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::vector<std::string> fallback_args(const std::string &language) {
  if (language == "c")
    return {"-x", "c", "-std=c17", "-fsyntax-only"};
  if (language == "objective-c")
    return {"-x", "objective-c", "-fsyntax-only"};
  return {"-x", "c++", "-std=c++17", "-fsyntax-only"};
}

} // namespace

bool supports_source_path(const std::string &path, const std::string &language) {
  std::string extension = fs::path(path).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (language == "c")
    return extension == ".c" || extension == ".h";
  if (language == "objective-c")
    return extension == ".m" || extension == ".mm" || extension == ".h";
  return extension == ".cc" || extension == ".cpp" || extension == ".cxx" || extension == ".c++" ||
         extension == ".hh" || extension == ".hpp" || extension == ".hxx" || extension == ".h";
}

static std::vector<std::string> environment_compiler_args() {
  std::vector<std::string> args;
  const auto append_flags = [&](const char *value) {
    if (!value || !*value)
      return;
    std::istringstream input(value);
    std::string argument;
    while (input >> std::quoted(argument))
      args.push_back(argument);
  };

  append_flags(std::getenv("NIX_CFLAGS_COMPILE"));
  append_flags(std::getenv("CCLS_ASP_EXTRA_CLANG_ARGS"));

  std::string_view implicit_include_dirs = CCLS_ASP_CXX_IMPLICIT_INCLUDE_DIRS;
  while (!implicit_include_dirs.empty()) {
    const auto separator = implicit_include_dirs.find('|');
    const auto include_dir = implicit_include_dirs.substr(0, separator);
    if (!include_dir.empty()) {
      args.emplace_back("-isystem");
      args.emplace_back(include_dir);
    }
    if (separator == std::string_view::npos)
      break;
    implicit_include_dirs.remove_prefix(separator + 1);
  }

  // Nix's compiler wrapper adds c++/v1 only when its executable runs.
  // ClangTool consumes compilation commands in-process, so materialize it.
  for (std::size_t index = 0; index + 1 < args.size(); ++index) {
    if (args[index] != "-isystem")
      continue;
    const fs::path cxx_headers = fs::path(args[index + 1]) / "c++" / "v1";
    std::error_code ec;
    if (fs::is_directory(cxx_headers, ec)) {
      args.push_back("-isystem");
      args.push_back(cxx_headers.string());
      break;
    }
  }

  if (const char *sdk_root = std::getenv("SDKROOT"); sdk_root && *sdk_root) {
    args.push_back("-isysroot");
    args.emplace_back(sdk_root);
  }
  args.emplace_back("-resource-dir=" CCLS_ASP_CLANG_RESOURCE_DIR);
  return args;
}

ParseResult parse_translation_units(const std::string &workspace, const std::vector<std::string> &owners,
                                    const std::string &language,
                                    const std::optional<std::string> &compilation_database) {
  ParseResult result;
  std::error_code ec;
  fs::path root = fs::weakly_canonical(fs::path(workspace), ec);
  if (ec || !fs::is_directory(root)) {
    result.errors.push_back("workspace is not a directory: " + workspace);
    return result;
  }

  CollectorState state{root, language, {}, {}, {}, {}};
  std::string database_error;
  fs::path database_root = root;
  if (compilation_database) {
    database_root = fs::path(*compilation_database);
    if (database_root.is_relative())
      database_root = root / database_root;
    database_root = fs::weakly_canonical(database_root, ec);
  }
  auto database = clang::tooling::CompilationDatabase::autoDetectFromDirectory(database_root.string(), database_error);

  std::vector<std::string> files;
  if (!owners.empty()) {
    for (const auto &owner : owners) {
      fs::path selected = fs::path(owner);
      if (selected.is_relative())
        selected = root / selected;
      selected = fs::weakly_canonical(selected, ec);
      if (!ec && fs::is_regular_file(selected))
        files.push_back(selected.string());
    }
  } else if (database) {
    files = database->getAllFiles();
    files.erase(std::remove_if(files.begin(), files.end(),
                               [&](const std::string &path) { return !supports_source_path(path, language); }),
                files.end());
  } else {
    files = source_files(root, language);
  }

  std::sort(files.begin(), files.end());
  files.erase(std::unique(files.begin(), files.end()), files.end());
  for (const auto &file : files) {
    const auto relative = fs::relative(fs::path(file), root, ec);
    if (!ec && !relative.native().starts_with(".."))
      result.translation_units.push_back(normalize_path(relative));
  }

  FactActionFactory factory(state);
  if (database && !files.empty()) {
    clang::tooling::ClangTool tool(*database, files);
    const auto compiler_args = environment_compiler_args();
    if (!compiler_args.empty()) {
      tool.appendArgumentsAdjuster(
          clang::tooling::getInsertArgumentAdjuster(compiler_args, clang::tooling::ArgumentInsertPosition::BEGIN));
    }
    const int status = tool.run(&factory);
    if (status != 0)
      result.errors.push_back("ClangTool failed with status " + std::to_string(status));
  } else {
    const auto args = fallback_args(language);
    for (const auto &file : files) {
      std::ifstream input(file);
      std::ostringstream buffer;
      buffer << input.rdbuf();
      if (!clang::tooling::runToolOnCodeWithArgs(std::make_unique<FactAction>(state), buffer.str(), args, file,
                                                 "ccls-asp"))
        result.errors.push_back("failed to parse " + file);
    }
  }

  std::sort(state.facts.begin(), state.facts.end(), [](const Fact &left, const Fact &right) {
    return std::tie(left.location.path, left.location.start_line, left.kind, left.qualified_name) <
           std::tie(right.location.path, right.location.start_line, right.kind, right.qualified_name);
  });
  result.facts = std::move(state.facts);
  std::sort(state.dependency_usages.begin(), state.dependency_usages.end(),
            [](const DependencyUsage &left, const DependencyUsage &right) {
              return std::tie(left.owner_path, left.source_locator, left.import_path) <
                     std::tie(right.owner_path, right.source_locator, right.import_path);
            });
  result.dependency_usages = std::move(state.dependency_usages);
  return result;
}

} // namespace ccls_asp
