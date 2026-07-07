#include "parser/tree_sitter_language.hpp"

extern "C" TSLanguage* tree_sitter_cpp(void);

namespace tgdb {

const TSLanguage* tree_sitter_cpp_language() { return tree_sitter_cpp(); }

}  // namespace tgdb
