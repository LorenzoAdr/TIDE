#include "ui/keybind/key_chord_util.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "ui/key_bindings.hpp"

namespace tuide {
namespace {

std::string to_lower_copy(std::string s) {
  for (char& ch : s) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return s;
}

KeyEventMatcher match_any_input(std::vector<std::string> inputs) {
  return [inputs = std::move(inputs)](const ftxui::Event& event) {
    const std::string& in = event.input();
    return std::find(inputs.begin(), inputs.end(), in) != inputs.end();
  };
}

}  // namespace

std::string describe_key_event(const ftxui::Event& event) {
  if (event == ftxui::Event::F1) {
    return "F1";
  }
  if (event == ftxui::Event::F2) {
    return "F2";
  }
  if (event == ftxui::Event::F3) {
    return "F3";
  }
  if (event == ftxui::Event::F4) {
    return "F4";
  }
  if (event == ftxui::Event::F5) {
    return "F5";
  }
  if (event == ftxui::Event::F6) {
    return "F6";
  }
  if (event == ftxui::Event::F7) {
    return "F7";
  }
  if (event == ftxui::Event::F8) {
    return "F8";
  }
  if (event == ftxui::Event::F9) {
    return "F9";
  }
  if (event == ftxui::Event::F10) {
    return "F10";
  }
  if (event == ftxui::Event::F11) {
    return "F11";
  }
  if (event == ftxui::Event::F12) {
    return "F12";
  }
  if (event == ftxui::Event::CtrlP || event_is_ctrl_p(event)) {
    return "Ctrl+P";
  }
  if (event == ftxui::Event::CtrlO || event_is_ctrl_o(event)) {
    return "Ctrl+O";
  }
  if (event == ftxui::Event::CtrlQ) {
    return "Ctrl+Q";
  }
  if (event == ftxui::Event::CtrlT) {
    return "Ctrl+T";
  }
  if (event == ftxui::Event::CtrlJ) {
    return "Ctrl+J";
  }
  if (event == ftxui::Event::CtrlA) {
    return "Ctrl+A";
  }
  if (event == ftxui::Event::CtrlE) {
    return "Ctrl+E";
  }
  if (event == ftxui::Event::CtrlS) {
    return "Ctrl+S";
  }
  if (event == ftxui::Event::CtrlB) {
    return "Ctrl+B";
  }
  if (event_is_ctrl_f(event)) {
    return "Ctrl+F";
  }
  if (event_is_ctrl_g(event)) {
    return "Ctrl+G";
  }
  if (event_is_ctrl_z(event)) {
    return "Ctrl+Z";
  }
  if (event_is_ctrl_y(event)) {
    return "Ctrl+Y";
  }
  if (event_is_ctrl_c(event)) {
    return "Ctrl+C";
  }
  if (event_is_ctrl_x(event)) {
    return "Ctrl+X";
  }
  if (event_is_ctrl_v(event)) {
    return "Ctrl+V";
  }
  if (event_is_ctrl_d(event)) {
    return "Ctrl+D";
  }
  if (event_is_ctrl_u(event)) {
    return "Ctrl+U";
  }
  if (event_is_ctrl_i(event)) {
    return "Ctrl+I";
  }
  if (event_is_ctrl_k(event)) {
    return "Ctrl+K";
  }
  if (event_is_ctrl_h(event)) {
    return "Ctrl+H";
  }
  if (event_is_ctrl_space(event)) {
    return "Ctrl+Space";
  }
  if (event_is_ctrl_alt_period(event)) {
    return "Ctrl+Alt+.";
  }
  if (event_is_ctrl_alt_slash(event)) {
    return "Ctrl+Alt+/";
  }
  if (event_is_ctrl_alt_e(event)) {
    return "Ctrl+Alt+E";
  }
  if (event_is_ctrl_alt_a(event)) {
    return "Ctrl+Alt+A";
  }
  if (event_is_ctrl_alt_f(event)) {
    return "Ctrl+Alt+F";
  }
  if (event_is_ctrl_shift_f(event)) {
    return "Ctrl+Shift+F";
  }
  if (event_is_ctrl_shift_s(event)) {
    return "Ctrl+Shift+S";
  }
  if (event_is_ctrl_alt_z(event)) {
    return "Ctrl+Alt+Z";
  }
  if (event_is_ctrl_shift_z(event)) {
    return "Ctrl+Shift+Z";
  }
  if (event_is_alt_left(event)) {
    return "Alt+Left";
  }
  if (event_is_alt_right(event)) {
    return "Alt+Right";
  }
  if (event_is_alt_up(event)) {
    return "Alt+Up";
  }
  if (event_is_alt_down(event)) {
    return "Alt+Down";
  }
  if (event_is_plain_tab(event) || event == ftxui::Event::Tab) {
    return "Tab";
  }
  if (event == ftxui::Event::TabReverse) {
    return "Shift+Tab";
  }
  if (event == ftxui::Event::Insert) {
    return "Insert";
  }
  if (event_is_open_shortcuts_modal(event)) {
    return "Alt+F1 / Shift+F1";
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (ch.size() == 1) {
      const unsigned char c = static_cast<unsigned char>(ch[0]);
      if (c >= 1 && c <= 26) {
        return std::string("Ctrl+") + static_cast<char>('A' + c - 1);
      }
      if (c >= 32 && c < 127) {
        return ch;
      }
    }
  }

  const std::string& input = event.input();
  if (input.empty()) {
    return "?";
  }
  std::ostringstream out;
  out << "key:";
  for (unsigned char c : input) {
    if (c >= 32 && c < 127 && c != '\\') {
      out << static_cast<char>(c);
    } else {
      out << "\\x" << std::hex << static_cast<int>(c);
    }
  }
  return out.str();
}

KeyChordSpec make_chord_from_event(const ftxui::Event& event) {
  std::vector<std::string> inputs;
  if (!event.input().empty()) {
    inputs.push_back(event.input());
  }
  return make_chord_from_inputs(describe_key_event(event), std::move(inputs));
}

KeyChordSpec make_chord_from_inputs(std::string canonical, std::vector<std::string> inputs) {
  KeyChordSpec chord;
  chord.canonical = std::move(canonical);
  KeyStrokeSpec stroke;
  stroke.canonical = chord.canonical;
  stroke.inputs = inputs;
  stroke.matches = match_any_input(std::move(inputs));
  chord.strokes.push_back(std::move(stroke));
  return chord;
}

std::optional<KeyChordSpec> try_parse_canonical_chord(const std::string& canonical) {
  const std::string key = to_lower_copy(canonical);
  auto wrap = [&](KeyEventMatcher matcher) {
    KeyChordSpec chord;
    chord.canonical = canonical;
    KeyStrokeSpec stroke;
    stroke.canonical = canonical;
    stroke.matches = std::move(matcher);
    chord.strokes.push_back(std::move(stroke));
    return chord;
  };

  if (key == "f1") {
    return wrap([](const ftxui::Event& e) { return event_is_f1(e); });
  }
  if (key == "f2") {
    return wrap([](const ftxui::Event& e) { return e == ftxui::Event::F2; });
  }
  if (key == "f3") {
    return wrap([](const ftxui::Event& e) { return e == ftxui::Event::F3; });
  }
  if (key == "f4") {
    return wrap([](const ftxui::Event& e) { return e == ftxui::Event::F4; });
  }
  if (key == "f5") {
    return wrap([](const ftxui::Event& e) { return e == ftxui::Event::F5; });
  }
  if (key == "f6") {
    return wrap([](const ftxui::Event& e) { return e == ftxui::Event::F6; });
  }
  if (key == "f7") {
    return wrap([](const ftxui::Event& e) { return e == ftxui::Event::F7; });
  }
  if (key == "f8") {
    return wrap([](const ftxui::Event& e) { return e == ftxui::Event::F8; });
  }
  if (key == "f9") {
    return wrap([](const ftxui::Event& e) { return e == ftxui::Event::F9; });
  }
  if (key == "f10") {
    return wrap([](const ftxui::Event& e) { return e == ftxui::Event::F10; });
  }
  if (key == "f11") {
    return wrap([](const ftxui::Event& e) { return e == ftxui::Event::F11; });
  }
  if (key == "f12") {
    return wrap([](const ftxui::Event& e) { return event_is_go_to_definition(e); });
  }
  if (key == "ctrl+p") {
    return wrap([](const ftxui::Event& e) { return event_is_ctrl_p(e); });
  }
  if (key == "ctrl+o") {
    return wrap([](const ftxui::Event& e) { return event_is_ctrl_o(e); });
  }
  if (key == "ctrl+q") {
    return wrap([](const ftxui::Event& e) { return e == ftxui::Event::CtrlQ; });
  }
  if (key == "ctrl+t") {
    return wrap([](const ftxui::Event& e) { return e == ftxui::Event::CtrlT; });
  }
  if (key == "ctrl+a") {
    return wrap([](const ftxui::Event& e) { return e == ftxui::Event::CtrlA; });
  }
  if (key == "ctrl+alt+a") {
    return wrap([](const ftxui::Event& e) { return event_is_ctrl_alt_a(e); });
  }
  if (key == "ctrl+e") {
    return wrap([](const ftxui::Event& e) { return e == ftxui::Event::CtrlE; });
  }
  if (key == "ctrl+s") {
    return wrap([](const ftxui::Event& e) { return e == ftxui::Event::CtrlS; });
  }
  if (key == "ctrl+b") {
    return wrap([](const ftxui::Event& e) { return e == ftxui::Event::CtrlB; });
  }
  if (key == "ctrl+f") {
    return wrap([](const ftxui::Event& e) { return event_is_ctrl_f(e); });
  }
  if (key == "ctrl+g") {
    return wrap([](const ftxui::Event& e) { return event_is_ctrl_g(e); });
  }
  if (key == "ctrl+z") {
    return wrap([](const ftxui::Event& e) {
      return event_is_ctrl_z(e) && !event_input_has_shift_modifier(e);
    });
  }
  if (key == "ctrl+space") {
    return wrap([](const ftxui::Event& e) { return event_is_ctrl_space(e); });
  }
  if (key == "alt+left") {
    return wrap([](const ftxui::Event& e) { return event_is_alt_left(e); });
  }
  if (key == "alt+right") {
    return wrap([](const ftxui::Event& e) { return event_is_alt_right(e); });
  }
  if (key == "alt+up") {
    return wrap([](const ftxui::Event& e) { return event_is_alt_up(e); });
  }
  if (key == "alt+down") {
    return wrap([](const ftxui::Event& e) { return event_is_alt_down(e); });
  }
  if (key == "tab") {
    return wrap([](const ftxui::Event& e) { return event_is_plain_tab(e); });
  }
  if (key == "shift+tab") {
    return wrap([](const ftxui::Event& e) { return e == ftxui::Event::TabReverse; });
  }
  return std::nullopt;
}

std::vector<KeyChordSpec> effective_chords(const KeyBindingRegistry& registry, KeyAction action) {
  return registry.effective_chords(action);
}

std::string format_chords_label(const std::vector<KeyChordSpec>& chords) {
  if (chords.empty()) {
    return "-";
  }
  std::ostringstream out;
  for (std::size_t i = 0; i < chords.size(); ++i) {
    if (i > 0) {
      out << " | ";
    }
    out << chords[i].canonical;
  }
  return out.str();
}

}  // namespace tuide
