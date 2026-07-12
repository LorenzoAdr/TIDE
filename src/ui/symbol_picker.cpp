#include "ui/symbol_picker.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_set>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "editor/editor_buffer_source.hpp"
#include "indexer/index_rules.hpp"
#include "parser/tree_sitter_service.hpp"
#include "ui/key_bindings.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"
#include "i18n/tr.hpp"
#include "util/fuzzy_match.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

constexpr int kModalWidth = 84;
constexpr int kTextWidth = kModalWidth - 4;
constexpr int kMaxRows = 14;
constexpr std::size_t kMaxCatalogEntries = 12000;

Element render_fuzzy_chars(const std::string& segment, std::size_t label_offset,
                           const std::unordered_set<std::size_t>& hits, Color base_color) {
  Elements parts;
  std::string run;
  Color run_color = base_color;
  auto flush = [&]() {
    if (!run.empty()) {
      parts.push_back(text(run) | color(run_color));
      run.clear();
    }
  };
  for (std::size_t i = 0; i < segment.size(); ++i) {
    const std::size_t label_index = label_offset + i;
    const Color want = hits.count(label_index) != 0 ? theme::Error() : base_color;
    if (!run.empty() && want != run_color) {
      flush();
    }
    run_color = want;
    run.push_back(segment[i]);
  }
  flush();
  return parts.size() == 1 ? parts[0] : hbox(std::move(parts));
}

std::string truncate_path_prefix(const std::string& path, int max_width) {
  if (static_cast<int>(path.size()) <= max_width) {
    return path;
  }
  constexpr const char* kEllipsis = "…";
  const int tail_budget = max_width - 1;
  if (tail_budget <= 0) {
    return kEllipsis;
  }
  std::string tail = path.substr(path.size() - static_cast<std::size_t>(tail_budget));
  const auto sep = tail.find_first_of("/\\");
  if (sep != std::string::npos) {
    tail = tail.substr(sep + 1);
  }
  return std::string(kEllipsis) + tail;
}

Element render_symbol_row(const SymbolPickerMatch& match, bool selected, int max_width) {
  const SymbolInfo& sym = match.symbol;
  const std::string suffix = "  :" + std::to_string(sym.line);
  const int suffix_width = static_cast<int>(suffix.size());
  int name_budget = max_width - suffix_width;
  if (!sym.file.empty()) {
    const std::string file_tag = "  " + sym.file;
    name_budget -= std::min(static_cast<int>(file_tag.size()), max_width / 3);
  }
  if (name_budget < 8) {
    name_budget = 8;
  }

  std::string name = sym.name;
  if (static_cast<int>(name.size()) > name_budget) {
    name = name.substr(name.size() - static_cast<std::size_t>(name_budget));
  }

  std::unordered_set<std::size_t> hits(match.match_indices.begin(), match.match_indices.end());
  Elements parts;
  parts.push_back(render_fuzzy_chars(name, sym.name.size() - name.size(), hits, theme::Header()));
  parts.push_back(text(suffix) | color(theme::Muted()));
  if (!sym.file.empty()) {
    const int file_budget =
        std::max(0, max_width - static_cast<int>(name.size()) - suffix_width);
    if (file_budget > 0) {
      parts.push_back(text("  " + truncate_path_prefix(sym.file, file_budget)) | color(theme::Muted()));
    }
  }

  Element row = hbox(std::move(parts));
  if (selected) {
    row = row | inverted | bold;
  }
  return row;
}

std::shared_ptr<std::vector<SymbolCatalogEntry>> build_catalog_from_snapshot(
    const SymbolIndexSnapshot& snapshot) {
  auto catalog = std::make_shared<std::vector<SymbolCatalogEntry>>();
  catalog->reserve(snapshot.symbols.size());
  for (const IndexedSymbol& indexed : snapshot.symbols) {
    SymbolInfo sym;
    sym.name = indexed.display_name;
    sym.kind = indexed.kind;
    sym.line = indexed.line;
    sym.file = indexed.file;
    catalog->push_back({sym, fuzzy_to_lower(sym.name)});
  }
  std::sort(catalog->begin(), catalog->end(),
            [](const SymbolCatalogEntry& a, const SymbolCatalogEntry& b) {
              return a.symbol.name < b.symbol.name;
            });
  if (catalog->size() > kMaxCatalogEntries) {
    catalog->resize(kMaxCatalogEntries);
  }
  return catalog;
}

std::shared_ptr<std::vector<SymbolCatalogEntry>> build_catalog_from_file(
    const std::vector<SymbolInfo>& symbols) {
  auto catalog = std::make_shared<std::vector<SymbolCatalogEntry>>();
  catalog->reserve(symbols.size());
  for (const SymbolInfo& sym : symbols) {
    catalog->push_back({sym, fuzzy_to_lower(sym.name)});
  }
  std::sort(catalog->begin(), catalog->end(),
            [](const SymbolCatalogEntry& a, const SymbolCatalogEntry& b) {
              return a.symbol.name < b.symbol.name;
            });
  return catalog;
}

}  // namespace

void SymbolPickerState::set_search_notify(std::function<void()> notify) {
  search_notify_ = std::move(notify);
}

void SymbolPickerState::notify_search_tick() {
  if (search_notify_) {
    search_notify_();
  }
}

void SymbolPickerState::sync_catalog(const WorkspaceModel& workspace,
                                     const std::shared_ptr<ISymbolProvider>& symbols,
                                     SymbolWorkspaceIndexer* symbol_indexer) {
  const std::shared_ptr<const SymbolIndexSnapshot> snapshot =
      symbol_indexer != nullptr ? symbol_indexer->snapshot() : nullptr;
  if (snapshot != nullptr && snapshot->workspace_root == workspace.root &&
      !snapshot->symbols.empty()) {
    const std::string key = "snap:" + std::to_string(reinterpret_cast<std::uintptr_t>(snapshot.get()));
    if (catalog_key == key && catalog != nullptr) {
      return;
    }
    catalog = build_catalog_from_snapshot(*snapshot);
    catalog_key = key;
    mark_matches_dirty();
    return;
  }

  const std::string path =
      workspace.buffer.path.empty() ? workspace.active_file : workspace.buffer.path;
  if (path.empty()) {
    if (catalog_key.empty() && catalog != nullptr && catalog->empty()) {
      return;
    }
    catalog = std::make_shared<std::vector<SymbolCatalogEntry>>();
    catalog_key.clear();
    loaded_file.clear();
    mark_matches_dirty();
    return;
  }

  const std::string source = editor_buffer_joined_source(workspace.buffer);
  if (path != loaded_file && !source.empty() && is_indexed_source_path(path)) {
    tree_sitter_service().prepare_document(path, source);
  }
  if (!source.empty() && is_indexed_source_path(path) &&
      !tree_sitter_service().document_symbols_ready(path, source) &&
      tree_sitter_service().revision_for(path) == 0) {
    tree_sitter_service().prepare_document(path, source);
  }
  const uint64_t ts_rev = tree_sitter_service().revision_for(path);
  const std::string key = "file:" + path + "@" + std::to_string(ts_rev);
  if (catalog_key == key && catalog != nullptr) {
    return;
  }

  loaded_file = path;
  const std::vector<SymbolInfo> file_symbols =
      tree_sitter_service().symbols_for_file(path, source);
  if (file_symbols.empty() && !tree_sitter_service().document_symbols_ready(path, source)) {
    return;
  }
  catalog = build_catalog_from_file(file_symbols);
  catalog_key = key;
  mark_matches_dirty();
}

void SymbolPickerState::mark_matches_dirty() {
  matches_dirty = true;
}

void SymbolPickerState::schedule_search() {
  if (!catalog) {
    matches.clear();
    searching = false;
    return;
  }

  ++search_generation;
  searching = true;
  runner.start(search_generation, fuzzy_to_lower(query), catalog);
  if (search_notify_) {
    search_notify_();
  }
}

void SymbolPickerState::poll_search() {
  if (!matches_dirty && !runner.running()) {
    return;
  }

  std::vector<SymbolPickerMatch> fresh;
  if (runner.poll(search_generation, &fresh)) {
    matches = std::move(fresh);
    matches_dirty = false;
    searching = false;
    if (selected >= static_cast<int>(matches.size())) {
      selected = std::max(0, static_cast<int>(matches.size()) - 1);
    }
    return;
  }

  if (matches_dirty && catalog) {
    schedule_search();
  }
}

void SymbolPickerState::on_closed() {
  runner.cancel();
  searching = false;
  matches_dirty = true;
  matches.clear();
}

void SymbolPickerState::jump_to_selected(WorkspaceModel* workspace, FocusManagerState* focus) {
  if (matches.empty() || workspace == nullptr) {
    return;
  }
  selected = std::max(0, std::min(selected, static_cast<int>(matches.size()) - 1));
  const SymbolInfo& sym = matches[static_cast<std::size_t>(selected)].symbol;

  if (!sym.file.empty() && !workspace->root.empty()) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path absolute = fs::absolute(fs::path(workspace->root) / sym.file, ec);
    if (!ec) {
      workspace->open_file(absolute.string());
    }
  }

  workspace->record_cursor_jump();
  workspace->buffer.reset_to_single_cursor(std::max(0, sym.line - 1), 0);
  workspace->buffer.scroll = std::max(0, workspace->buffer.primary_line() - 2);
  workspace->buffer.view_token++;
  if (focus != nullptr) {
    focus->region = FocusRegion::Editor;
  }
  open = false;
  query.clear();
  selected = 0;
  on_closed();
}

Component MakeSymbolPickerOverlay(Component main, WorkspaceModel* workspace,
                                  SymbolPickerState* state, FocusManagerState* focus,
                                  std::shared_ptr<ISymbolProvider> symbols,
                                  SymbolWorkspaceIndexer* symbol_indexer) {
  return Renderer(
      CatchEvent(main, [workspace, state, focus, symbols, symbol_indexer](Event event) {
        if (!state->open) {
          return false;
        }

        state->sync_catalog(*workspace, symbols, symbol_indexer);
        if (event == Event::Custom) {
          state->poll_search();
          return false;
        }

        if (event == Event::Escape) {
          state->open = false;
          state->query.clear();
          state->selected = 0;
          state->on_closed();
          return true;
        }
        if (event == Event::Return) {
          state->jump_to_selected(workspace, focus);
          return true;
        }
        if (event == Event::ArrowDown) {
          if (!state->matches.empty()) {
            state->selected = std::min(state->selected + 1,
                                       static_cast<int>(state->matches.size()) - 1);
          }
          return true;
        }
        if (event == Event::ArrowUp) {
          state->selected = std::max(0, state->selected - 1);
          return true;
        }
        if (event_is_ctrl_o(event)) {
          if (!state->matches.empty()) {
            state->selected =
                (state->selected + 1) % static_cast<int>(state->matches.size());
          }
          return true;
        }
        if (event == Event::Backspace) {
          if (!state->query.empty()) {
            state->query.pop_back();
            state->selected = 0;
            state->mark_matches_dirty();
            state->schedule_search();
          }
          return true;
        }
        if (event.is_character()) {
          const std::string ch = event.character();
          if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) >= 32 &&
              static_cast<unsigned char>(ch[0]) < 127) {
            state->query += ch;
            state->selected = 0;
            state->mark_matches_dirty();
            state->schedule_search();
          }
          return true;
        }
        return true;
      }),
      [main, workspace, state, symbols, symbol_indexer] {
        Element base = main->Render();
        if (!state->open) {
          return base;
        }

        state->sync_catalog(*workspace, symbols, symbol_indexer);
        state->poll_search();
        if (state->runner.running()) {
          state->notify_search_tick();
        }

        std::string input_line = state->query;
        input_line.push_back('_');

        Elements rows;
        const int start = std::max(
            0, std::min(state->selected,
                        std::max(0, static_cast<int>(state->matches.size()) - kMaxRows)));
        const int end = std::min(static_cast<int>(state->matches.size()), start + kMaxRows);
        for (int i = start; i < end; ++i) {
          rows.push_back(render_symbol_row(state->matches[static_cast<std::size_t>(i)],
                                           i == state->selected, kTextWidth));
        }
        if (rows.empty()) {
          const bool indexing =
              symbol_indexer != nullptr && symbol_indexer->scanning() &&
              (state->catalog == nullptr || state->catalog->empty());
          const char* key = indexing ? "common.indexing" : "common.no_matches";
          rows.push_back(text(i18n::tr(key)) | color(theme::Muted()));
        }

        Element dialog = ModalWindow(
            text(i18n::tr("picker.symbol.title")) | color(theme::Accent()),
            vbox({ModalInputLine(input_line),
                  separator(),
                  vbox(std::move(rows)) | frame | vscroll_indicator |
                      bgcolor(theme::PanelBg())}) |
                size(WIDTH, EQUAL, kModalWidth));

        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tgdb
