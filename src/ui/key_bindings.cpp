#include "ui/key_bindings.hpp"

#include <string>

namespace tgdb {

namespace {

bool csi_key(const ftxui::Event& event, int modifier, int keycode) {
  const std::string& input = event.input();
  const std::string legacy =
      std::string("\x1B[27;") + std::to_string(modifier) + ";" + std::to_string(keycode) + "~";
  const std::string kitty =
      std::string("\x1B[") + std::to_string(keycode) + ";" + std::to_string(modifier) + "u";
  return input == legacy || input == kitty;
}

bool csi_key_any_modifier(const ftxui::Event& event,
                            const int* modifiers, std::size_t modifier_count, int keycode) {
  for (std::size_t i = 0; i < modifier_count; ++i) {
    if (csi_key(event, modifiers[i], keycode)) {
      return true;
    }
  }
  return false;
}

bool alt_letter(const ftxui::Event& event, char lower, char upper) {
  const std::string& input = event.input();
  return input.size() == 2 && input[0] == '\x1B' &&
         (input[1] == lower || input[1] == upper);
}

}  // namespace

bool event_is_ctrl_shift_l(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[27;6;76~") ||
         event == ftxui::Event::Special("\x1B[27;6;108~") ||
         event == ftxui::Event::Special("\x1B[27;5;76~");
}

bool event_is_shift_left(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;2D") ||
         event == ftxui::Event::Special("\x1B[1;3D") ||
         event == ftxui::Event::Special("\x1B[1;4D");
}

bool event_is_shift_right(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;2C") ||
         event == ftxui::Event::Special("\x1B[1;3C") ||
         event == ftxui::Event::Special("\x1B[1;4C");
}

bool event_is_shift_up(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;2A") ||
         event == ftxui::Event::Special("\x1B[1;3A") ||
         event == ftxui::Event::Special("\x1B[1;4A");
}

bool event_is_shift_down(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;2B") ||
         event == ftxui::Event::Special("\x1B[1;3B") ||
         event == ftxui::Event::Special("\x1B[1;4B");
}

bool event_is_ctrl_shift_up(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;6A");
}

bool event_is_ctrl_shift_down(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;6B");
}

bool event_is_ctrl_shift_o(const ftxui::Event& event) {
  const int mods[] = {6};
  return csi_key_any_modifier(event, mods, 1, 79) ||
         csi_key_any_modifier(event, mods, 1, 111) ||
         event == ftxui::Event::Special("\x1B[27;6;79~") ||
         event == ftxui::Event::Special("\x1B[27;6;111~") ||
         event == ftxui::Event::Special("\x1B[79;6u") ||
         event == ftxui::Event::Special("\x1B[111;6u");
}

bool event_is_ctrl_h_csi(const ftxui::Event& event) {
  const int mods[] = {5, 6};
  return csi_key_any_modifier(event, mods, 2, 72) ||
         csi_key_any_modifier(event, mods, 2, 104) ||
         event == ftxui::Event::Special("\x1B[27;5;72~") ||
         event == ftxui::Event::Special("\x1B[27;5;104~") ||
         event == ftxui::Event::Special("\x1B[27;6;72~") ||
         event == ftxui::Event::Special("\x1B[27;6;104~") ||
         event == ftxui::Event::Special("\x1B[72;5u") ||
         event == ftxui::Event::Special("\x1B[104;5u") ||
         event == ftxui::Event::Special("\x1B[72;6u") ||
         event == ftxui::Event::Special("\x1B[104;6u");
}

bool event_is_alt_h(const ftxui::Event& event) {
  return event == ftxui::Event::AltH || alt_letter(event, 'h', 'H') ||
         event == ftxui::Event::Special("\x1Bh") ||
         event == ftxui::Event::Special("\x1B[104;3u") ||
         event == ftxui::Event::Special("\x1B[72;3u");
}

bool event_is_alt_o(const ftxui::Event& event) {
  return event == ftxui::Event::AltO || alt_letter(event, 'o', 'O') ||
         event == ftxui::Event::Special("\x1Bo") ||
         event == ftxui::Event::Special("\x1B[111;3u") ||
         event == ftxui::Event::Special("\x1B[79;3u");
}

bool event_is_ctrl_shift_h(const ftxui::Event& event) {
  const int mods[] = {6};
  return csi_key_any_modifier(event, mods, 1, 72) ||
         csi_key_any_modifier(event, mods, 1, 104) ||
         event == ftxui::Event::Special("\x1B[27;6;72~") ||
         event == ftxui::Event::Special("\x1B[27;6;104~") ||
         event == ftxui::Event::Special("\x1B[72;6u") ||
         event == ftxui::Event::Special("\x1B[104;6u");
}

bool event_is_open_search_panel(const ftxui::Event& event) {
  return event == ftxui::Event::F7 || event_is_ctrl_h_csi(event) ||
         event_is_ctrl_shift_h(event) || event_is_alt_h(event);
}

bool event_is_open_outline_panel(const ftxui::Event& event) {
  return event == ftxui::Event::F8 || event_is_ctrl_shift_o(event) ||
         event_is_alt_o(event);
}

bool event_is_ctrl_left(const ftxui::Event& event) {
  return event == ftxui::Event::ArrowLeftCtrl;
}

bool event_is_ctrl_right(const ftxui::Event& event) {
  return event == ftxui::Event::ArrowRightCtrl;
}

bool event_is_ctrl_shift_left(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;6D");
}

bool event_is_ctrl_shift_right(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;6C");
}

bool event_is_ctrl_c(const ftxui::Event& event) {
  return event == ftxui::Event::CtrlC ||
         event == ftxui::Event::Special("\x1B[27;5;99~") ||
         event == ftxui::Event::Special("\x1B[27;5;67~");
}

bool event_is_ctrl_z(const ftxui::Event& event) {
  return event == ftxui::Event::CtrlZ ||
         event == ftxui::Event::Special("\x1B[27;5;122~") ||
         event == ftxui::Event::Special("\x1B[27;5;90~");
}

bool event_is_ctrl_v(const ftxui::Event& event) {
  return event == ftxui::Event::CtrlV ||
         event == ftxui::Event::Special("\x1B[27;5;118~") ||
         event == ftxui::Event::Special("\x1B[27;5;86~");
}

bool event_is_ctrl_h(const ftxui::Event& event) {
  // Raw Ctrl+H (0x08) is indistinguishable from Backspace once FTXUI normalizes it.
  // Use event_is_ctrl_h_csi() for Ctrl+H when extended key reporting is active.
  return event_is_ctrl_h_csi(event);
}

bool event_is_ctrl_f(const ftxui::Event& event) {
  return event == ftxui::Event::CtrlF ||
         event == ftxui::Event::Special("\x1B[27;5;102~") ||
         event == ftxui::Event::Special("\x1B[27;5;70~");
}

bool event_is_ctrl_g(const ftxui::Event& event) {
  return event == ftxui::Event::CtrlG ||
         event == ftxui::Event::Special("\x1B[27;5;103~") ||
         event == ftxui::Event::Special("\x1B[27;5;71~");
}

bool event_is_ctrl_u(const ftxui::Event& event) {
  return event == ftxui::Event::CtrlU ||
         event == ftxui::Event::Special("\x1B[27;5;117~") ||
         event == ftxui::Event::Special("\x1B[27;5;85~");
}

bool event_is_plain_tab(const ftxui::Event& event) {
  return event == ftxui::Event::Tab || event == ftxui::Event::CtrlI;
}

bool event_is_ctrl_i(const ftxui::Event& event) {
  // Plain Tab/CtrlI (\t) is handled separately using keyboard_control state.
  if (event_is_plain_tab(event)) {
    return false;
  }
  const int mods[] = {5};
  return csi_key_any_modifier(event, mods, 1, 9) ||
         csi_key_any_modifier(event, mods, 1, 105) ||
         csi_key_any_modifier(event, mods, 1, 73) ||
         event == ftxui::Event::Special("\x1B[27;5;105~") ||
         event == ftxui::Event::Special("\x1B[27;5;73~") ||
         event == ftxui::Event::Special("\x1B[9;5u") ||
         event == ftxui::Event::Special("\x1B[105;5u") ||
         event == ftxui::Event::Special("\x1B[73;5u");
}

bool event_is_shift_key_press(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[57417u") ||
         event == ftxui::Event::Special("\x1B[57417;1u") ||
         event == ftxui::Event::Special("\x1B[57418u") ||
         event == ftxui::Event::Special("\x1B[57418;1u");
}

bool event_is_shift_key_release(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[57417;2u") ||
         event == ftxui::Event::Special("\x1B[57418;2u");
}

bool event_is_ctrl_key_press(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[57442u") ||
         event == ftxui::Event::Special("\x1B[57442;1u") ||
         event == ftxui::Event::Special("\x1B[57443u") ||
         event == ftxui::Event::Special("\x1B[57443;1u");
}

bool event_is_ctrl_key_release(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[57442;2u") ||
         event == ftxui::Event::Special("\x1B[57443;2u");
}

bool event_is_ctrl_d(const ftxui::Event& event) {
  return event == ftxui::Event::CtrlD ||
         event == ftxui::Event::Special("\x1B[27;5;100~") ||
         event == ftxui::Event::Special("\x1B[27;5;68~");
}

bool event_is_ctrl_shift_d(const ftxui::Event& event) {
  const int mods[] = {6};
  return csi_key_any_modifier(event, mods, 1, 68) ||
         csi_key_any_modifier(event, mods, 1, 100) ||
         event == ftxui::Event::Special("\x1B[27;6;68~") ||
         event == ftxui::Event::Special("\x1B[27;6;100~") ||
         event == ftxui::Event::Special("\x1B[68;6u") ||
         event == ftxui::Event::Special("\x1B[100;6u");
}

bool event_is_ctrl_backspace(const ftxui::Event& event) {
  // Ctrl+W is the de-facto delete-word-backward in many terminals.
  if (event == ftxui::Event::CtrlW) {
    return true;
  }
  const int mods[] = {5, 6};
  return csi_key_any_modifier(event, mods, 2, 8) ||
         csi_key_any_modifier(event, mods, 2, 127) ||
         event == ftxui::Event::Special("\x1B[27;5;8~") ||
         event == ftxui::Event::Special("\x1B[27;5;127~") ||
         event == ftxui::Event::Special("\x1B[27;6;8~") ||
         event == ftxui::Event::Special("\x1B[27;6;127~") ||
         event == ftxui::Event::Special("\x1B[8;5u") ||
         event == ftxui::Event::Special("\x1B[127;5u") ||
         event == ftxui::Event::Special("\x1B[8;6u") ||
         event == ftxui::Event::Special("\x1B[127;6u");
}

bool event_is_ctrl_delete(const ftxui::Event& event) {
  const int mods[] = {5, 6};
  return csi_key_any_modifier(event, mods, 2, 3) ||
         csi_key_any_modifier(event, mods, 2, 51) ||
         event == ftxui::Event::Special("\x1B[3;5~") ||
         event == ftxui::Event::Special("\x1B[27;5;3~") ||
         event == ftxui::Event::Special("\x1B[27;6;3~") ||
         event == ftxui::Event::Special("\x1B[3;5u") ||
         event == ftxui::Event::Special("\x1B[51;5u") ||
         event == ftxui::Event::Special("\x1B[3;6u") ||
         event == ftxui::Event::Special("\x1B[51;6u");
}

bool event_is_ctrl_space(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x00") ||
         event == ftxui::Event::Special("\x1B[32;5u") ||
         event == ftxui::Event::Special("\x1B[0;5;32~") ||
         event == ftxui::Event::Special("\x1B[27;5;32~") ||
         event == ftxui::Event::Special("\x1B[27;5;32;5~");
}

bool event_is_ctrl_period(const ftxui::Event& event) {
  const std::string& input = event.input();
  return input == "\x1e" || input == "\x1E" ||
         event == ftxui::Event::Special("\x1e") ||
         event == ftxui::Event::Special("\x1E") ||
         event == ftxui::Event::Special("\x1B[46;5u") ||
         event == ftxui::Event::Special("\x1B[27;5;46~") ||
         event == ftxui::Event::Special("\x1B[27;5;190~") ||
         event == ftxui::Event::Special("\x1B[46;5;1~");
}

bool event_is_alt_period(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B.") ||
         event == ftxui::Event::Special("\x1B[46;3u") ||
         event == ftxui::Event::Special("\x1B[27;3;46~");
}

bool event_is_alt_slash(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B/") ||
         event == ftxui::Event::Special("\x1B[47;3u") ||
         event == ftxui::Event::Special("\x1B[47;5u");
}

bool event_is_completion(const ftxui::Event& event) {
  return event == ftxui::Event::F6 || event_is_ctrl_space(event) ||
         event_is_ctrl_period(event) || event_is_alt_period(event) ||
         event_is_alt_slash(event);
}

bool event_is_go_to_definition(const ftxui::Event& event) {
  return event == ftxui::Event::F12 || event == ftxui::Event::Special("\x1B[24~");
}

bool event_is_go_to_declaration(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[24;2~") ||
         event == ftxui::Event::Special("\x1B[24;6~");
}

bool event_has_shift_modifier(const ftxui::Event& event) {
  return event_is_shift_key_press(event) || event_is_shift_left(event) ||
         event_is_shift_right(event) || event_is_shift_up(event) ||
         event_is_shift_down(event) || event_is_ctrl_shift_left(event) ||
         event_is_ctrl_shift_right(event) || event_is_ctrl_shift_up(event) ||
         event_is_ctrl_shift_down(event) || event_is_ctrl_shift_d(event) ||
         event_is_ctrl_shift_l(event) || event_is_ctrl_shift_h(event) ||
         event_is_ctrl_shift_o(event);
}

bool event_has_ctrl_modifier(const ftxui::Event& event) {
  if (event_is_plain_tab(event)) {
    return false;
  }
  return event_is_ctrl_key_press(event) || event_is_ctrl_c(event) ||
         event_is_ctrl_v(event) || event_is_ctrl_z(event) || event_is_ctrl_f(event) ||
         event_is_ctrl_g(event) || event_is_ctrl_u(event) || event_is_ctrl_d(event) ||
         event_is_ctrl_shift_d(event) || event_is_ctrl_backspace(event) ||
         event_is_ctrl_delete(event) || event_is_ctrl_space(event) ||
         event_is_ctrl_left(event) || event_is_ctrl_right(event) ||
         event_is_ctrl_shift_left(event) || event_is_ctrl_shift_right(event) ||
         event_is_ctrl_shift_up(event) || event_is_ctrl_shift_down(event) ||
         event_is_ctrl_shift_l(event) || event_is_ctrl_shift_h(event) ||
         event_is_ctrl_shift_o(event) || event_is_ctrl_h(event) ||
         event == ftxui::Event::CtrlS || event == ftxui::Event::CtrlQ ||
         event == ftxui::Event::CtrlB || event == ftxui::Event::CtrlW ||
         event_is_ctrl_i(event);
}

bool editor_priority_key(const ftxui::Event& event) {
  return event == ftxui::Event::Escape || event_is_ctrl_z(event) || event_is_ctrl_c(event) ||
         event_is_ctrl_v(event) || event_is_ctrl_f(event) || event_is_ctrl_g(event) ||
         event_is_shift_left(event) || event_is_shift_right(event) || event_is_shift_up(event) ||
         event_is_shift_down(event) || event_is_ctrl_left(event) || event_is_ctrl_right(event) ||
         event_is_ctrl_shift_left(event) || event_is_ctrl_shift_right(event) ||
         event_is_ctrl_shift_up(event) || event_is_ctrl_shift_down(event) ||
         event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight ||
         event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown ||
         event == ftxui::Event::Backspace || event == ftxui::Event::Delete ||
         event == ftxui::Event::Return || event == ftxui::Event::Tab ||
         event == ftxui::Event::Home || event == ftxui::Event::End ||
         event == ftxui::Event::PageUp || event == ftxui::Event::PageDown ||
         event_is_ctrl_u(event) || event_is_ctrl_i(event) || event_is_ctrl_d(event) ||
         event_is_ctrl_shift_d(event) || event_is_ctrl_backspace(event) ||
         event_is_ctrl_delete(event) || event == ftxui::Event::CtrlS ||
         event_is_ctrl_shift_l(event) || event_is_completion(event) ||
         event_is_go_to_definition(event) || event_is_go_to_declaration(event);
}

}  // namespace tgdb
