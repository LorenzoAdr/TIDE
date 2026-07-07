#pragma once

extern "C" {
#include <tree_sitter/api.h>
}

namespace tgdb {

const TSLanguage* tree_sitter_cpp_language();

}  // namespace tgdb
