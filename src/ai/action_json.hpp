#pragma once

#include <string>

namespace tuide {

// Drop <think>…</think> (and markdown ```json fences) so the JSON after CoT remains.
std::string strip_model_think(const std::string& raw);

// Last complete JSON object whose first-level "action" is a string.
std::string extract_action_json(const std::string& raw);

// Last complete object with "action":"ola_v…". Ignores a leading `{` of CoT.
std::string extract_ola_json(const std::string& raw);

}  // namespace tuide
