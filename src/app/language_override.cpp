#include "app/language_override.hpp"

#include <mutex>

#include "i18n/tr.hpp"
#include "lsp/lsp_uri.hpp"

namespace tuide {
namespace {

std::mutex g_mu;
std::map<std::string, std::string> g_overrides;

std::string override_key(const std::string& path) {
  const std::string normalized = normalize_lsp_path(path);
  return normalized.empty() ? path : normalized;
}

}  // namespace

const std::vector<LanguageChoice>& language_choices() {
  static const std::vector<LanguageChoice> kChoices = {
      {"c", "status.language.c"},
      {"cpp", "status.language.cpp"},
      {"python", "status.language.python"},
      {"shellscript", "status.language.shellscript"},
      {"latex", "status.language.latex"},
      {"rust", "status.language.rust"},
      {"go", "status.language.go"},
      {"zig", "status.language.zig"},
      {"fortran", "status.language.fortran"},
      {"lua", "status.language.lua"},
      {"javascript", "status.language.javascript"},
      {"typescript", "status.language.typescript"},
      {"cmake", "status.language.cmake"},
      {"make", "status.language.make"},
      {"yaml", "status.language.yaml"},
      {"xml", "status.language.xml"},
      {"plaintext", "status.language.plaintext"},
  };
  return kChoices;
}

std::string language_display_name(const std::string& language_id) {
  if (language_id.empty() || language_id == "plaintext") {
    return i18n::tr("status.language.none");
  }
  for (const auto& choice : language_choices()) {
    if (language_id == choice.id) {
      return i18n::tr(choice.label_i18n_key);
    }
  }
  return language_id;
}

void set_language_overrides(std::map<std::string, std::string> overrides) {
  std::map<std::string, std::string> normalized;
  for (auto& entry : overrides) {
    if (entry.first.empty() || entry.second.empty()) {
      continue;
    }
    normalized[override_key(entry.first)] = std::move(entry.second);
  }
  std::lock_guard<std::mutex> lock(g_mu);
  g_overrides = std::move(normalized);
}

std::map<std::string, std::string> language_overrides_snapshot() {
  std::lock_guard<std::mutex> lock(g_mu);
  return g_overrides;
}

void set_language_override_for_path(const std::string& path,
                                    const std::optional<std::string>& language_id) {
  if (path.empty()) {
    return;
  }
  const std::string key = override_key(path);
  std::lock_guard<std::mutex> lock(g_mu);
  if (!language_id.has_value() || language_id->empty()) {
    g_overrides.erase(key);
    return;
  }
  g_overrides[key] = *language_id;
}

std::optional<std::string> language_override_for_path(const std::string& path) {
  if (path.empty()) {
    return std::nullopt;
  }
  const std::string key = override_key(path);
  std::lock_guard<std::mutex> lock(g_mu);
  const auto it = g_overrides.find(key);
  if (it == g_overrides.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool path_has_language_override(const std::string& path) {
  return language_override_for_path(path).has_value();
}

}  // namespace tuide
