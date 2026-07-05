#pragma once

#include "ftxui/component/event.hpp"

namespace tgdb {

bool event_is_shift_home(const ftxui::Event& event);
bool event_is_shift_end(const ftxui::Event& event);

bool event_is_ctrl_alt_l(const ftxui::Event& event);
bool event_is_ctrl_shift_l(const ftxui::Event& event);
bool event_is_alt_f1(const ftxui::Event& event);
bool event_is_shift_f1(const ftxui::Event& event);
bool event_is_open_shortcuts_modal(const ftxui::Event& event);
bool event_is_f1(const ftxui::Event& event);

bool event_is_shift_left(const ftxui::Event& event);
bool event_is_shift_right(const ftxui::Event& event);
bool event_is_shift_up(const ftxui::Event& event);
bool event_is_shift_down(const ftxui::Event& event);
bool event_is_alt_left(const ftxui::Event& event);
bool event_is_alt_right(const ftxui::Event& event);
bool event_is_alt_s(const ftxui::Event& event);
bool event_is_ctrl_alt_up(const ftxui::Event& event);
bool event_is_ctrl_alt_down(const ftxui::Event& event);
bool event_is_ctrl_shift_up(const ftxui::Event& event);
bool event_is_ctrl_shift_down(const ftxui::Event& event);
bool event_is_open_search_panel(const ftxui::Event& event);
bool event_is_open_outline_panel(const ftxui::Event& event);
bool event_is_open_binary_symbols_panel(const ftxui::Event& event);

bool event_is_ctrl_alt_o(const ftxui::Event& event);
bool event_is_ctrl_shift_o(const ftxui::Event& event);

bool event_is_ctrl_left(const ftxui::Event& event);
bool event_is_ctrl_right(const ftxui::Event& event);
bool event_is_ctrl_alt_left(const ftxui::Event& event);
bool event_is_ctrl_alt_right(const ftxui::Event& event);
bool event_is_ctrl_shift_left(const ftxui::Event& event);
bool event_is_ctrl_shift_right(const ftxui::Event& event);
bool event_is_ctrl_c(const ftxui::Event& event);
bool event_is_ctrl_z(const ftxui::Event& event);
bool event_is_ctrl_alt_z(const ftxui::Event& event);
bool event_is_ctrl_shift_z(const ftxui::Event& event);
bool event_is_ctrl_y(const ftxui::Event& event);
bool event_is_ctrl_v(const ftxui::Event& event);
bool event_is_ctrl_x(const ftxui::Event& event);
bool event_is_ctrl_f(const ftxui::Event& event);
bool event_is_ctrl_alt_f(const ftxui::Event& event);
bool event_is_ctrl_shift_f(const ftxui::Event& event);
bool event_is_workspace_search_with_selection(const ftxui::Event& event);
bool event_is_ctrl_alt_h(const ftxui::Event& event);
bool event_is_ctrl_shift_h(const ftxui::Event& event);
bool event_is_ctrl_h(const ftxui::Event& event);
bool event_is_ctrl_g(const ftxui::Event& event);
bool event_is_ctrl_k(const ftxui::Event& event);
bool event_is_ctrl_u(const ftxui::Event& event);
bool event_is_ctrl_i(const ftxui::Event& event);
bool event_is_ctrl_d(const ftxui::Event& event);
bool event_is_ctrl_alt_d(const ftxui::Event& event);
bool event_is_ctrl_shift_d(const ftxui::Event& event);
bool event_is_ctrl_backspace(const ftxui::Event& event);
bool event_is_ctrl_delete(const ftxui::Event& event);
bool event_is_ctrl_space(const ftxui::Event& event);
bool event_is_completion(const ftxui::Event& event);
bool event_is_completion_trigger(const ftxui::Event& event, bool ctrl_modifier_held);
bool event_is_go_to_definition(const ftxui::Event& event);
bool event_is_go_to_declaration(const ftxui::Event& event);

bool event_is_shift_key_press(const ftxui::Event& event);
bool event_is_shift_key_release(const ftxui::Event& event);
bool event_is_ctrl_key_press(const ftxui::Event& event);
bool event_is_ctrl_key_release(const ftxui::Event& event);
bool event_has_shift_modifier(const ftxui::Event& event);
bool event_has_ctrl_modifier(const ftxui::Event& event);
bool event_is_plain_tab(const ftxui::Event& event);

bool event_input_has_shift_modifier(const ftxui::Event& event);
bool event_input_has_shift_release(const ftxui::Event& event);

bool editor_priority_key(const ftxui::Event& event);

bool event_is_tide_global_shortcut(const ftxui::Event& event);

// App-level shortcuts that must not be consumed by Helix normal mode or prefix pending.
bool event_is_tide_app_shortcut(const ftxui::Event& event);

}  // namespace tgdb
