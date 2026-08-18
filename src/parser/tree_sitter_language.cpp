#include "parser/tree_sitter_language.hpp"

#include <cctype>
#include <filesystem>

#include "lsp/lsp_uri.hpp"
#include "tree-sitter-cmake.h"
#include "tree-sitter-fortran.h"
#include "tree-sitter-go.h"
#include "tree-sitter-javascript.h"
#include "tree-sitter-latex.h"
#include "tree-sitter-make.h"
#include "tree-sitter-python.h"
#include "tree-sitter-rust.h"
#include "tree-sitter-typescript.h"
#include "tree-sitter-xml.h"
#include "tree-sitter-yaml.h"
#include "tree-sitter-zig.h"
#include "tree_sitter/tree-sitter-bash.h"
#include "tree_sitter/tree-sitter-lua.h"

extern "C" TSLanguage* tree_sitter_cpp(void);

namespace fs = std::filesystem;

namespace tuide {

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

const TSLanguage* tree_sitter_cmake_language() { return tree_sitter_cmake(); }

const TSLanguage* tree_sitter_make_language() { return tree_sitter_make(); }

const TSLanguage* tree_sitter_yaml_language() { return tree_sitter_yaml(); }

const TSLanguage* tree_sitter_xml_language() { return tree_sitter_xml(); }

TreeSitterLangKind tree_sitter_lang_kind_for_alias(const std::string& alias) {
  std::string key;
  key.reserve(alias.size());
  for (const unsigned char c : alias) {
    if (std::isspace(c) || c == '{' || c == '}') {
      if (!key.empty()) {
        break;
      }
      continue;
    }
    if (c == '.') {
      continue;
    }
    key.push_back(static_cast<char>(std::tolower(c)));
  }
  if (key.empty()) {
    return TreeSitterLangKind::kNone;
  }
  if (key == "c" || key == "cpp" || key == "c++" || key == "cc" || key == "cxx" || key == "h" ||
      key == "hpp" || key == "hh" || key == "hxx") {
    return TreeSitterLangKind::kCpp;
  }
  if (key == "python" || key == "py" || key == "python3" || key == "py3") {
    return TreeSitterLangKind::kPython;
  }
  if (key == "bash" || key == "sh" || key == "shell" || key == "shellscript" || key == "zsh" ||
      key == "ksh") {
    return TreeSitterLangKind::kBash;
  }
  if (key == "latex" || key == "tex") {
    return TreeSitterLangKind::kLatex;
  }
  if (key == "rust" || key == "rs") {
    return TreeSitterLangKind::kRust;
  }
  if (key == "go" || key == "golang") {
    return TreeSitterLangKind::kGo;
  }
  if (key == "zig") {
    return TreeSitterLangKind::kZig;
  }
  if (key == "fortran" || key == "f90" || key == "f95" || key == "f03" || key == "f08" ||
      key == "for") {
    return TreeSitterLangKind::kFortran;
  }
  if (key == "lua") {
    return TreeSitterLangKind::kLua;
  }
  if (key == "javascript" || key == "js" || key == "jsx") {
    return TreeSitterLangKind::kJavaScript;
  }
  if (key == "typescript" || key == "ts" || key == "tsx") {
    return TreeSitterLangKind::kTypeScript;
  }
  if (key == "cmake") {
    return TreeSitterLangKind::kCmake;
  }
  if (key == "make" || key == "makefile") {
    return TreeSitterLangKind::kMake;
  }
  if (key == "yaml" || key == "yml") {
    return TreeSitterLangKind::kYaml;
  }
  if (key == "xml") {
    return TreeSitterLangKind::kXml;
  }
  return TreeSitterLangKind::kNone;
}

TreeSitterLangKind tree_sitter_lang_kind_for_path(const std::string& path) {
  return tree_sitter_lang_kind_for_alias(language_id_for_path(path));
}

const TSLanguage* tree_sitter_language_for_kind(TreeSitterLangKind kind) {
  switch (kind) {
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
    case TreeSitterLangKind::kCmake:
      return tree_sitter_cmake_language();
    case TreeSitterLangKind::kMake:
      return tree_sitter_make_language();
    case TreeSitterLangKind::kYaml:
      return tree_sitter_yaml_language();
    case TreeSitterLangKind::kXml:
      return tree_sitter_xml_language();
    case TreeSitterLangKind::kCpp:
      return tree_sitter_cpp_language();
    case TreeSitterLangKind::kNone:
      break;
  }
  return nullptr;
}

const TSLanguage* tree_sitter_language_for_path(const std::string& path) {
  return tree_sitter_language_for_kind(tree_sitter_lang_kind_for_path(path));
}

}  // namespace tuide
