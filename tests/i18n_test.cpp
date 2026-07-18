#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "i18n/locale.hpp"
#include "i18n/tr.hpp"

namespace {

void check(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void with_lang(const char* lang, const std::function<void()>& fn) {
  if (lang != nullptr) {
    setenv("LANG", lang, 1);
  }
  fn();
}

}  // namespace

int main() {
  try {
    tuide::i18n::set_locale(tuide::i18n::UiLocale::kEs);
    check(tuide::i18n::tr("welcome.tagline") == "depura y edita desde la terminal",
          "Spanish welcome tagline");
    check(tuide::i18n::tr("common.yes") == " Sí ", "Spanish yes");

    tuide::i18n::set_locale(tuide::i18n::UiLocale::kEn);
    check(tuide::i18n::tr("welcome.tagline") == "debug and edit from the terminal",
          "English welcome tagline");
    check(tuide::i18n::tr("common.yes") == " Yes ", "English yes");

    tuide::i18n::set_locale(tuide::i18n::UiLocale::kAuto);
    with_lang("en_US.UTF-8", [] {
      check(tuide::i18n::detect_system_locale() == tuide::i18n::UiLocale::kEn,
            "detect en from LANG");
    });
    with_lang("es_ES.UTF-8", [] {
      check(tuide::i18n::detect_system_locale() == tuide::i18n::UiLocale::kEs,
            "detect es from LANG");
    });

    tuide::i18n::set_locale(tuide::i18n::UiLocale::kEn);
    check(tuide::i18n::tr("nonexistent.key") == "[nonexistent.key]", "missing key marker");

    tuide::i18n::set_locale(tuide::i18n::UiLocale::kEs);
    check(tuide::i18n::tr_fmt("settings.locale.mode_label", {"Auto"}) == "Idioma (Auto)",
          "tr_fmt placeholder");

    std::cout << "i18n_test: all checks passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "i18n_test failed: " << ex.what() << '\n';
    return 1;
  }
}
