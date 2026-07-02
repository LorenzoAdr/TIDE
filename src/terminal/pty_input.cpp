#include "terminal/pty_input.hpp"

#include "ui/key_bindings.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

bool looks_like_terminal_mouse_report(const std::string& bytes) {
  if (bytes.size() < 4) {
    return false;
  }
  std::size_t i = 0;
  if (bytes[i] == '\x1b') {
    ++i;
  }
  if (i + 2 >= bytes.size() || bytes[i] != '[' || bytes[i + 1] != '<') {
    return false;
  }
  const char last = bytes.back();
  return last == 'M' || last == 'm';
}

}  // namespace

std::optional<std::string> event_to_pty_bytes(const Event& event) {
  if (event.is_mouse()) {
    return std::nullopt;
  }
  const std::string raw = event.input();
  if (!raw.empty() && looks_like_terminal_mouse_report(raw)) {
    return std::nullopt;
  }
  if (event == Event::Return) {
    return std::string("\r");
  }
  if (event == Event::Backspace) {
    return std::string("\x7f");
  }
  if (event == Event::Delete) {
    return std::string("\x1b[3~");
  }
  if (event == Event::Tab) {
    return std::string("\t");
  }
  if (event == Event::TabReverse) {
    return std::string("\x1b[Z");
  }
  if (event == Event::Escape) {
    return std::string("\x1b");
  }
  if (event == Event::ArrowLeft) {
    return std::string("\x1b[D");
  }
  if (event == Event::ArrowRight) {
    return std::string("\x1b[C");
  }
  if (event == Event::ArrowUp) {
    return std::string("\x1b[A");
  }
  if (event == Event::ArrowDown) {
    return std::string("\x1b[B");
  }
  if (event == Event::Home) {
    return std::string("\x1b[H");
  }
  if (event == Event::End) {
    return std::string("\x1b[F");
  }
  if (event == Event::PageUp) {
    return std::string("\x1b[5~");
  }
  if (event == Event::PageDown) {
    return std::string("\x1b[6~");
  }
  if (event == Event::Insert) {
    return std::string("\x1b[2~");
  }
  if (event == Event::CtrlA) {
    return std::string("\x01");
  }
  if (event == Event::CtrlB) {
    return std::string("\x02");
  }
  if (event_is_ctrl_c(event)) {
    return std::string("\x03");
  }
  if (event == Event::CtrlD) {
    return std::string("\x04");
  }
  if (event == Event::CtrlE) {
    return std::string("\x05");
  }
  if (event == Event::CtrlF) {
    return std::string("\x06");
  }
  if (event == Event::CtrlG) {
    return std::string("\x07");
  }
  if (event == Event::CtrlH) {
    return std::string("\x08");
  }
  if (event == Event::CtrlI) {
    return std::string("\t");
  }
  if (event == Event::CtrlJ) {
    return std::string("\n");
  }
  if (event == Event::CtrlK) {
    return std::string("\x0b");
  }
  if (event == Event::CtrlL) {
    return std::string("\x0c");
  }
  if (event == Event::CtrlM) {
    return std::string("\r");
  }
  if (event == Event::CtrlN) {
    return std::string("\x0e");
  }
  if (event == Event::CtrlO) {
    return std::string("\x0f");
  }
  if (event == Event::CtrlP) {
    return std::string("\x10");
  }
  if (event == Event::CtrlQ) {
    return std::string("\x11");
  }
  if (event == Event::CtrlR) {
    return std::string("\x12");
  }
  if (event == Event::CtrlS) {
    return std::string("\x13");
  }
  if (event == Event::CtrlT) {
    return std::string("\x14");
  }
  if (event == Event::CtrlU) {
    return std::string("\x15");
  }
  if (event == Event::CtrlV) {
    return std::string("\x16");
  }
  if (event == Event::CtrlW) {
    return std::string("\x17");
  }
  if (event == Event::CtrlX) {
    return std::string("\x18");
  }
  if (event == Event::CtrlY) {
    return std::string("\x19");
  }
  if (event == Event::CtrlZ) {
    return std::string("\x1a");
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (!ch.empty()) {
      if (looks_like_terminal_mouse_report(ch)) {
        return std::nullopt;
      }
      return ch;
    }
  }
  return std::nullopt;
}

}  // namespace tgdb
