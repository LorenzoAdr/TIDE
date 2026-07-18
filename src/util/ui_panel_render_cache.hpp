#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "ftxui/dom/elements.hpp"

namespace tuide {

enum class UiPanelId : std::uint8_t {
  FileTree = 0,
  EditorCenter = 1,
  RightSidebar = 2,
  Console = 3,
  kCount
};

struct UiPanelRenderCacheEntry {
  ftxui::Element cached = ftxui::text("");
  uint64_t dirty_generation = 1;
  uint64_t last_painted_generation = 0;
  uint64_t last_colors_revision = 0;
};

struct UiPanelRenderCache {
  std::array<UiPanelRenderCacheEntry, static_cast<std::size_t>(UiPanelId::kCount)> entries;

  void mark_dirty(UiPanelId panel) {
    entries[static_cast<std::size_t>(panel)].dirty_generation++;
  }

  void mark_all_dirty() {
    for (auto& entry : entries) {
      entry.dirty_generation++;
    }
  }

  ftxui::Element render(UiPanelId panel, uint64_t colors_revision,
                        const std::function<ftxui::Element()>& build) {
    UiPanelRenderCacheEntry& entry = entries[static_cast<std::size_t>(panel)];
    if (entry.dirty_generation != entry.last_painted_generation ||
        entry.last_colors_revision != colors_revision) {
      entry.cached = build();
      entry.last_painted_generation = entry.dirty_generation;
      entry.last_colors_revision = colors_revision;
    }
    return entry.cached;
  }
};

}  // namespace tuide
