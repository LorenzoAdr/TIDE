#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ui/keybind/key_binding_registry.hpp"

namespace tuide {

// Human-readable label for a recorded / stored chord stroke.
std::string describe_key_event(const ftxui::Event& event);

// Build a single-stroke chord from a live terminal event (recording).
KeyChordSpec make_chord_from_event(const ftxui::Event& event);

// Rebuild a chord from persisted label + raw terminal inputs.
KeyChordSpec make_chord_from_inputs(std::string canonical,
                                    std::vector<std::string> inputs);

// Best-effort parse of simple canonical forms ("ctrl+p", "f10", "alt+left").
// Returns nullopt when the form is unknown (prefer inputs-based chords).
std::optional<KeyChordSpec> try_parse_canonical_chord(const std::string& canonical);

// Effective chord list for display / export (override if present, else defaults).
std::vector<KeyChordSpec> effective_chords(const KeyBindingRegistry& registry,
                                           KeyAction action);

std::string format_chords_label(const std::vector<KeyChordSpec>& chords);

}  // namespace tuide
