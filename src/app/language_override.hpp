#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tuide {

struct LanguageChoice {
  const char* id = "";
  const char* label_i18n_key = "";
};

// Selectable languages for the status-bar picker (excludes auto).
const std::vector<LanguageChoice>& language_choices();

std::string language_display_name(const std::string& language_id);

// Runtime overrides keyed by normalize_lsp_path(path). Empty language clears.
void set_language_overrides(std::map<std::string, std::string> overrides);
std::map<std::string, std::string> language_overrides_snapshot();
void set_language_override_for_path(const std::string& path,
                                    const std::optional<std::string>& language_id);
std::optional<std::string> language_override_for_path(const std::string& path);
bool path_has_language_override(const std::string& path);

}  // namespace tuide
