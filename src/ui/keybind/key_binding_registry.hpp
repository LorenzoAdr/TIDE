#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include "ftxui/component/event.hpp"
#include "ui/keybind/key_action.hpp"

namespace tuide {

using KeyEventMatcher = std::function<bool(const ftxui::Event&)>;

struct KeyStrokeSpec {
  // Human/canonical form for future JSON overrides (phase 2), e.g. "ctrl+p".
  std::string canonical;
  KeyEventMatcher matches;
};

struct KeyChordSpec {
  // Display / persistence form, e.g. "ctrl+k ctrl+c" or "ctrl+p".
  std::string canonical;
  std::vector<KeyStrokeSpec> strokes;
};

struct KeyBindingEntry {
  KeyAction action = KeyAction::Count;
  KeyScope scope = KeyScope::Global;
  bool remappable = true;
  // Panels must not consume these (Ctrl+P, Ctrl+Q, …).
  bool global_reserved = false;
  // Helix / prefix pending must not consume these.
  bool app_reserved = false;
  std::vector<KeyChordSpec> chords;
};

struct KeyBindingConflict {
  std::string chord_canonical;
  KeyAction first = KeyAction::Count;
  KeyAction second = KeyAction::Count;
  KeyScope first_scope = KeyScope::Global;
  KeyScope second_scope = KeyScope::Global;
  bool context_dependent = false;
};

class KeyBindingRegistry {
 public:
  static KeyBindingRegistry with_defaults();

  const std::vector<KeyBindingEntry>& entries() const { return entries_; }

  bool matches(KeyAction action, const ftxui::Event& event) const;
  std::optional<KeyAction> match(const ftxui::Event& event, KeyScope scope) const;

  bool is_global_reserved(const ftxui::Event& event) const;
  bool is_app_reserved(const ftxui::Event& event) const;

  const KeyBindingEntry* find(KeyAction action) const;

  // Phase 2 hooks: override chord list for an action (empty = restore default).
  void set_override(KeyAction action, std::vector<KeyChordSpec> chords);
  void clear_overrides();
  bool has_overrides() const { return !overrides_.empty(); }

  std::vector<KeyBindingConflict> find_conflicts() const;

  // Persistence stubs for phase 2 (bindings.json / settings section).
  nlohmann::json export_overrides() const;
  bool import_overrides(const nlohmann::json& doc, std::string* error);

 private:
  struct Override {
    KeyAction action = KeyAction::Count;
    std::vector<KeyChordSpec> chords;
  };

  const KeyBindingEntry* effective_entry(KeyAction action) const;
  bool chord_matches_single(const KeyChordSpec& chord, const ftxui::Event& event) const;

  std::vector<KeyBindingEntry> entries_;
  std::vector<Override> overrides_;
};

// Process-wide registry (defaults until phase 2 loads overrides).
KeyBindingRegistry& keybind_registry();

bool keybind_matches(KeyAction action, const ftxui::Event& event);
std::optional<KeyAction> keybind_match(const ftxui::Event& event, KeyScope scope);

}  // namespace tuide
