#include "ui/key_bindings.hpp"

#include <optional>
#include <string>

namespace tuide {

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

bool input_has_shift_token(const std::string& input) {
  return input.find("[1;2") != std::string::npos || input.find(";2u") != std::string::npos ||
         input.find(";2~") != std::string::npos || input.find("57417") != std::string::npos ||
         input.find("57418") != std::string::npos;
}

bool input_has_shift_release_token(const std::string& input) {
  return input.find("57417;2u") != std::string::npos ||
         input.find("57418;2u") != std::string::npos;
}

}  // namespace

bool event_is_ctrl_alt_l(const ftxui::Event& event) {
  const int mods[] = {7};
  return event == ftxui::Event::CtrlAltL ||
         csi_key_any_modifier(event, mods, 1, 76) ||
         csi_key_any_modifier(event, mods, 1, 108) ||
         event == ftxui::Event::Special("\x1B[27;7;76~") ||
         event == ftxui::Event::Special("\x1B[27;7;108~") ||
         event == ftxui::Event::Special("\x1B[76;7u") ||
         event == ftxui::Event::Special("\x1B[108;7u");
}

bool event_is_ctrl_shift_l(const ftxui::Event& event) {
  const int mods[] = {6};
  return csi_key_any_modifier(event, mods, 1, 76) ||
         csi_key_any_modifier(event, mods, 1, 108) ||
         event == ftxui::Event::Special("\x1B[27;6;76~") ||
         event == ftxui::Event::Special("\x1B[27;6;108~") ||
         event == ftxui::Event::Special("\x1B[76;6u") ||
         event == ftxui::Event::Special("\x1B[108;6u");
}

bool event_is_alt_f1(const ftxui::Event& event) {
  const int mods[] = {3};
  return csi_key_any_modifier(event, mods, 1, 11) ||
         csi_key_any_modifier(event, mods, 1, 80) ||
         csi_key_any_modifier(event, mods, 1, 112) ||
         event == ftxui::Event::Special("\x1B[1;3P") ||
         event == ftxui::Event::Special("\x1B[11;3~") ||
         event == ftxui::Event::Special("\x1B[11;3u") ||
         event == ftxui::Event::Special("\x1B[80;3u") ||
         event == ftxui::Event::Special("\x1B[112;3u") ||
         event == ftxui::Event::Special("\x1B[27;3;11~") ||
         event == ftxui::Event::Special("\x1B[27;3;112~") ||
         // Konsole (linux.keytab): F1 = ESC [[ A, Alt+F1 = ESC + F1
         event == ftxui::Event::Special("\x1B\x1B[[A") ||
         // xterm SS3 / CSI: Alt+F1 = ESC + F1 sequence
         event == ftxui::Event::Special("\x1B\x1BOP") ||
         event == ftxui::Event::Special("\x1B\x1B[11~");
}

bool event_is_shift_f1(const ftxui::Event& event) {
  const int mods[] = {2};
  return csi_key_any_modifier(event, mods, 1, 11) ||
         csi_key_any_modifier(event, mods, 1, 80) ||
         csi_key_any_modifier(event, mods, 1, 112) ||
         event == ftxui::Event::Special("\x1B[1;2P") ||
         event == ftxui::Event::Special("\x1B[11;2~") ||
         event == ftxui::Event::Special("\x1B[25;2~") ||
         event == ftxui::Event::Special("\x1B[11;2u") ||
         event == ftxui::Event::Special("\x1B[80;2u") ||
         event == ftxui::Event::Special("\x1B[112;2u") ||
         event == ftxui::Event::Special("\x1B[27;2;11~") ||
         event == ftxui::Event::Special("\x1B[27;2;112~");
}

bool event_is_open_shortcuts_modal(const ftxui::Event& event) {
  return event_is_alt_f1(event) || event_is_shift_f1(event);
}

bool event_is_f1(const ftxui::Event& event) {
  if (event_is_open_shortcuts_modal(event)) {
    return false;
  }
  return event == ftxui::Event::F1 || event == ftxui::Event::Special("\x1BOP") ||
         event == ftxui::Event::Special("\x1B[11~") ||
         event == ftxui::Event::Special("\x1B[[A");
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

bool event_is_shift_home(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;2H") ||
         event == ftxui::Event::Special("\x1B[27;2;1~");
}

bool event_is_shift_end(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;2F") ||
         event == ftxui::Event::Special("\x1B[27;2;4~") ||
         event == ftxui::Event::Special("\x1B[1;5~");
}

bool event_is_alt_left(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;3D");
}

bool event_is_alt_right(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;3C");
}

bool event_is_ctrl_alt_up(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;7A");
}

bool event_is_ctrl_alt_down(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;7B");
}

bool event_is_ctrl_shift_up(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;6A");
}

bool event_is_ctrl_shift_down(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;6B");
}

bool event_is_ctrl_alt_o(const ftxui::Event& event) {
  const int mods[] = {7};
  return event == ftxui::Event::CtrlAltO ||
         csi_key_any_modifier(event, mods, 1, 79) ||
         csi_key_any_modifier(event, mods, 1, 111) ||
         event == ftxui::Event::Special("\x1B[27;7;79~") ||
         event == ftxui::Event::Special("\x1B[27;7;111~") ||
         event == ftxui::Event::Special("\x1B[79;7u") ||
         event == ftxui::Event::Special("\x1B[111;7u");
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

bool event_is_alt_s(const ftxui::Event& event) {
  return event == ftxui::Event::AltS || alt_letter(event, 's', 'S') ||
         event == ftxui::Event::Special("\x1Bs") ||
         event == ftxui::Event::Special("\x1B[115;3u") ||
         event == ftxui::Event::Special("\x1B[83;3u");
}

bool event_is_ctrl_alt_h(const ftxui::Event& event) {
  const int mods[] = {7};
  return event == ftxui::Event::CtrlAltH ||
         csi_key_any_modifier(event, mods, 1, 72) ||
         csi_key_any_modifier(event, mods, 1, 104) ||
         event == ftxui::Event::Special("\x1B[27;7;72~") ||
         event == ftxui::Event::Special("\x1B[27;7;104~") ||
         event == ftxui::Event::Special("\x1B[72;7u") ||
         event == ftxui::Event::Special("\x1B[104;7u");
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
         event_is_ctrl_alt_h(event) || event_is_ctrl_shift_h(event);
}

bool event_is_open_outline_panel(const ftxui::Event& event) {
  return event == ftxui::Event::F8 || event_is_ctrl_alt_o(event) ||
         event_is_ctrl_shift_o(event);
}

bool event_is_open_binary_symbols_panel(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[83;6u") ||
         event == ftxui::Event::Special("\x1B[115;6u") ||
         event == ftxui::Event::Special("\x1B[27;6;83~") ||
         event == ftxui::Event::Special("\x1B[27;6;115~");
}

bool event_is_ctrl_left(const ftxui::Event& event) {
  return event == ftxui::Event::ArrowLeftCtrl;
}

bool event_is_ctrl_right(const ftxui::Event& event) {
  return event == ftxui::Event::ArrowRightCtrl;
}

bool event_is_ctrl_alt_left(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;7D");
}

bool event_is_ctrl_alt_right(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[1;7C");
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

bool event_is_ctrl_alt_z(const ftxui::Event& event) {
  const int mods[] = {7};
  return event == ftxui::Event::CtrlAltZ ||
         csi_key_any_modifier(event, mods, 1, 122) ||
         csi_key_any_modifier(event, mods, 1, 90) ||
         event == ftxui::Event::Special("\x1B[27;7;122~") ||
         event == ftxui::Event::Special("\x1B[27;7;90~") ||
         event == ftxui::Event::Special("\x1B[122;7u") ||
         event == ftxui::Event::Special("\x1B[90;7u");
}

bool event_is_ctrl_shift_z(const ftxui::Event& event) {
  const int mods[] = {6};
  return (event == ftxui::Event::CtrlZ && event_input_has_shift_modifier(event)) ||
         csi_key_any_modifier(event, mods, 1, 122) ||
         csi_key_any_modifier(event, mods, 1, 90) ||
         event == ftxui::Event::Special("\x1B[27;6;122~") ||
         event == ftxui::Event::Special("\x1B[27;6;90~") ||
         event == ftxui::Event::Special("\x1B[122;6u") ||
         event == ftxui::Event::Special("\x1B[90;6u");
}

bool event_is_ctrl_y(const ftxui::Event& event) {
  return event == ftxui::Event::CtrlY ||
         event == ftxui::Event::Special("\x1B[27;5;121~") ||
         event == ftxui::Event::Special("\x1B[27;5;89~");
}

bool event_is_ctrl_v(const ftxui::Event& event) {
  return event == ftxui::Event::CtrlV ||
         event == ftxui::Event::Special("\x1B[27;5;118~") ||
         event == ftxui::Event::Special("\x1B[27;5;86~");
}

bool event_is_ctrl_x(const ftxui::Event& event) {
  return event == ftxui::Event::CtrlX ||
         event == ftxui::Event::Special("\x1B[27;5;120~") ||
         event == ftxui::Event::Special("\x1B[27;5;88~");
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

bool event_is_ctrl_alt_f(const ftxui::Event& event) {
  const int mods[] = {7};
  return event == ftxui::Event::CtrlAltF ||
         csi_key_any_modifier(event, mods, 1, 70) ||
         csi_key_any_modifier(event, mods, 1, 102) ||
         event == ftxui::Event::Special("\x1B[27;7;70~") ||
         event == ftxui::Event::Special("\x1B[27;7;102~") ||
         event == ftxui::Event::Special("\x1B[70;7u") ||
         event == ftxui::Event::Special("\x1B[104;7u");
}

bool event_is_ctrl_shift_f(const ftxui::Event& event) {
  const int mods[] = {6};
  return (event == ftxui::Event::CtrlF && event_input_has_shift_modifier(event)) ||
         csi_key_any_modifier(event, mods, 1, 70) ||
         csi_key_any_modifier(event, mods, 1, 102) ||
         event == ftxui::Event::Special("\x1B[27;6;70~") ||
         event == ftxui::Event::Special("\x1B[27;6;102~") ||
         event == ftxui::Event::Special("\x1B[70;6u") ||
         event == ftxui::Event::Special("\x1B[104;6u");
}

bool event_is_ctrl_shift_s(const ftxui::Event& event) {
  const int mods[] = {6};
  return (event == ftxui::Event::CtrlS && event_input_has_shift_modifier(event)) ||
         csi_key_any_modifier(event, mods, 1, 83) ||
         csi_key_any_modifier(event, mods, 1, 115) ||
         event == ftxui::Event::Special("\x1B[27;6;83~") ||
         event == ftxui::Event::Special("\x1B[27;6;115~") ||
         event == ftxui::Event::Special("\x1B[83;6u") ||
         event == ftxui::Event::Special("\x1B[115;6u");
}

bool event_is_workspace_search_with_selection(const ftxui::Event& event) {
  return event_is_ctrl_alt_f(event) || event_is_ctrl_shift_f(event);
}

bool event_is_ctrl_g(const ftxui::Event& event) {
  return event == ftxui::Event::CtrlG ||
         event == ftxui::Event::Special("\x1B[27;5;103~") ||
         event == ftxui::Event::Special("\x1B[27;5;71~");
}

bool event_is_ctrl_k(const ftxui::Event& event) {
  return event == ftxui::Event::CtrlK ||
         event == ftxui::Event::Special("\x1B[27;5;107~") ||
         event == ftxui::Event::Special("\x1B[27;5;75~");
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

namespace {

bool input_is_ctrl_modifier_key(const std::string& input) {
  return input.find("57442") != std::string::npos || input.find("57443") != std::string::npos;
}

bool input_is_alt_modifier_key(const std::string& input) {
  return input.find("57440") != std::string::npos || input.find("57441") != std::string::npos;
}

bool input_is_kitty_release_event(const std::string& input) {
  return input.find(":3u") != std::string::npos;
}

}  // namespace

bool event_is_shift_key_press(const ftxui::Event& event) {
  if (event == ftxui::Event::Special("\x1B[57417u") ||
      event == ftxui::Event::Special("\x1B[57417;1u") ||
      event == ftxui::Event::Special("\x1B[57418u") ||
      event == ftxui::Event::Special("\x1B[57418;1u")) {
    return true;
  }
  const std::string& input = event.input();
  return input.find("57417;1u") != std::string::npos ||
         input.find("57418;1u") != std::string::npos ||
         input.find("57417u") != std::string::npos ||
         input.find("57418u") != std::string::npos;
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
  const std::string& input = event.input();
  if (input_is_ctrl_modifier_key(input) && input_is_kitty_release_event(input)) {
    return true;
  }
  return event == ftxui::Event::Special("\x1B[57442;2u") ||
         event == ftxui::Event::Special("\x1B[57443;2u");
}

bool event_is_alt_key_press(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[57440u") ||
         event == ftxui::Event::Special("\x1B[57440;1u") ||
         event == ftxui::Event::Special("\x1B[57441u") ||
         event == ftxui::Event::Special("\x1B[57441;1u");
}

bool event_is_alt_key_release(const ftxui::Event& event) {
  const std::string& input = event.input();
  if (input_is_alt_modifier_key(input) && input_is_kitty_release_event(input)) {
    return true;
  }
  return event == ftxui::Event::Special("\x1B[57440;2u") ||
         event == ftxui::Event::Special("\x1B[57441;2u");
}

bool event_is_kitty_key_release(const ftxui::Event& event) {
  return input_is_kitty_release_event(event.input());
}

bool event_is_ctrl_p(const ftxui::Event& event) {
  if (event == ftxui::Event::CtrlP) {
    return true;
  }
  const std::string& input = event.input();
  if (input == "\x10") {
    return true;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) == 0x10) {
      return true;
    }
  }
  return input == "\x1B[112;5u" || input == "\x1B[16;5u" ||
         input == "\x1B[112;5;1~" || input == "\x1B[27;5;112~" ||
         input == "\x1B[27;5;80~";
}

bool event_is_alt_p(const ftxui::Event& event) {
  return event == ftxui::Event::AltP || alt_letter(event, 'p', 'P') ||
         event == ftxui::Event::Special("\x1Bp") ||
         event == ftxui::Event::Special("\x1B[112;3u") ||
         event == ftxui::Event::Special("\x1B[80;3u") ||
         event == ftxui::Event::Special("\x1B[27;3;112~") ||
         event == ftxui::Event::Special("\x1B[27;3;80~");
}

bool event_is_quick_open(const ftxui::Event& event) {
  return event_is_ctrl_p(event);
}

bool event_is_alt_e(const ftxui::Event& event) {
  return event == ftxui::Event::AltE || alt_letter(event, 'e', 'E') ||
         event == ftxui::Event::Special("\x1B" "e") ||
         event == ftxui::Event::Special("\x1B[101;3u") ||
         event == ftxui::Event::Special("\x1B[69;3u") ||
         event == ftxui::Event::Special("\x1B[27;3;101~") ||
         event == ftxui::Event::Special("\x1B[27;3;69~");
}

bool event_is_ctrl_o(const ftxui::Event& event) {
  if (event == ftxui::Event::CtrlO) {
    return true;
  }
  const std::string& input = event.input();
  if (input == "\x0f") {
    return true;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) == 0x0f) {
      return true;
    }
  }
  return input == "\x1B[111;5u" || input == "\x1B[15;5u" ||
         input == "\x1B[111;5;1~" || input == "\x1B[27;5;111~" ||
         input == "\x1B[27;5;79~";
}

bool event_is_ctrl_d(const ftxui::Event& event) {
  return event == ftxui::Event::CtrlD ||
         event == ftxui::Event::Special("\x1B[27;5;100~") ||
         event == ftxui::Event::Special("\x1B[27;5;68~");
}

bool event_is_ctrl_alt_d(const ftxui::Event& event) {
  const int mods[] = {7};
  return event == ftxui::Event::CtrlAltD ||
         csi_key_any_modifier(event, mods, 1, 68) ||
         csi_key_any_modifier(event, mods, 1, 100) ||
         event == ftxui::Event::Special("\x1B[27;7;68~") ||
         event == ftxui::Event::Special("\x1B[27;7;100~") ||
         event == ftxui::Event::Special("\x1B[68;7u") ||
         event == ftxui::Event::Special("\x1B[100;7u");
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

bool event_is_ctrl_a(const ftxui::Event& event) {
  if (event == ftxui::Event::CtrlA) {
    return true;
  }
  const std::string& input = event.input();
  if (input == "\x01") {
    return true;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) == 0x01) {
      return true;
    }
  }
  const int mods[] = {5};
  return csi_key_any_modifier(event, mods, 1, 97) ||
         csi_key_any_modifier(event, mods, 1, 65) ||
         input == "\x1B[97;5u" || input == "\x1B[1;5u" ||
         input == "\x1B[97;5;1~" || input == "\x1B[27;5;97~" ||
         input == "\x1B[27;5;65~";
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
  const std::string& input = event.input();
  if (input == "\x00" || input == "\0") {
    return true;
  }
  return event == ftxui::Event::Special("\x00") ||
         event == ftxui::Event::Special("\x1B[32;5u") ||
         event == ftxui::Event::Special("\x1B[32;6u") ||
         event == ftxui::Event::Special("\x1B[0;5;32~") ||
         event == ftxui::Event::Special("\x1B[0;6;32~") ||
         event == ftxui::Event::Special("\x1B[27;5;32~") ||
         event == ftxui::Event::Special("\x1B[27;6;32~") ||
         event == ftxui::Event::Special("\x1B[27;5;32;5~") ||
         event == ftxui::Event::Special("\x1B[32;5;1~");
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

bool event_is_ctrl_alt_period(const ftxui::Event& event) {
  const int mods[] = {7};
  return csi_key_any_modifier(event, mods, 1, 46) ||
         event == ftxui::Event::Special("\x1B[46;7u") ||
         event == ftxui::Event::Special("\x1B[27;7;46~");
}

bool event_is_ctrl_alt_slash(const ftxui::Event& event) {
  const int mods[] = {7};
  return csi_key_any_modifier(event, mods, 1, 47) ||
         event == ftxui::Event::Special("\x1B[47;7u") ||
         event == ftxui::Event::Special("\x1B[27;7;47~");
}

bool event_is_ctrl_alt_e(const ftxui::Event& event) {
  const int mods[] = {7};
  return event == ftxui::Event::CtrlAltE ||
         csi_key_any_modifier(event, mods, 1, 69) ||
         csi_key_any_modifier(event, mods, 1, 101) ||
         event == ftxui::Event::Special("\x1B[27;7;69~") ||
         event == ftxui::Event::Special("\x1B[27;7;101~") ||
         event == ftxui::Event::Special("\x1B[69;7u") ||
         event == ftxui::Event::Special("\x1B[101;7u");
}

bool event_is_cursor_history_back(const ftxui::Event& event) {
  const int mods[] = {7};
  return csi_key_any_modifier(event, mods, 1, 91) ||
         event == ftxui::Event::Special("\x1B[91;7u") ||
         event == ftxui::Event::Special("\x1B[27;7;91~");
}

bool event_is_cursor_history_forward(const ftxui::Event& event) {
  const int mods[] = {7};
  return csi_key_any_modifier(event, mods, 1, 93) ||
         event == ftxui::Event::Special("\x1B[93;7u") ||
         event == ftxui::Event::Special("\x1B[27;7;93~");
}

bool event_is_completion(const ftxui::Event& event) {
  return event_is_ctrl_space(event) || event_is_ctrl_period(event) ||
         event_is_ctrl_alt_period(event) || event_is_ctrl_alt_slash(event);
}

bool event_is_completion_trigger(const ftxui::Event& event, bool ctrl_modifier_held) {
  if (event_is_completion(event)) {
    return true;
  }
  if (ctrl_modifier_held && event.is_character() && event.character() == " ") {
    return true;
  }
  const std::string& input = event.input();
  return input.find("32;5") != std::string::npos || input.find("27;5;32") != std::string::npos ||
         input.find("57442") != std::string::npos && input.find("32") != std::string::npos;
}

bool event_is_go_to_definition(const ftxui::Event& event) {
  return event == ftxui::Event::F12 || event == ftxui::Event::Special("\x1B[24~");
}

bool event_is_go_to_declaration(const ftxui::Event& event) {
  return event == ftxui::Event::Special("\x1B[24;2~") ||
         event == ftxui::Event::Special("\x1B[24;7~") ||
         event == ftxui::Event::Special("\x1B[24;6~");
}

bool event_has_shift_modifier(const ftxui::Event& event) {
  return event_is_shift_key_press(event) || event_is_shift_left(event) ||
         event_is_shift_right(event) || event_is_shift_up(event) ||
         event_is_shift_down(event) || event_is_ctrl_shift_left(event) ||
         event_is_ctrl_shift_right(event) || event_is_ctrl_shift_up(event) ||
         event_is_ctrl_shift_down(event) || event_is_ctrl_shift_d(event) ||
         event_is_ctrl_shift_l(event) || event_is_ctrl_shift_h(event) ||
         event_is_ctrl_shift_o(event) || event_is_shift_home(event) ||
         event_is_shift_end(event);
}

bool event_input_has_shift_modifier(const ftxui::Event& event) {
  if (event_has_shift_modifier(event) || event_is_shift_key_press(event) ||
      event == ftxui::Event::TabReverse) {
    return true;
  }
  return input_has_shift_token(event.input());
}

bool event_input_has_shift_release(const ftxui::Event& event) {
  if (event_is_shift_key_release(event)) {
    return true;
  }
  return input_has_shift_release_token(event.input());
}

bool event_has_ctrl_modifier(const ftxui::Event& event) {
  if (event_is_plain_tab(event)) {
    return false;
  }
  return event_is_ctrl_key_press(event) || event_is_ctrl_c(event) ||
         event_is_ctrl_v(event) || event_is_ctrl_x(event) || event_is_ctrl_z(event) || event_is_ctrl_f(event) ||
         event_is_ctrl_g(event) || event_is_ctrl_u(event) || event_is_ctrl_d(event) ||
         event_is_ctrl_alt_d(event) || event_is_ctrl_shift_d(event) ||
         event_is_ctrl_backspace(event) ||
         event_is_ctrl_delete(event) || event_is_ctrl_space(event) ||
         event_is_ctrl_left(event) || event_is_ctrl_right(event) ||
         event_is_ctrl_alt_left(event) || event_is_ctrl_alt_right(event) ||
         event_is_ctrl_shift_left(event) || event_is_ctrl_shift_right(event) ||
         event_is_ctrl_alt_up(event) || event_is_ctrl_alt_down(event) ||
         event_is_ctrl_shift_up(event) || event_is_ctrl_shift_down(event) ||
         event_is_ctrl_alt_l(event) || event_is_ctrl_shift_l(event) ||
         event_is_ctrl_alt_h(event) || event_is_ctrl_shift_h(event) ||
         event_is_ctrl_alt_o(event) || event_is_ctrl_shift_o(event) ||
         event_is_ctrl_alt_z(event) || event_is_ctrl_shift_z(event) ||
         event_is_ctrl_alt_f(event) || event_is_ctrl_shift_f(event) ||
         event_is_ctrl_shift_s(event) ||
         event_is_ctrl_h(event) ||
         event == ftxui::Event::CtrlS ||
         event == ftxui::Event::CtrlQ ||
         event == ftxui::Event::CtrlB || event == ftxui::Event::CtrlW ||
         event_is_ctrl_i(event) || event_is_ctrl_p(event) || event_is_ctrl_o(event);
}

bool event_is_tuide_global_shortcut(const ftxui::Event& event) {
  return event_is_quick_open(event) || event == ftxui::Event::CtrlT ||
         event_is_ctrl_o(event) || event == ftxui::Event::CtrlQ ||
         event == ftxui::Event::CtrlA || event == ftxui::Event::CtrlE ||
         event == ftxui::Event::CtrlB;
}

bool event_is_tuide_app_shortcut(const ftxui::Event& event) {
  if (event_is_tuide_global_shortcut(event)) {
    return true;
  }
  if (event_is_f1(event) || event_is_ctrl_alt_e(event) || event_is_open_shortcuts_modal(event)) {
    return true;
  }
  if (event_is_open_search_panel(event) || event_is_open_outline_panel(event) ||
      event_is_open_binary_symbols_panel(event) || event_is_ctrl_shift_s(event)) {
    return true;
  }
  return event == ftxui::Event::F2 || event == ftxui::Event::F3 ||
         event == ftxui::Event::F4 || event == ftxui::Event::F5 ||
         event == ftxui::Event::F6 || event == ftxui::Event::F9 ||
         event == ftxui::Event::F10 || event == ftxui::Event::F11 ||
         event == ftxui::Event::Special({24});
}

bool editor_priority_key(const ftxui::Event& event) {
  return event == ftxui::Event::Escape || event_is_ctrl_z(event) || event_is_ctrl_c(event) ||
         event_is_ctrl_v(event) || event_is_ctrl_x(event) || event_is_ctrl_f(event) || event_is_ctrl_g(event) ||
         event_is_shift_left(event) || event_is_shift_right(event) || event_is_shift_up(event) ||
         event_is_shift_down(event) || event_is_ctrl_left(event) || event_is_ctrl_right(event) ||
         event_is_ctrl_alt_left(event) || event_is_ctrl_alt_right(event) ||
         event_is_ctrl_shift_left(event) || event_is_ctrl_shift_right(event) ||
         event_is_ctrl_alt_up(event) || event_is_ctrl_alt_down(event) ||
         event_is_ctrl_shift_up(event) || event_is_ctrl_shift_down(event) ||
         event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight ||
         event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown ||
         event == ftxui::Event::Backspace || event == ftxui::Event::Delete ||
         event == ftxui::Event::Return || event == ftxui::Event::Tab ||
         event == ftxui::Event::Home || event == ftxui::Event::End ||
         event_is_shift_home(event) || event_is_shift_end(event) ||
         event == ftxui::Event::PageUp || event == ftxui::Event::PageDown ||
         event_is_ctrl_u(event) || event_is_ctrl_i(event) || event_is_ctrl_d(event) ||
         event_is_ctrl_alt_d(event) || event_is_ctrl_shift_d(event) ||
         event_is_ctrl_backspace(event) ||
         event_is_ctrl_delete(event) || event == ftxui::Event::CtrlS ||
         event_is_ctrl_alt_l(event) || event_is_ctrl_shift_l(event) ||
         event_is_completion(event) || event_is_go_to_definition(event) ||
         event_is_go_to_declaration(event) ||
         event_is_cursor_history_back(event) || event_is_cursor_history_forward(event);
}

bool event_has_alt_modifier(const ftxui::Event& event) {
  if (event_is_alt_key_press(event) || event_is_alt_key_release(event)) {
    return true;
  }
  if (event == ftxui::Event::AltH || event == ftxui::Event::AltO || event == ftxui::Event::AltP ||
      event == ftxui::Event::AltE || event == ftxui::Event::AltS) {
    return true;
  }
  if (event_is_alt_left(event) || event_is_alt_right(event)) {
    return true;
  }
  if (event == ftxui::Event::Special("\x1B[1;3A") || event == ftxui::Event::Special("\x1B[1;3B")) {
    return true;
  }
  const std::string& input = event.input();
  if (input.size() == 2 && input[0] == '\x1B' && static_cast<unsigned char>(input[1]) >= 32) {
    return true;
  }
  if (input.find(";3u") != std::string::npos || input.find(";3~") != std::string::npos ||
      input.find("[1;3") != std::string::npos || input.find("[27;3;") != std::string::npos) {
    return !event_has_ctrl_modifier(event);
  }
  return false;
}

bool event_is_helix_overlay_exempt(const ftxui::Event& event) {
  return event_is_open_shortcuts_modal(event);
}

namespace {

std::optional<ftxui::Event> kitty_alt_key_event(int keycode) {
  switch (keycode) {
    case 13:
      return ftxui::Event::Return;
    case 27:
      return ftxui::Event::Escape;
    case 32:
      return ftxui::Event::Character(' ');
    case 8:
      return ftxui::Event::Backspace;
    case 9:
      return ftxui::Event::Tab;
    case 127:
      return ftxui::Event::Delete;
    case 57399:
      return ftxui::Event::Home;
    case 57400:
      return ftxui::Event::End;
    case 57421:
      return ftxui::Event::PageUp;
    case 57422:
      return ftxui::Event::PageDown;
    case 57416:
      return ftxui::Event::ArrowUp;
    case 57414:
      return ftxui::Event::ArrowDown;
    case 57415:
      return ftxui::Event::ArrowLeft;
    case 57417:
      return ftxui::Event::ArrowRight;
    default:
      break;
  }
  if (keycode >= 97 && keycode <= 122) {
    return ftxui::Event::Character(static_cast<char>(keycode));
  }
  if (keycode >= 65 && keycode <= 90) {
    return ftxui::Event::Character(static_cast<char>(keycode));
  }
  if (keycode >= 48 && keycode <= 57) {
    return ftxui::Event::Character(static_cast<char>(keycode));
  }
  if (keycode >= 33 && keycode <= 126) {
    return ftxui::Event::Character(static_cast<char>(keycode));
  }
  return std::nullopt;
}

bool parse_kitty_alt_only_keycode(const std::string& input, int* out_keycode) {
  if (input.size() < 5 || input[0] != '\x1B' || input[1] != '[') {
    return false;
  }
  const std::size_t semi = input.find(';');
  if (semi == std::string::npos || input.back() != 'u') {
    return false;
  }
  const std::string modifier = input.substr(semi + 1, input.size() - semi - 2);
  if (modifier != "3") {
    return false;
  }
  try {
    *out_keycode = std::stoi(input.substr(2, semi - 2));
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace

std::optional<ftxui::Event> strip_alt_modifier_for_helix(const ftxui::Event& event) {
  if (event_is_alt_key_press(event) || event_is_alt_key_release(event) ||
      event_is_helix_overlay_exempt(event) || event_has_ctrl_modifier(event)) {
    return std::nullopt;
  }

  if (event == ftxui::Event::AltH) {
    return ftxui::Event::Character('h');
  }
  if (event == ftxui::Event::AltO) {
    return ftxui::Event::Character('o');
  }
  if (event == ftxui::Event::AltP) {
    return ftxui::Event::Character('p');
  }
  if (event == ftxui::Event::AltE) {
    return ftxui::Event::Character('e');
  }
  if (event == ftxui::Event::AltS) {
    return ftxui::Event::Character('s');
  }
  if (event_is_alt_left(event)) {
    return ftxui::Event::ArrowLeft;
  }
  if (event_is_alt_right(event)) {
    return ftxui::Event::ArrowRight;
  }
  if (event == ftxui::Event::Special("\x1B[1;3A")) {
    return ftxui::Event::ArrowUp;
  }
  if (event == ftxui::Event::Special("\x1B[1;3B")) {
    return ftxui::Event::ArrowDown;
  }

  const std::string& input = event.input();
  if (input.size() == 2 && input[0] == '\x1B' && static_cast<unsigned char>(input[1]) >= 32) {
    return ftxui::Event::Character(input[1]);
  }

  int keycode = 0;
  if (parse_kitty_alt_only_keycode(input, &keycode)) {
    return kitty_alt_key_event(keycode);
  }

  return std::nullopt;
}

}  // namespace tuide
