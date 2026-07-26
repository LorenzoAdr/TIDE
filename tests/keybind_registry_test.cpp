#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "ftxui/component/event.hpp"
#include "ui/key_bindings.hpp"
#include "ui/keybind/key_action.hpp"
#include "ui/keybind/key_binding_registry.hpp"

namespace {

int failures = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  using namespace tuide;

  const KeyBindingRegistry registry = KeyBindingRegistry::with_defaults();

  expect(registry.entries().size() == static_cast<std::size_t>(KeyAction::Count),
         "defaults register every KeyAction");

  std::set<KeyAction> seen;
  for (const auto& entry : registry.entries()) {
    seen.insert(entry.action);
    expect(!entry.chords.empty(), "every action has at least one chord");
    expect(!key_action_id(entry.action).empty(), "every action has a stable id");
    expect(key_action_from_id(key_action_id(entry.action)) == entry.action,
           "action id round-trips");
  }
  expect(seen.size() == static_cast<std::size_t>(KeyAction::Count),
         "no duplicate/missing actions in defaults");

  expect(registry.matches(KeyAction::QuickOpen, ftxui::Event::CtrlP), "Ctrl+P → QuickOpen");
  expect(registry.matches(KeyAction::GoToSymbol, ftxui::Event::CtrlO), "Ctrl+O → GoToSymbol");
  expect(registry.matches(KeyAction::Quit, ftxui::Event::CtrlQ), "Ctrl+Q → Quit");
  expect(registry.matches(KeyAction::OpenSettings, ftxui::Event::F10), "F10 → OpenSettings");
  expect(registry.matches(KeyAction::ToggleHelixMode, ftxui::Event::F6), "F6 → ToggleHelixMode");
  expect(registry.find(KeyAction::ToggleHelixMode) != nullptr &&
             !registry.find(KeyAction::ToggleHelixMode)->remappable,
         "ToggleHelixMode is not remappable");

  expect(registry.is_global_reserved(ftxui::Event::CtrlP), "Ctrl+P is global-reserved");
  expect(registry.is_app_reserved(ftxui::Event::F1), "F1 is app-reserved");
  expect(registry.is_app_reserved(ftxui::Event::F6), "F6 is app-reserved");
  expect(event_is_tuide_global_shortcut(ftxui::Event::CtrlP),
         "legacy event_is_tuide_global_shortcut uses registry");
  expect(event_is_tuide_app_shortcut(ftxui::Event::F10),
         "legacy event_is_tuide_app_shortcut uses registry");

  const auto conflicts = registry.find_conflicts();
  bool found_alt_left = false;
  bool found_f5 = false;
  for (const auto& conflict : conflicts) {
    if (conflict.chord_canonical == "alt+left") {
      found_alt_left = true;
      expect(conflict.context_dependent, "alt+left conflict is context-dependent");
    }
    if (conflict.chord_canonical == "f5") {
      found_f5 = true;
      expect(conflict.context_dependent, "f5 conflict is context-dependent");
    }
  }
  expect(found_alt_left, "reports alt+left Editor/Global dual use");
  expect(found_f5, "reports f5 QuickLaunch/DebugContinue dual use");

  nlohmann::json exported = registry.export_overrides();
  expect(exported.contains("bindings"), "export_overrides has bindings object");
  std::string err;
  KeyBindingRegistry mutable_registry = KeyBindingRegistry::with_defaults();
  expect(mutable_registry.import_overrides(exported, &err), "import empty overrides succeeds");
  expect(err.empty(), "import empty overrides has no error");

  nlohmann::json bad = nlohmann::json::object();
  bad["bindings"] = nlohmann::json::object({{"not_a_real_action", nlohmann::json::array({"x"})}});
  expect(!mutable_registry.import_overrides(bad, &err), "unknown action id rejected");
  expect(!err.empty(), "unknown action reports error");

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "keybind_registry_test OK\n";
  return EXIT_SUCCESS;
}
