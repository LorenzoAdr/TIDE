#pragma once

#include <cstdlib>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace tuide {
namespace l2_feat {

// Trial: L2_FEAT_<NAME>=1|0 overrides.
// Promoted defaults: tools/l2_battery/features_promoted.json (committed by sweep).
inline bool enabled(const char* name) {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }
  const std::string env_key = std::string("L2_FEAT_") + name;
  if (const char* e = std::getenv(env_key.c_str())) {
    if (e[0] == '0' && e[1] == '\0') {
      return false;
    }
    if (e[0] != '\0') {
      return true;
    }
  }
  const char* roots[] = {"tools/l2_battery/features_promoted.json",
                         ".tuide/ai/l2_facet_sweep/features_promoted.json"};
  for (const char* path : roots) {
    std::ifstream in(path);
    if (!in) {
      continue;
    }
    try {
      nlohmann::json j;
      in >> j;
      if (j.contains(name) && j[name].is_boolean()) {
        return j[name].get<bool>();
      }
      if (j.contains(name) && j[name].is_number_integer()) {
        return j[name].get<int>() != 0;
      }
    } catch (...) {
    }
  }
  return false;
}

}  // namespace l2_feat
}  // namespace tuide
