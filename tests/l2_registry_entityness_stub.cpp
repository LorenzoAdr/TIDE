#include "ai/l2_effect_registry.hpp"
#include "ai/l2_entityness.hpp"

namespace tuide {

bool registry_open(const std::string& /*workspace_root*/, EffectRegistry* /*out*/,
                   std::string* err) {
  if (err) {
    *err = "stub registry";
  }
  return false;
}

void registry_close(EffectRegistry* /*r*/) {}

bool entityness_score_problem_frame(EffectRegistry* /*r*/, const ProblemFrame& /*pf*/,
                                    const std::string& /*query*/, const RegistryEmbedFn& /*embed*/,
                                    const EntitynessOpts& /*opts*/, EntitynessLinkReport* /*out*/,
                                    std::string* err) {
  if (err) {
    *err = "stub entityness";
  }
  return false;
}

nlohmann::json EntitynessLinkReport::to_json() const { return nlohmann::json::object(); }

}  // namespace tuide
