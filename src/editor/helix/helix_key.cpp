#include "editor/helix/helix_key.hpp"

#include "ui/key_bindings.hpp"

namespace tgdb {

namespace {

std::string letter_token(char ch, bool shift) {
  if (shift) {
    if (ch >= 'a' && ch <= 'z') {
      ch = static_cast<char>(ch - 'a' + 'A');
    }
    return std::string(1, ch);
  }
  if (ch >= 'A' && ch <= 'Z') {
    return std::string(1, static_cast<char>(ch - 'A' + 'a'));
  }
  return std::string(1, ch);
}

}  // namespace

std::optional<std::string> helix_key_token(const ftxui::Event& event) {
  if (event == ftxui::Event::Escape) {
    return "<esc>";
  }
  if (event == ftxui::Event::Return) {
    return "<ret>";
  }
  if (event == ftxui::Event::Backspace) {
    return "<backspace>";
  }
  if (event == ftxui::Event::Tab) {
    return "<tab>";
  }
  if (event == ftxui::Event::TabReverse) {
    return "<s-tab>";
  }
  if (event == ftxui::Event::Delete) {
    return "<del>";
  }
  if (event == ftxui::Event::PageUp) {
    return "<pageup>";
  }
  if (event == ftxui::Event::PageDown) {
    return "<pagedown>";
  }
  if (event == ftxui::Event::Home) {
    return "<home>";
  }
  if (event == ftxui::Event::End) {
    return "<end>";
  }
  if (event == ftxui::Event::ArrowLeft) {
    return "<left>";
  }
  if (event == ftxui::Event::ArrowRight) {
    return "<right>";
  }
  if (event == ftxui::Event::ArrowUp) {
    return "<up>";
  }
  if (event == ftxui::Event::ArrowDown) {
    return "<down>";
  }

  const bool shift = event_input_has_shift_modifier(event);

  if (event == ftxui::Event::Character(' ')) {
    return "<space>";
  }

  char printable = '\0';
  if (helix_event_is_printable(event, &printable)) {
    return letter_token(printable, shift);
  }

  return std::nullopt;
}

bool helix_event_is_printable(const ftxui::Event& event, char* out_char) {
  if (out_char == nullptr) {
    return false;
  }
  if (event.is_character()) {
    const char ch = event.character()[0];
    if (ch >= 32 && ch != 127) {
      *out_char = ch;
      return true;
    }
  }
  return false;
}

}  // namespace tgdb
