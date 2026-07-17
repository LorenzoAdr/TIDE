#include "parser/tree_sitter_language.hpp"

#include <filesystem>

#include "lsp/lsp_uri.hpp"
#include "tree-sitter-fortran.h"
#include "tree-sitter-go.h"
#include "tree-sitter-javascript.h"
#include "tree-sitter-latex.h"
#include "tree-sitter-python.h"
#include "tree-sitter-rust.h"
#include "tree-sitter-typescript.h"
#include "tree-sitter-zig.h"
#include "tree_sitter/tree-sitter-bash.h"
#include "tree_sitter/tree-sitter-lua.h"

extern "C" TSLanguage* tree_sitter_cpp(void);

namespace fs = std::filesystem;

namespace tgdb {

const TSLanguage* tree_sitter_cpp_language() { return tree_sitter_cpp(); }

const TSLanguage* tree_sitter_python_language() { return tree_sitter_python(); }

const TSLanguage* tree_sitter_bash_language() { return tree_sitter_bash(); }

const TSLanguage* tree_sitter_latex_language() { return tree_sitter_latex(); }

const TSLanguage* tree_sitter_rust_language() { return tree_sitter_rust(); }

const TSLanguage* tree_sitter_go_language() { return tree_sitter_go(); }

const TSLanguage* tree_sitter_zig_language() { return tree_sitter_zig(); }

const TSLanguage* tree_sitter_fortran_language() { return tree_sitter_fortran(); }

const TSLanguage* tree_sitter_lua_language() { return tree_sitter_lua(); }

const TSLanguage* tree_sitter_javascript_language() { return tree_sitter_javascript(); }

const TSLanguage* tree_sitter_typescript_language() { return tree_sitter_typescript(); }

TreeSitterLangKind tree_sitter_lang_kind_for_path(const std::string& path) {
  const std::string lang = language_id_for_path(path);
  if (lang == "python") {
    return TreeSitterLangKind::kPython;
  }
  if (lang == "shellscript") {
    return TreeSitterLangKind::kBash;
  }
  if (lang == "latex") {
    return TreeSitterLangKind::kLatex;
  }
  if (lang == "rust") {
    return TreeSitterLangKind::kRust;
  }
  if (lang == "go") {
    return TreeSitterLangKind::kGo;
  }
  if (lang == "zig") {
    return TreeSitterLangKind::kZig;
  }
  if (lang == "fortran") {
    return TreeSitterLangKind::kFortran;
  }
  if (lang == "lua") {
    return TreeSitterLangKind::kLua;
  }
  if (lang == "javascript") {
    return TreeSitterLangKind::kJavaScript;
  }
  if (lang == "typescript") {
    return TreeSitterLangKind::kTypeScript;
  }
  if (lang == "c" || lang == "cpp") {
    return TreeSitterLangKind::kCpp;
  }
  return TreeSitterLangKind::kNone;
}

const TSLanguage* tree_sitter_language_for_path(const std::string& path) {
  switch (tree_sitter_lang_kind_for_path(path)) {
    case TreeSitterLangKind::kPython:
      return tree_sitter_python_language();
    case TreeSitterLangKind::kBash:
      return tree_sitter_bash_language();
    case TreeSitterLangKind::kLatex:
      return tree_sitter_latex_language();
    case TreeSitterLangKind::kRust:
      return tree_sitter_rust_language();
    case TreeSitterLangKind::kGo:
      return tree_sitter_go_language();
    case TreeSitterLangKind::kZig:
      return tree_sitter_zig_language();
    case TreeSitterLangKind::kFortran:
      return tree_sitter_fortran_language();
    case TreeSitterLangKind::kLua:
      return tree_sitter_lua_language();
    case TreeSitterLangKind::kJavaScript:
      return tree_sitter_javascript_language();
    case TreeSitterLangKind::kTypeScript:
      return tree_sitter_typescript_language();
    case TreeSitterLangKind::kCpp:
      return tree_sitter_cpp_language();
    case TreeSitterLangKind::kNone:
      break;
  }
  return nullptr;
}

}  // namespace tgdb
