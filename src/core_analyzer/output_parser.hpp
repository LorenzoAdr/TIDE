#pragma once

#include <string>
#include <vector>

#include "app/debug_model.hpp"

namespace tgdb {

struct CoreAnalyzerParseResult {
  std::vector<CoreAnalyzerInstance> instances;
  std::string raw_output;
};

CoreAnalyzerParseResult parse_obj_command_output(const std::string& output);
CoreAnalyzerParseResult parse_ref_command_output(const std::string& output);

std::string build_obj_search_command(const std::string& type_query);

}  // namespace tgdb
