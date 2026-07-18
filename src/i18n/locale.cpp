#include "i18n/locale.hpp"

#include <cstdlib>
#include <cstring>

namespace tuide::i18n {

namespace {

UiLocale g_locale_setting = UiLocale::kAuto;
UiLocale g_effective_locale = UiLocale::kEs;

bool starts_with_ci(std::string_view text, std::string_view prefix) {
  if (text.size() < prefix.size()) {
    return false;
  }
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    const char a = text[i];
    const char b = prefix[i];
    if (a >= 'A' && a <= 'Z') {
      if (a + ('a' - 'A') != b && a != b) {
        return false;
      }
    } else if (a != b) {
      return false;
    }
  }
  return true;
}

}  // namespace

UiLocale detect_system_locale() {
  const char* lang = std::getenv("LANG");
  if (lang == nullptr || lang[0] == '\0') {
    lang = std::getenv("LC_ALL");
  }
  if (lang == nullptr || lang[0] == '\0') {
    return UiLocale::kEs;
  }
  const std::string_view value(lang);
  if (starts_with_ci(value, "en")) {
    return UiLocale::kEn;
  }
  return UiLocale::kEs;
}

UiLocale resolve_locale(UiLocale setting) {
  if (setting == UiLocale::kAuto) {
    return detect_system_locale();
  }
  return setting;
}

void set_locale(UiLocale locale) {
  g_locale_setting = locale;
  g_effective_locale = resolve_locale(locale);
}

UiLocale current_locale_setting() { return g_locale_setting; }

UiLocale effective_locale() { return g_effective_locale; }

const char* locale_tag(UiLocale locale) {
  switch (locale) {
    case UiLocale::kEs:
      return "es";
    case UiLocale::kEn:
      return "en";
    case UiLocale::kAuto:
    default:
      return "auto";
  }
}

UiLocale parse_locale(std::string_view tag) {
  if (tag == "en") {
    return UiLocale::kEn;
  }
  if (tag == "es") {
    return UiLocale::kEs;
  }
  return UiLocale::kAuto;
}

}  // namespace tuide::i18n
