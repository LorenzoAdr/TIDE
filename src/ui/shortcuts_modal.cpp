#include "ui/shortcuts_modal.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "i18n/tr.hpp"
#include "ui/key_bindings.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace tuide {

using namespace ftxui;

namespace {

constexpr int kKeyWidth = 20;
constexpr int kVisibleRows = 20;

struct ShortcutEntry {
  std::string key;
  std::string desc;
};

struct ShortcutSection {
  std::string title;
  std::vector<ShortcutEntry> entries;
};

std::vector<ShortcutSection> shortcut_sections() {
  return {
      {i18n::tr("shortcuts.section.general"),
       {
           {"F1", i18n::tr("shortcuts.general.open_external")},
           {"Alt+E", i18n::tr("shortcuts.general.open_external_here")},
           {"Alt+F1 / Shift+F1", i18n::tr("shortcuts.general.keyboard_shortcuts")},
           {"F2", i18n::tr("shortcuts.general.debug_wizard")},
           {"F3", i18n::tr("shortcuts.general.change_workspace")},
           {"F4", i18n::tr("shortcuts.general.terminal_tab")},
           {"F5", i18n::tr("shortcuts.general.quick_launch")},
           {"F7", i18n::tr("shortcuts.general.search_panel")},
           {"F8", i18n::tr("shortcuts.general.outline_panel")},
           {"F9", i18n::tr("shortcuts.general.problems_panel")},
           {"F6", i18n::tr("shortcuts.general.helix_toggle")},
           {"Ctrl+Shift+S", i18n::tr("shortcuts.general.binary_symbols")},
           {"F10", i18n::tr("shortcuts.general.settings")},
           {"Ctrl+P / Alt+P", i18n::tr("shortcuts.general.quick_open")},
           {"Ctrl+O", i18n::tr("shortcuts.general.go_to_symbol")},
           {"Ctrl+T", i18n::tr("shortcuts.general.toggle_bottom_panel")},
           {"Ctrl+Q", i18n::tr("shortcuts.general.quit")},
           {"Ctrl+A", i18n::tr("shortcuts.general.focus_explorer")},
           {"Ctrl+E", i18n::tr("shortcuts.general.focus_editor")},
           {"Alt+← / →", i18n::tr("shortcuts.general.move_focus_horizontal")},
           {"Alt+↑ / ↓", i18n::tr("shortcuts.general.move_focus_vertical")},
           {"Esc", i18n::tr("shortcuts.general.unfocus_input")},
       }},
      {i18n::tr("shortcuts.section.editor"),
       {
           {"Ctrl+S", i18n::tr("shortcuts.editor.save")},
           {"Ctrl+F", i18n::tr("shortcuts.editor.find_in_file")},
           {"Ctrl+Alt+F / Ctrl+Shift+F", i18n::tr("shortcuts.editor.find_in_workspace")},
           {"Ctrl+G", i18n::tr("shortcuts.editor.goto_line")},
           {"Ctrl+Z", i18n::tr("shortcuts.editor.undo")},
           {"Ctrl+Alt+Z / Ctrl+Shift+Z / Ctrl+Y", i18n::tr("shortcuts.editor.redo")},
           {"Ctrl+C", i18n::tr("shortcuts.editor.copy")},
           {"Ctrl+K Ctrl+C", i18n::tr("shortcuts.editor.comment_lines")},
           {"Ctrl+K Ctrl+U", i18n::tr("shortcuts.editor.uncomment_lines")},
           {"Ctrl+X", i18n::tr("shortcuts.editor.cut")},
           {"Ctrl+V", i18n::tr("shortcuts.editor.paste")},
           {"Ctrl+U", i18n::tr("shortcuts.editor.half_page_up")},
           {"Ctrl+I", i18n::tr("shortcuts.editor.half_page_down")},
           {"Ctrl+Backspace", i18n::tr("shortcuts.editor.delete_word_backward")},
           {"Ctrl+Delete", i18n::tr("shortcuts.editor.delete_word_forward")},
           {"Ctrl+D / Ctrl+Alt+D / Ctrl+Shift+D", i18n::tr("shortcuts.editor.select_next_match")},
           {"Ctrl+Alt+L / Ctrl+Shift+L", i18n::tr("shortcuts.editor.select_all_matches")},
           {"Alt+.", i18n::tr("shortcuts.editor.complete_recommended")},
           {"Ctrl+. / Ctrl+Space", i18n::tr("shortcuts.editor.complete_lsp")},
           {"Ctrl+Shift+Space", i18n::tr("shortcuts.editor.complete_alt")},
           {i18n::tr("shortcuts.key.while_typing"), i18n::tr("shortcuts.editor.auto_complete")},
           {"F12", i18n::tr("shortcuts.editor.go_to_definition")},
           {"Alt+← / →", i18n::tr("shortcuts.editor.cursor_history")},
           {"Shift+F12 / Ctrl+Alt+F12 / Ctrl+Shift+F12", i18n::tr("shortcuts.editor.go_to_declaration")},
           {i18n::tr("shortcuts.key.alt_click"), i18n::tr("shortcuts.editor.select_to_click")},
           {i18n::tr("shortcuts.key.ctrl_click"), i18n::tr("shortcuts.editor.ctrl_click_definition")},
           {i18n::tr("shortcuts.key.ctrl_mod_click"),
            i18n::tr("shortcuts.editor.ctrl_click_declaration")},
           {"Tab", i18n::tr("shortcuts.editor.indent")},
           {i18n::tr("shortcuts.key.shift_arrows"), i18n::tr("shortcuts.editor.extend_selection")},
           {i18n::tr("shortcuts.key.ctrl_arrows"), i18n::tr("shortcuts.editor.move_by_words")},
           {"Ctrl+Alt+↑/↓ / Ctrl+Shift+↑/↓", i18n::tr("shortcuts.editor.block_selection")},
       }},
      {i18n::tr("shortcuts.section.helix"),
       {
           {"hjkl", i18n::tr("shortcuts.helix.move_cursor")},
           {"5j / 3w", i18n::tr("shortcuts.helix.count")},
           {"w + d", i18n::tr("shortcuts.helix.word_delete")},
           {"x + d", i18n::tr("shortcuts.helix.line_delete")},
           {"i / a / o", i18n::tr("shortcuts.helix.insert")},
           {"v", i18n::tr("shortcuts.helix.select_mode")},
           {"d / c / y / p", i18n::tr("shortcuts.helix.edit_ops")},
           {"u / U", i18n::tr("shortcuts.helix.undo_redo")},
           {"/  n  N", i18n::tr("shortcuts.helix.search")},
           {"g g / g e", i18n::tr("shortcuts.helix.file_bounds")},
           {"g d", i18n::tr("shortcuts.helix.goto_definition")},
           {"z u / z d", i18n::tr("shortcuts.helix.scroll")},
           {"m i w / m i (", i18n::tr("shortcuts.helix.select_inner_word")},
           {"space f/s", i18n::tr("shortcuts.helix.ide_bridge")},
           {"space c", i18n::tr("shortcuts.helix.comments")},
           {"space ?", i18n::tr("shortcuts.helix.help")},
           {"Esc", i18n::tr("shortcuts.helix.normal_cancel")},
           {"F6", i18n::tr("shortcuts.general.helix_toggle")},
       }},
      {i18n::tr("shortcuts.section.debug"),
       {
           {"F5", i18n::tr("shortcuts.debug.continue")},
           {"F10", i18n::tr("shortcuts.debug.step_over")},
           {"F11", i18n::tr("shortcuts.debug.step_into")},
           {"Shift+F11", i18n::tr("shortcuts.debug.step_out")},
           {"Ctrl+U", i18n::tr("shortcuts.debug.scroll_half_up")},
           {"Ctrl+I", i18n::tr("shortcuts.debug.scroll_half_down")},
           {"Ctrl+B", i18n::tr("shortcuts.debug.toggle_breakpoint")},
           {"Ctrl+Shift+S", i18n::tr("shortcuts.debug.source_substitute")},
           {i18n::tr("shortcuts.key.gutter_click"), i18n::tr("shortcuts.debug.gutter_breakpoint")},
           {i18n::tr("shortcuts.key.gdb_console"), i18n::tr("shortcuts.debug.gdb_console")},
           {"1 / 2", i18n::tr("shortcuts.debug.bottom_tabs")},
           {"3", i18n::tr("shortcuts.debug.performance_tab")},
       }},
      {i18n::tr("shortcuts.section.debug_panel"),
       {
           {"1–4", i18n::tr("shortcuts.debug_panel.tabs")},
           {"j / k", i18n::tr("shortcuts.debug_panel.navigate_rows")},
           {"Enter", i18n::tr("shortcuts.debug_panel.expand_or_frame")},
           {"e / =", i18n::tr("shortcuts.debug_panel.edit_watch")},
           {"x / d", i18n::tr("shortcuts.debug_panel.delete_watch_or_bp")},
       }},
      {i18n::tr("shortcuts.section.terminal"),
       {
           {i18n::tr("shortcuts.key.enter_click"), i18n::tr("shortcuts.terminal.type_in_shell")},
           {"Tab", i18n::tr("shortcuts.terminal.send_tab")},
           {"Alt+← / →", i18n::tr("shortcuts.terminal.switch_bottom_tab")},
           {"1", i18n::tr("shortcuts.terminal.tab_terminal")},
           {"2", i18n::tr("shortcuts.terminal.tab_performance_or_gdb")},
       }},
      {i18n::tr("shortcuts.section.performance"),
       {
           {i18n::tr("shortcuts.key.click"), i18n::tr("shortcuts.performance.open_tab")},
           {"j / k", i18n::tr("shortcuts.performance.scroll_threads")},
           {"1 / 2 / 3", i18n::tr("shortcuts.performance.bottom_tabs")},
       }},
      {i18n::tr("shortcuts.section.git"),
       {
           {"1 / 2 / 3", i18n::tr("shortcuts.git.tabs")},
           {"s / u", i18n::tr("shortcuts.git.stage_unstage")},
           {"p / P", i18n::tr("shortcuts.git.push_pull")},
           {"c", i18n::tr("shortcuts.git.focus_commit")},
           {"Enter", i18n::tr("shortcuts.git.confirm_commit")},
           {"Esc / Ctrl+E", i18n::tr("shortcuts.git.back_to_editor")},
       }},
  };
}

Element render_key(const std::string& key) {
  return text(key) | color(theme::Accent()) | bold | size(WIDTH, EQUAL, kKeyWidth);
}

Element render_desc(const std::string& desc) {
  return text(desc) | color(theme::Header()) | flex;
}

std::vector<Element> build_rows() {
  std::vector<Element> rows;
  for (const auto& section : shortcut_sections()) {
    rows.push_back(text(section.title) | bold | color(theme::Accent()) | underlined);
    for (const auto& entry : section.entries) {
      rows.push_back(hbox({render_key(entry.key), render_desc(entry.desc)}));
    }
    rows.push_back(separator() | color(theme::AccentDim()));
  }
  if (!rows.empty()) {
    rows.pop_back();
  }
  return rows;
}

void clamp_scroll(ShortcutsModalState* state, int total_rows) {
  if (state == nullptr) {
    return;
  }
  const int max_first = std::max(0, total_rows - kVisibleRows);
  state->first_visible = std::max(0, std::min(state->first_visible, max_first));
}

bool handle_scroll_keys(ShortcutsModalState* state, Event event, int total_rows) {
  if (state == nullptr || !state->open) {
    return false;
  }

  if (event == Event::Escape || event_is_open_shortcuts_modal(event)) {
    state->open = false;
    state->first_visible = 0;
    return true;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->first_visible += 1;
    clamp_scroll(state, total_rows);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->first_visible -= 1;
    clamp_scroll(state, total_rows);
    return true;
  }
  if (event == Event::PageDown) {
    state->first_visible += kVisibleRows;
    clamp_scroll(state, total_rows);
    return true;
  }
  if (event == Event::PageUp) {
    state->first_visible -= kVisibleRows;
    clamp_scroll(state, total_rows);
    return true;
  }
  if (event == Event::Home) {
    state->first_visible = 0;
    return true;
  }
  if (event == Event::End) {
    state->first_visible = std::max(0, total_rows - kVisibleRows);
    return true;
  }
  return true;
}

}  // namespace

Component MakeShortcutsModalOverlay(Component main, ShortcutsModalState* state) {
  return Renderer(
      CatchEvent(main, [state](Event event) {
        if (state == nullptr || !state->open) {
          return false;
        }
        const auto rows = build_rows();
        return handle_scroll_keys(state, event, static_cast<int>(rows.size()));
      }),
      [main, state] {
        Element base = main->Render();
        if (state == nullptr || !state->open) {
          return base;
        }

        const auto rows = build_rows();
        clamp_scroll(state, static_cast<int>(rows.size()));

        Elements visible;
        const int end =
            std::min(static_cast<int>(rows.size()), state->first_visible + kVisibleRows);
        for (int i = state->first_visible; i < end; ++i) {
          visible.push_back(rows[static_cast<std::size_t>(i)]);
        }

        const int total = static_cast<int>(rows.size());
        const bool can_scroll = total > kVisibleRows;
        std::string footer = i18n::tr("modal.shortcuts.footer.close");
        if (can_scroll) {
          footer += i18n::tr("modal.shortcuts.footer.scroll");
        }

        Element dialog = ModalWindow(
            text(i18n::tr("modal.shortcuts.title")) | color(theme::Accent()),
            vbox({
                vbox(std::move(visible)) | flex,
                separator() | color(theme::AccentDim()),
                text(footer) | color(theme::Muted()),
            }));

        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tuide
