#include "ui/keybind/key_binding_registry.hpp"

#include <algorithm>
#include <utility>

#include <nlohmann/json.hpp>

#include "ui/key_bindings.hpp"
#include "ui/keybind/key_chord_util.hpp"

namespace tuide {
namespace {

KeyStrokeSpec stroke(std::string canonical, KeyEventMatcher matcher) {
  KeyStrokeSpec out;
  out.canonical = std::move(canonical);
  out.matches = std::move(matcher);
  return out;
}

KeyChordSpec chord1(std::string canonical, KeyEventMatcher matcher) {
  KeyChordSpec out;
  out.canonical = canonical;
  out.strokes.push_back(stroke(std::move(canonical), std::move(matcher)));
  return out;
}

KeyChordSpec chord2(std::string canonical, KeyStrokeSpec first, KeyStrokeSpec second) {
  KeyChordSpec out;
  out.canonical = std::move(canonical);
  out.strokes.push_back(std::move(first));
  out.strokes.push_back(std::move(second));
  return out;
}

KeyBindingEntry make_entry(KeyAction action, KeyScope scope, std::vector<KeyChordSpec> chords,
                           bool remappable = true, bool global_reserved = false,
                           bool app_reserved = false) {
  KeyBindingEntry entry;
  entry.action = action;
  entry.scope = scope;
  entry.remappable = remappable;
  entry.global_reserved = global_reserved;
  entry.app_reserved = app_reserved;
  entry.chords = std::move(chords);
  return entry;
}

}  // namespace

KeyBindingRegistry KeyBindingRegistry::with_defaults() {
  KeyBindingRegistry registry;

  auto& e = registry.entries_;
  e.reserve(static_cast<std::size_t>(KeyAction::Count));

  // --- Global ---
  e.push_back(make_entry(
      KeyAction::OpenExternalFile, KeyScope::Global,
      {chord1("f1", [](const ftxui::Event& ev) { return event_is_f1(ev); })}, true, false, true));
  e.push_back(make_entry(
      KeyAction::OpenExternalFileHere, KeyScope::Global,
      {chord1("ctrl+alt+e", [](const ftxui::Event& ev) { return event_is_ctrl_alt_e(ev); })}, true,
      false, true));
  e.push_back(make_entry(
      KeyAction::OpenShortcutsModal, KeyScope::Global,
      {chord1("alt+f1|shift+f1",
              [](const ftxui::Event& ev) { return event_is_open_shortcuts_modal(ev); })},
      true, false, true));
  e.push_back(make_entry(
      KeyAction::OpenDebugWizard, KeyScope::Global,
      {chord1("f2", [](const ftxui::Event& ev) { return ev == ftxui::Event::F2; })}, true, false,
      true));
  e.push_back(make_entry(
      KeyAction::OpenWorkspaceWizard, KeyScope::Global,
      {chord1("f3", [](const ftxui::Event& ev) { return ev == ftxui::Event::F3; })}, true, false,
      true));
  e.push_back(make_entry(
      KeyAction::FocusTerminalTab, KeyScope::Global,
      {chord1("f4", [](const ftxui::Event& ev) { return ev == ftxui::Event::F4; })}, true, true,
      true));
  e.push_back(make_entry(
      KeyAction::QuickLaunch, KeyScope::Global,
      {chord1("f5", [](const ftxui::Event& ev) { return ev == ftxui::Event::F5; })}, true, false,
      true));
  e.push_back(make_entry(
      KeyAction::OpenSearchPanel, KeyScope::Global,
      {chord1("f7|ctrl+h|ctrl+alt+h|ctrl+shift+h",
              [](const ftxui::Event& ev) { return event_is_open_search_panel(ev); })},
      true, false, true));
  e.push_back(make_entry(
      KeyAction::OpenOutlinePanel, KeyScope::Global,
      {chord1("f8|ctrl+alt+o|ctrl+shift+o",
              [](const ftxui::Event& ev) { return event_is_open_outline_panel(ev); })},
      true, false, true));
  e.push_back(make_entry(
      KeyAction::OpenProblemsPanel, KeyScope::Global,
      {chord1("f9", [](const ftxui::Event& ev) { return ev == ftxui::Event::F9; })}, true, false,
      true));
  e.push_back(make_entry(
      KeyAction::OpenBinarySymbolsPanel, KeyScope::Global,
      {chord1("ctrl+shift+s",
              [](const ftxui::Event& ev) { return event_is_open_binary_symbols_panel(ev); })},
      true, false, true));
  e.push_back(make_entry(
      KeyAction::OpenSettings, KeyScope::Global,
      {chord1("f10", [](const ftxui::Event& ev) { return ev == ftxui::Event::F10; })}, true, false,
      true));
  e.push_back(make_entry(
      KeyAction::QuickOpen, KeyScope::Global,
      {chord1("ctrl+p", [](const ftxui::Event& ev) { return event_is_quick_open(ev); })}, true, true,
      true));
  e.push_back(make_entry(
      KeyAction::GoToSymbol, KeyScope::Global,
      {chord1("ctrl+o", [](const ftxui::Event& ev) { return event_is_ctrl_o(ev); })}, true, true,
      true));
  e.push_back(make_entry(
      KeyAction::ToggleBottomPanel, KeyScope::Global,
      {chord1("ctrl+t", [](const ftxui::Event& ev) { return ev == ftxui::Event::CtrlT; })}, true,
      true, true));
  e.push_back(make_entry(
      KeyAction::ToggleConsoleExpand, KeyScope::Global,
      {chord1("ctrl+alt+j", [](const ftxui::Event& ev) { return event_is_ctrl_alt_j(ev); })},
      true, true, true));
  e.push_back(make_entry(
      KeyAction::Quit, KeyScope::Global,
      {chord1("ctrl+q", [](const ftxui::Event& ev) { return ev == ftxui::Event::CtrlQ; })}, true,
      true, true));
  e.push_back(make_entry(
      KeyAction::FocusExplorer, KeyScope::Global,
      {chord1("ctrl+alt+a", [](const ftxui::Event& ev) { return event_is_ctrl_alt_a(ev); })}, true,
      true, true));
  e.push_back(make_entry(
      KeyAction::FocusEditor, KeyScope::Global,
      {chord1("ctrl+e", [](const ftxui::Event& ev) { return ev == ftxui::Event::CtrlE; })}, true,
      true, true));
  e.push_back(make_entry(
      KeyAction::FocusMoveLeft, KeyScope::Global,
      {chord1("alt+left", [](const ftxui::Event& ev) { return event_is_alt_left(ev); })}));
  e.push_back(make_entry(
      KeyAction::FocusMoveRight, KeyScope::Global,
      {chord1("alt+right", [](const ftxui::Event& ev) { return event_is_alt_right(ev); })}));
  e.push_back(make_entry(
      KeyAction::FocusMoveDown, KeyScope::Global,
      {chord1("alt+down", [](const ftxui::Event& ev) { return event_is_alt_down(ev); })}));
  e.push_back(make_entry(
      KeyAction::FocusMoveUp, KeyScope::Global,
      {chord1("alt+up", [](const ftxui::Event& ev) { return event_is_alt_up(ev); })}));
  e.push_back(make_entry(
      KeyAction::WorkspaceSearchSelection, KeyScope::Global,
      {chord1("ctrl+alt+f|ctrl+shift+f",
              [](const ftxui::Event& ev) { return event_is_workspace_search_with_selection(ev); })},
      true, false, true));
  e.push_back(make_entry(
      KeyAction::ToggleHelixMode, KeyScope::Global,
      {chord1("f6", [](const ftxui::Event& ev) { return ev == ftxui::Event::F6; })},
      /*remappable=*/false, false, true));

  // --- Editor ---
  e.push_back(make_entry(
      KeyAction::SaveFile, KeyScope::Editor,
      {chord1("ctrl+s", [](const ftxui::Event& ev) { return ev == ftxui::Event::CtrlS; })}, true,
      false, true));
  e.push_back(make_entry(
      KeyAction::FindInFile, KeyScope::Editor,
      {chord1("ctrl+f", [](const ftxui::Event& ev) { return event_is_ctrl_f(ev); })}, true, false,
      true));
  e.push_back(make_entry(
      KeyAction::GoToLine, KeyScope::Editor,
      {chord1("ctrl+g", [](const ftxui::Event& ev) { return event_is_ctrl_g(ev); })}, true, false,
      true));
  e.push_back(make_entry(
      KeyAction::Undo, KeyScope::Editor,
      {chord1("ctrl+z", [](const ftxui::Event& ev) {
         return event_is_ctrl_z(ev) && !event_input_has_shift_modifier(ev);
       })}));
  e.push_back(make_entry(
      KeyAction::Redo, KeyScope::Editor,
      {chord1("ctrl+alt+z|ctrl+shift+z|ctrl+y", [](const ftxui::Event& ev) {
         return event_is_ctrl_alt_z(ev) || event_is_ctrl_shift_z(ev) || event_is_ctrl_y(ev);
       })}));
  e.push_back(make_entry(
      KeyAction::Copy, KeyScope::Editor,
      {chord1("ctrl+c", [](const ftxui::Event& ev) { return event_is_ctrl_c(ev); })}));
  e.push_back(make_entry(
      KeyAction::Cut, KeyScope::Editor,
      {chord1("ctrl+x", [](const ftxui::Event& ev) { return event_is_ctrl_x(ev); })}));
  e.push_back(make_entry(
      KeyAction::Paste, KeyScope::Editor,
      {chord1("ctrl+v|insert", [](const ftxui::Event& ev) {
         return event_is_ctrl_v(ev) || ev == ftxui::Event::Insert;
       })}));
  e.push_back(make_entry(
      KeyAction::CommentLines, KeyScope::Editor,
      {chord2("ctrl+k ctrl+c", stroke("ctrl+k", [](const ftxui::Event& ev) {
                                return event_is_ctrl_k(ev);
                              }),
              stroke("ctrl+c", [](const ftxui::Event& ev) { return event_is_ctrl_c(ev); }))}));
  e.push_back(make_entry(
      KeyAction::UncommentLines, KeyScope::Editor,
      {chord2("ctrl+k ctrl+u", stroke("ctrl+k", [](const ftxui::Event& ev) {
                                return event_is_ctrl_k(ev);
                              }),
              stroke("ctrl+u", [](const ftxui::Event& ev) { return event_is_ctrl_u(ev); }))}));
  e.push_back(make_entry(
      KeyAction::HalfPageUp, KeyScope::Editor,
      {chord1("ctrl+u", [](const ftxui::Event& ev) { return event_is_ctrl_u(ev); })}));
  e.push_back(make_entry(
      KeyAction::HalfPageDown, KeyScope::Editor,
      {chord1("ctrl+i", [](const ftxui::Event& ev) { return event_is_ctrl_i(ev); })}));
  e.push_back(make_entry(
      KeyAction::DeleteWordBackward, KeyScope::Editor,
      {chord1("ctrl+backspace", [](const ftxui::Event& ev) { return event_is_ctrl_backspace(ev); })}));
  e.push_back(make_entry(
      KeyAction::DeleteWordForward, KeyScope::Editor,
      {chord1("ctrl+delete", [](const ftxui::Event& ev) { return event_is_ctrl_delete(ev); })}));
  e.push_back(make_entry(
      KeyAction::SelectNextMatch, KeyScope::Editor,
      {chord1("ctrl+d|ctrl+alt+d|ctrl+shift+d", [](const ftxui::Event& ev) {
         return event_is_ctrl_d(ev) || event_is_ctrl_alt_d(ev) || event_is_ctrl_shift_d(ev);
       })}));
  e.push_back(make_entry(
      KeyAction::SelectAllMatches, KeyScope::Editor,
      {chord1("ctrl+alt+l|ctrl+shift+l", [](const ftxui::Event& ev) {
         return event_is_ctrl_alt_l(ev) || event_is_ctrl_shift_l(ev);
       })}));
  e.push_back(make_entry(
      KeyAction::TriggerCompletion, KeyScope::Editor,
      {chord1("ctrl+space|ctrl+.|ctrl+alt+.|ctrl+alt+/",
              [](const ftxui::Event& ev) { return event_is_completion(ev); })}));
  e.push_back(make_entry(
      KeyAction::GoToDefinition, KeyScope::Editor,
      {chord1("f12", [](const ftxui::Event& ev) { return event_is_go_to_definition(ev); })}));
  e.push_back(make_entry(
      KeyAction::GoToDeclaration, KeyScope::Editor,
      {chord1("shift+f12|ctrl+alt+f12|ctrl+shift+f12",
              [](const ftxui::Event& ev) { return event_is_go_to_declaration(ev); })}));
  e.push_back(make_entry(
      KeyAction::CursorHistoryBack, KeyScope::Editor,
      {chord1("alt+left", [](const ftxui::Event& ev) { return event_is_alt_left(ev); })}));
  e.push_back(make_entry(
      KeyAction::CursorHistoryForward, KeyScope::Editor,
      {chord1("alt+right", [](const ftxui::Event& ev) { return event_is_alt_right(ev); })}));
  e.push_back(make_entry(
      KeyAction::Indent, KeyScope::Editor,
      {chord1("tab", [](const ftxui::Event& ev) { return event_is_plain_tab(ev); })}));
  e.push_back(make_entry(
      KeyAction::Unindent, KeyScope::Editor,
      {chord1("shift+tab", [](const ftxui::Event& ev) { return ev == ftxui::Event::TabReverse; })}));
  e.push_back(make_entry(
      KeyAction::ToggleBreakpoint, KeyScope::Editor,
      {chord1("ctrl+b", [](const ftxui::Event& ev) { return ev == ftxui::Event::CtrlB; })}, true,
      true, true));
  e.push_back(make_entry(
      KeyAction::ExtendLeft, KeyScope::Editor,
      {chord1("shift+left", [](const ftxui::Event& ev) { return event_is_shift_left(ev); })}));
  e.push_back(make_entry(
      KeyAction::ExtendRight, KeyScope::Editor,
      {chord1("shift+right", [](const ftxui::Event& ev) { return event_is_shift_right(ev); })}));
  e.push_back(make_entry(
      KeyAction::ExtendUp, KeyScope::Editor,
      {chord1("shift+up", [](const ftxui::Event& ev) { return event_is_shift_up(ev); })}));
  e.push_back(make_entry(
      KeyAction::ExtendDown, KeyScope::Editor,
      {chord1("shift+down", [](const ftxui::Event& ev) { return event_is_shift_down(ev); })}));
  e.push_back(make_entry(
      KeyAction::ExtendHome, KeyScope::Editor,
      {chord1("shift+home", [](const ftxui::Event& ev) { return event_is_shift_home(ev); })}));
  e.push_back(make_entry(
      KeyAction::ExtendEnd, KeyScope::Editor,
      {chord1("shift+end", [](const ftxui::Event& ev) { return event_is_shift_end(ev); })}));
  e.push_back(make_entry(
      KeyAction::WordLeft, KeyScope::Editor,
      {chord1("ctrl+left|ctrl+alt+left|ctrl+shift+left", [](const ftxui::Event& ev) {
         return event_is_ctrl_left(ev) || event_is_ctrl_alt_left(ev) ||
                event_is_ctrl_shift_left(ev);
       })}));
  e.push_back(make_entry(
      KeyAction::WordRight, KeyScope::Editor,
      {chord1("ctrl+right|ctrl+alt+right|ctrl+shift+right", [](const ftxui::Event& ev) {
         return event_is_ctrl_right(ev) || event_is_ctrl_alt_right(ev) ||
                event_is_ctrl_shift_right(ev);
       })}));
  e.push_back(make_entry(
      KeyAction::BlockSelectUp, KeyScope::Editor,
      {chord1("ctrl+alt+up|ctrl+shift+up", [](const ftxui::Event& ev) {
         return event_is_ctrl_alt_up(ev) || event_is_ctrl_shift_up(ev);
       })}));
  e.push_back(make_entry(
      KeyAction::BlockSelectDown, KeyScope::Editor,
      {chord1("ctrl+alt+down|ctrl+shift+down", [](const ftxui::Event& ev) {
         return event_is_ctrl_alt_down(ev) || event_is_ctrl_shift_down(ev);
       })}));

  // --- Debug ---
  e.push_back(make_entry(
      KeyAction::DebugContinue, KeyScope::Debug,
      {chord1("f5", [](const ftxui::Event& ev) { return ev == ftxui::Event::F5; })}, true, false,
      true));
  e.push_back(make_entry(
      KeyAction::DebugStepOver, KeyScope::Debug,
      {chord1("f10", [](const ftxui::Event& ev) { return ev == ftxui::Event::F10; })}, true, false,
      true));
  e.push_back(make_entry(
      KeyAction::DebugStepInto, KeyScope::Debug,
      {chord1("f11", [](const ftxui::Event& ev) { return ev == ftxui::Event::F11; })}, true, false,
      true));
  e.push_back(make_entry(
      KeyAction::DebugStepOut, KeyScope::Debug,
      {chord1("shift+f11",
              [](const ftxui::Event& ev) { return ev == ftxui::Event::Special({24}); })},
      true, false, true));
  e.push_back(make_entry(
      KeyAction::DebugSourceSubstitute, KeyScope::Debug,
      {chord1("ctrl+shift+s", [](const ftxui::Event& ev) { return event_is_ctrl_shift_s(ev); })},
      true, false, true));

  return registry;
}

bool KeyBindingRegistry::chord_matches_single(const KeyChordSpec& chord,
                                              const ftxui::Event& event) const {
  return chord.strokes.size() == 1 && chord.strokes.front().matches &&
         chord.strokes.front().matches(event);
}

const KeyBindingEntry* KeyBindingRegistry::find(KeyAction action) const {
  for (const auto& entry : entries_) {
    if (entry.action == action) {
      return &entry;
    }
  }
  return nullptr;
}

const std::vector<KeyChordSpec>* KeyBindingRegistry::chords_for(KeyAction action) const {
  for (const auto& override_entry : overrides_) {
    if (override_entry.action == action) {
      return &override_entry.chords;
    }
  }
  const KeyBindingEntry* entry = find(action);
  return entry != nullptr ? &entry->chords : nullptr;
}

std::vector<KeyChordSpec> KeyBindingRegistry::effective_chords(KeyAction action) const {
  const std::vector<KeyChordSpec>* chords = chords_for(action);
  if (chords == nullptr) {
    return {};
  }
  return *chords;
}

bool KeyBindingRegistry::matches(KeyAction action, const ftxui::Event& event) const {
  const std::vector<KeyChordSpec>* chords = chords_for(action);
  if (chords == nullptr) {
    return false;
  }
  for (const auto& chord : *chords) {
    if (chord_matches_single(chord, event)) {
      return true;
    }
  }
  return false;
}

std::optional<KeyAction> KeyBindingRegistry::match(const ftxui::Event& event,
                                                   KeyScope scope) const {
  const uint8_t mask = to_mask(scope);
  for (const auto& entry : entries_) {
    if ((to_mask(entry.scope) & mask) == 0) {
      continue;
    }
    if (matches(entry.action, event)) {
      return entry.action;
    }
  }
  return std::nullopt;
}

bool KeyBindingRegistry::is_global_reserved(const ftxui::Event& event) const {
  for (const auto& entry : entries_) {
    if (!entry.global_reserved) {
      continue;
    }
    if (matches(entry.action, event)) {
      return true;
    }
  }
  return false;
}

bool KeyBindingRegistry::is_app_reserved(const ftxui::Event& event) const {
  for (const auto& entry : entries_) {
    if (!entry.app_reserved) {
      continue;
    }
    if (matches(entry.action, event)) {
      return true;
    }
  }
  return false;
}

void KeyBindingRegistry::set_override(KeyAction action, std::vector<KeyChordSpec> chords) {
  for (auto& override_entry : overrides_) {
    if (override_entry.action == action) {
      if (chords.empty()) {
        overrides_.erase(std::remove_if(overrides_.begin(), overrides_.end(),
                                        [action](const KeyBindingOverride& o) {
                                          return o.action == action;
                                        }),
                         overrides_.end());
      } else {
        override_entry.chords = std::move(chords);
      }
      return;
    }
  }
  if (!chords.empty()) {
    overrides_.push_back(KeyBindingOverride{action, std::move(chords)});
  }
}

void KeyBindingRegistry::clear_overrides() {
  overrides_.clear();
}

std::vector<KeyBindingConflict> KeyBindingRegistry::find_conflicts() const {
  std::vector<KeyBindingConflict> conflicts;

  struct Seen {
    std::string canonical;
    KeyAction action;
    KeyScope scope;
  };
  std::vector<Seen> seen;

  for (const auto& entry : entries_) {
    const std::vector<KeyChordSpec>* chords = chords_for(entry.action);
    if (chords == nullptr) {
      continue;
    }
    for (const auto& chord : *chords) {
      if (chord.strokes.size() != 1) {
        continue;
      }
      for (const auto& prev : seen) {
        if (prev.canonical != chord.canonical) {
          continue;
        }
        KeyBindingConflict conflict;
        conflict.chord_canonical = chord.canonical;
        conflict.first = prev.action;
        conflict.second = entry.action;
        conflict.first_scope = prev.scope;
        conflict.second_scope = entry.scope;
        conflict.context_dependent = prev.scope != entry.scope;
        conflicts.push_back(conflict);
      }
      seen.push_back(Seen{chord.canonical, entry.action, entry.scope});
    }
  }
  return conflicts;
}

nlohmann::json KeyBindingRegistry::export_overrides() const {
  nlohmann::json doc = nlohmann::json::object();
  doc["version"] = 1;
  nlohmann::json bindings = nlohmann::json::object();
  for (const auto& override_entry : overrides_) {
    nlohmann::json chords = nlohmann::json::array();
    for (const auto& chord : override_entry.chords) {
      nlohmann::json item = nlohmann::json::object();
      item["canonical"] = chord.canonical;
      nlohmann::json inputs = nlohmann::json::array();
      if (!chord.strokes.empty()) {
        for (const auto& input : chord.strokes.front().inputs) {
          inputs.push_back(input);
        }
      }
      item["inputs"] = std::move(inputs);
      chords.push_back(std::move(item));
    }
    bindings[std::string(key_action_id(override_entry.action))] = std::move(chords);
  }
  doc["bindings"] = std::move(bindings);
  return doc;
}

bool KeyBindingRegistry::import_overrides(const nlohmann::json& doc, std::string* error) {
  if (!doc.is_object()) {
    if (error != nullptr) {
      *error = "overrides root must be an object";
    }
    return false;
  }
  if (!doc.contains("bindings")) {
    clear_overrides();
    return true;
  }
  const auto& bindings = doc.at("bindings");
  if (!bindings.is_object()) {
    if (error != nullptr) {
      *error = "bindings must be an object";
    }
    return false;
  }

  std::vector<KeyBindingOverride> loaded;
  for (auto it = bindings.begin(); it != bindings.end(); ++it) {
    const KeyAction action = key_action_from_id(it.key());
    if (action == KeyAction::Count) {
      if (error != nullptr) {
        *error = "unknown action id: " + it.key();
      }
      return false;
    }
    if (!it.value().is_array()) {
      if (error != nullptr) {
        *error = "binding value must be an array";
      }
      return false;
    }

    std::vector<KeyChordSpec> chords;
    for (const auto& item : it.value()) {
      if (item.is_string()) {
        auto parsed = try_parse_canonical_chord(item.get<std::string>());
        if (!parsed.has_value()) {
          if (error != nullptr) {
            *error = "unsupported canonical chord: " + item.get<std::string>();
          }
          return false;
        }
        chords.push_back(std::move(*parsed));
        continue;
      }
      if (!item.is_object() || !item.contains("canonical") || !item["canonical"].is_string()) {
        if (error != nullptr) {
          *error = "chord item must be a string or {canonical,inputs}";
        }
        return false;
      }
      const std::string canonical = item["canonical"].get<std::string>();
      std::vector<std::string> inputs;
      if (item.contains("inputs") && item["inputs"].is_array()) {
        for (const auto& input : item["inputs"]) {
          if (!input.is_string()) {
            if (error != nullptr) {
              *error = "inputs must be strings";
            }
            return false;
          }
          inputs.push_back(input.get<std::string>());
        }
      }
      if (!inputs.empty()) {
        chords.push_back(make_chord_from_inputs(canonical, std::move(inputs)));
      } else {
        auto parsed = try_parse_canonical_chord(canonical);
        if (!parsed.has_value()) {
          if (error != nullptr) {
            *error = "chord missing inputs and unsupported canonical: " + canonical;
          }
          return false;
        }
        chords.push_back(std::move(*parsed));
      }
    }
    if (!chords.empty()) {
      loaded.push_back(KeyBindingOverride{action, std::move(chords)});
    }
  }

  clear_overrides();
  overrides_ = std::move(loaded);
  return true;
}

KeyBindingRegistry& keybind_registry() {
  static KeyBindingRegistry registry = KeyBindingRegistry::with_defaults();
  return registry;
}

bool keybind_matches(KeyAction action, const ftxui::Event& event) {
  return keybind_registry().matches(action, event);
}

std::optional<KeyAction> keybind_match(const ftxui::Event& event, KeyScope scope) {
  return keybind_registry().match(event, scope);
}

}  // namespace tuide
