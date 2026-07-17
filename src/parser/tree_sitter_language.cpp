#include "parser/tree_sitter_language.hpp"

#include <filesystem>

#include "lsp/lsp_uri.hpp"
#include "tree-sitter-latex.h"
#include "tree-sitter-python.h"
#include "tree_sitter/tree-sitter-bash.h"

extern "C" TSLanguage* tree_sitter_cpp(void);

namespace fs = std::filesystem;

namespace tgdb {

const TSLanguage* tree_sitter_cpp_language() { return tree_sitter_cpp(); }

const TSLanguage* tree_sitter_python_language() { return tree_sitter_python(); }

const TSLanguage* tree_sitter_bash_language() { return tree_sitter_bash(); }

const TSLanguage* tree_sitter_latex_language() { return tree_sitter_latex(); }

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
    case TreeSitterLangKind::kCpp:
      return tree_sitter_cpp_language();
    case TreeSitterLangKind::kNone:
      break;
  }
  return nullptr;
}

}  // namespace tgdb
