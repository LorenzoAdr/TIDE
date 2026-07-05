#pragma once

#include <string>
#include <string_view>

namespace tgdb::i18n {

enum class UiLocale {
  kEs,
  kEn,
  kAuto,
};

UiLocale detect_system_locale();
UiLocale resolve_locale(UiLocale setting);
void set_locale(UiLocale locale);
UiLocale current_locale_setting();
UiLocale effective_locale();

const char* locale_tag(UiLocale locale);
UiLocale parse_locale(std::string_view tag);

}  // namespace tgdb::i18n
