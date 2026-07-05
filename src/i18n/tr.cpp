#include "i18n/tr.hpp"

#include <sstream>

#include "i18n/locale.hpp"
#include "i18n/strings_internal.hpp"

namespace tgdb::i18n {

namespace {

const StringTable& table_for(UiLocale locale) {
  return locale == UiLocale::kEn ? english_strings() : spanish_strings();
}

std::string lookup(std::string_view key, UiLocale locale) {
  const StringTable& table = table_for(locale);
  const auto it = table.find(key);
  if (it != table.end()) {
    return std::string(it->second);
  }
  return {};
}

}  // namespace

std::string tr(std::string_view key) {
  const UiLocale locale = effective_locale();
  std::string value = lookup(key, locale);
  if (value.empty() && locale == UiLocale::kEn) {
    value = lookup(key, UiLocale::kEs);
  }
  if (value.empty()) {
    return std::string("[") + std::string(key) + "]";
  }
  return value;
}

std::string tr_fmt(std::string_view key, const std::vector<std::string>& args) {
  std::string text = tr(key);
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string placeholder = "{" + std::to_string(i) + "}";
    std::size_t pos = 0;
    while ((pos = text.find(placeholder, pos)) != std::string::npos) {
      text.replace(pos, placeholder.size(), args[i]);
      pos += args[i].size();
    }
  }
  return text;
}

}  // namespace tgdb::i18n
