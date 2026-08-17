#pragma once

#include <string>

extern "C" {
#include <tree_sitter/api.h>
}

namespace tuide {

enum class TreeSitterLangKind {
  kCpp,
  kPython,
  kBash,
  kLatex,
  kRust,
  kGo,
  kZig,
  kFortran,
  kLua,
  kJavaScript,
  kTypeScript,
  kCmake,
  kMake,
  kYaml,
  kXml,
  kNone,
};

const TSLanguage* tree_sitter_cpp_language();
const TSLanguage* tree_sitter_python_language();
const TSLanguage* tree_sitter_bash_language();
const TSLanguage* tree_sitter_latex_language();
const TSLanguage* tree_sitter_rust_language();
const TSLanguage* tree_sitter_go_language();
const TSLanguage* tree_sitter_zig_language();
const TSLanguage* tree_sitter_fortran_language();
const TSLanguage* tree_sitter_lua_language();
const TSLanguage* tree_sitter_javascript_language();
const TSLanguage* tree_sitter_typescript_language();
const TSLanguage* tree_sitter_cmake_language();
const TSLanguage* tree_sitter_make_language();
const TSLanguage* tree_sitter_yaml_language();
const TSLanguage* tree_sitter_xml_language();

TreeSitterLangKind tree_sitter_lang_kind_for_path(const std::string& path);
const TSLanguage* tree_sitter_language_for_path(const std::string& path);

}  // namespace tuide
