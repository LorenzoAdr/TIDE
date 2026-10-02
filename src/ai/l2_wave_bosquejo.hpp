#pragma once

#include <string>
#include <vector>

#include "ai/l2_effect_registry.hpp"
#include "ai/l2_wave.hpp"

namespace tuide {

// Foto de barrios a partir de olores de zona (clase de sitio, no nombres del ancla).
// Sin peek, sin ids.
bool wave_control_bosquejo_from_registry(EffectRegistry* r, const RegistryEmbedFn& embed,
                                         const std::vector<std::string>& conceptos,
                                         WaveControlBosquejo* out, std::string* err);

}  // namespace tuide
