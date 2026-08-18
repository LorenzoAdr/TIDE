#pragma once

#include <string>

namespace tuide {
namespace l2_grammar {

// Absolute path to GBNF for llama-cli --grammar-file, or empty if disabled/missing.
std::string resolve_for_phase(const std::string& workspace_root, const std::string& phase);

}  // namespace l2_grammar
}  // namespace tuide
