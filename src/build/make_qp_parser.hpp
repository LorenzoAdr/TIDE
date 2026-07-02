#pragma once

#include <map>
#include <string>
#include <vector>

namespace tgdb {

struct MakeQpInfo {
  std::map<std::string, std::string> variables;
  std::vector<std::string> targets;
  std::vector<std::string> output_dirs;
  std::vector<std::string> compile_flags;
};

MakeQpInfo parse_make_qp_output(const std::string& text);
std::vector<std::string> extract_compile_flags_from_make_vars(
    const std::map<std::string, std::string>& variables);

}  // namespace tgdb
