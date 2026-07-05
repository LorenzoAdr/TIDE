#include "editor/helix/helix_hints.hpp"

#include "ftxui/dom/elements.hpp"
#include "i18n/tr.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

Element hint_table(const std::vector<std::pair<std::string, std::string>>& entries,
                   const std::string& title) {
  Elements rows;
  rows.push_back(text(title) | bold | color(theme::Accent()));
  rows.push_back(separator());
  if (entries.empty()) {
    rows.push_back(text(i18n::tr("helix.no_shortcuts")) | color(theme::Muted()));
  } else {
    for (const auto& [key, desc] : entries) {
      rows.push_back(hbox({
          text(" " + key + " ") | color(theme::Accent()) | bold,
          text("  " + desc) | color(theme::Header()),
      }));
    }
  }
  return vbox(std::move(rows)) | border | bgcolor(theme::PanelBg()) | color(theme::Header());
}

}  // namespace

Element make_helix_hint_overlay(const HelixEditorState& helix) {
  if (!helix.hint_visible || helix.pending_keys.empty()) {
    return text("");
  }
  const HelixKeyTrieNode* base = &helix_keymap_root(helix.mode);
  for (const std::string& key : helix.pending_keys) {
    const auto it = base->children.find(key);
    if (it == base->children.end()) {
      return text("");
    }
    base = &it->second;
  }
  const auto entries = helix_hint_entries(helix.mode, base);
  const std::string title =
      i18n::tr_fmt("helix.hints.title_suffix", {helix.pending_label()});
  Element panel = hint_table(entries, title);
  return dbox({
      text(""),
      vbox({
          filler(),
          hbox({filler(), panel}),
      }),
  });
}

std::vector<std::pair<std::string, std::string>> helix_help_sections() {
  return {
      {"hjkl / flechas", i18n::tr("helix.help.move_highlight")},
      {"w / b / e", i18n::tr("helix.help.word_motion")},
      {"w + d / w + c", i18n::tr("helix.help.delete_change_selection")},
      {"5j / 3w", i18n::tr("helix.help.count_prefix")},
      {"i / a / o / O", i18n::tr("helix.help.insert_modes")},
      {"v", i18n::tr("helix.help.select_mode")},
      {"w + d", i18n::tr("helix.help.delete_word")},
      {"x + d", i18n::tr("helix.help.delete_line")},
      {"; d", i18n::tr("helix.help.delete_char")},
      {"d / c / y / p", i18n::tr("helix.help.yank_ops")},
      {"u / U", i18n::tr("helix.help.undo_redo")},
      {"/  n  N", i18n::tr("helix.help.search")},
      {"g g / g e", i18n::tr("helix.help.file_bounds")},
      {"g g + g e", i18n::tr("helix.help.select_whole_file")},
      {"%", i18n::tr("helix.help.select_whole_file_percent")},
      {"g d", i18n::tr("helix.help.goto_definition")},
      {"z u / z d", i18n::tr("helix.help.scroll")},
      {"m i w / m a w", i18n::tr("helix.help.text_objects_word")},
      {"m i ( / m a (", i18n::tr("helix.help.text_objects_paren")},
      {"m m", i18n::tr("helix.help.match_brackets")},
      {"[ d / ] d", i18n::tr("helix.help.diagnostics")},
      {"[ f / ] f  [ t / ] t  [ p / ] p  [ } / ] }", i18n::tr("helix.help.scope_nav")},
      {"space f / space s", i18n::tr("helix.help.space_palette")},
      {"space c", i18n::tr("helix.help.toggle_comments")},
      {"space ?", i18n::tr("helix.help.help_overlay")},
      {": w / : q", i18n::tr("helix.help.command_mode")},
      {"Esc", i18n::tr("helix.help.escape")},
      {"Ctrl+*", i18n::tr("helix.help.tide_shortcuts")},
  };
}

Element make_helix_command_overlay(const HelixEditorState& helix) {
  if (!helix.command_mode) {
    return text("");
  }
  const std::string line = ":" + helix.command_buffer + "_";
  Element panel = hbox({text(line) | color(theme::Accent())}) | border | bgcolor(theme::PanelBg());
  return vbox({
      text(""),
      hbox({filler(), panel}) | flex,
  });
}

Element make_helix_help_overlay(const HelixEditorState& helix) {
  if (!helix.help_open) {
    return text("");
  }
  Elements rows;
  rows.push_back(text(i18n::tr("helix.help.title")) | bold | color(theme::Accent()));
  rows.push_back(separator());
  for (const auto& [key, desc] : helix_help_sections()) {
    rows.push_back(hbox({
        text(" " + key + " ") | color(theme::Accent()) | size(WIDTH, EQUAL, 18),
        text(desc) | color(theme::Header()),
    }));
  }
  rows.push_back(separator());
  rows.push_back(text(i18n::tr("helix.help.footer")) | color(theme::Muted()));
  Element panel = vbox(std::move(rows)) | border | bgcolor(theme::PanelBg()) | size(WIDTH, LESS_THAN, 60);
  return ScreenModalOverlay(text(""), panel);
}

}  // namespace tgdb
