#pragma once

#include <string>

extern "C" {
#include <tree_sitter/api.h>
}

namespace tgdb {

enum class TreeSitterLangKind { kCpp, kPython, kNone };

const TSLanguage* tree_sitter_cpp_language();
const TSLanguage* tree_sitter_python_language();

TreeSitterLangKind tree_sitter_lang_kind_for_path(const std::string& path);
const TSLanguage* tree_sitter_language_for_path(const std::string& path);

}  // namespace tgdb
