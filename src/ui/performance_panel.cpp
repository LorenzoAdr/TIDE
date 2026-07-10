#include "ui/performance_panel.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/theme.hpp"
#include "i18n/tr.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

int visible_height(int height) {
  return std::max(4, height);
}

int visible_width(int width) {
  return std::max(20, width);
}

std::string format_mib(std::size_t kb) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(1) << (static_cast<double>(kb) / 1024.0) << " MiB";
  return stream.str();
}

std::string format_percent(double value) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(1) << value << "%";
  return stream.str();
}

std::string format_fps(double fps) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(1) << fps;
  return stream.str();
}

std::string summarize_lsp_workers(const std::vector<ThreadSample>& workers) {
  bool has_clangd = false;
  std::vector<std::string> names;
  for (const ThreadSample& worker : workers) {
    if (worker.is_child_process) {
      if (worker.comm == "clangd" || worker.comm.rfind("clangd", 0) == 0) {
        has_clangd = true;
      }
      continue;
    }
    if (worker.comm.rfind("lsp-", 0) == 0) {
      names.push_back(worker.comm);
    }
  }
  if (!has_clangd && names.empty()) {
    return "";
  }
  std::ostringstream out;
  out << i18n::tr("panel.performance.lsp_prefix");
  if (has_clangd) {
    out << " clangd";
  }
  for (const std::string& name : names) {
    out << " " << name;
  }
  return out.str();
}

Color usage_color(double ratio) {
  if (ratio >= 0.85) {
    return theme::Error();
  }
  if (ratio >= 0.60) {
    return theme::Warning();
  }
  return theme::Accent();
}

Element render_usage_bar(double ratio, int width) {
  ratio = std::clamp(ratio, 0.0, 1.0);
  const int filled = static_cast<int>(std::lround(ratio * static_cast<double>(width)));
  std::string bar;
  bar.reserve(static_cast<std::size_t>(width));
  for (int i = 0; i < width; ++i) {
    bar += (i < filled ? "█" : "░");
  }
  return text(bar) | color(usage_color(ratio));
}

Element render_labeled_bar(const std::string& label, double used_kb, double total_kb, int bar_width) {
  const double ratio = total_kb > 0.0 ? used_kb / total_kb : 0.0;
  std::ostringstream value;
  value << format_mib(static_cast<std::size_t>(used_kb)) << " / " << format_mib(static_cast<std::size_t>(total_kb))
        << "  " << format_percent(ratio * 100.0);
  return hbox({
      text(label) | size(WIDTH, EQUAL, 10) | color(theme::Muted()),
      render_usage_bar(ratio, bar_width) | size(WIDTH, EQUAL, bar_width),
      text(" ") | size(WIDTH, EQUAL, 1),
      text(value.str()) | color(theme::Header()),
  });
}

Element render_core_grid(const std::vector<CpuCoreSample>& cores, int core_count,
                         double total_percent, int panel_width) {
  if (cores.empty()) {
    return text(i18n::tr("panel.performance.no_core_data")) | color(theme::Muted());
  }

  const int columns = std::clamp(panel_width / 18, 1, 4);
  const int mini_bar = std::max(4, (panel_width / columns) - 10);
  Elements rows;

  std::ostringstream header;
  header << i18n::tr_fmt("panel.performance.cpu_header", {std::to_string(core_count), format_percent(total_percent)});
  rows.push_back(text(header.str()) | color(theme::Muted()));

  for (std::size_t i = 0; i < cores.size(); i += static_cast<std::size_t>(columns)) {
    Elements cells;
    for (int col = 0; col < columns; ++col) {
      const std::size_t index = i + static_cast<std::size_t>(col);
      if (index >= cores.size()) {
        break;
      }
      const CpuCoreSample& core = cores[index];
      const int core_id = static_cast<int>(index);
      const double ratio = std::clamp(core.usage_percent / 100.0, 0.0, 1.0);
      std::ostringstream label;
      label << "C" << core_id;
      cells.push_back(hbox({
          text(label.str()) | size(WIDTH, EQUAL, 3) | color(theme::Muted()),
          render_usage_bar(ratio, mini_bar),
          text(format_percent(core.usage_percent)) | size(WIDTH, EQUAL, 6) | color(theme::Header()),
      }));
    }
    rows.push_back(hbox(std::move(cells)));
  }
  return vbox(std::move(rows));
}

Element render_process_section(const PerformanceSnapshot& snapshot, int body_height,
                             int panel_width, PerformancePanelState* state) {
  (void)panel_width;
  Elements lines;

  std::ostringstream summary;
  summary << i18n::tr_fmt("panel.performance.summary_process",
                          {format_fps(snapshot.fps), format_mib(snapshot.process.rss_kb),
                           format_percent(snapshot.process.cpu_percent),
                           std::to_string(snapshot.process.thread_count),
                           summarize_lsp_workers(snapshot.process.threads)});
  summary << "  (CPU/hilo: hilo perf-sampler, % = 1 nucleo)";
  lines.push_back(text(summary.str()) | color(theme::Header()));

  constexpr int kHeaderLines = 2;
  const int table_height = std::max(1, body_height - kHeaderLines);
  const int total_threads = static_cast<int>(snapshot.process.threads.size());
  const int max_scroll = std::max(0, total_threads - table_height);
  state->thread_scroll = std::clamp(state->thread_scroll, 0, max_scroll);

  lines.push_back(hbox({
      text(i18n::tr("panel.performance.col.tid_pid")) | size(WIDTH, EQUAL, 8) | color(theme::Muted()) | bold,
      text(i18n::tr("panel.performance.col.name")) | size(WIDTH, EQUAL, 14) | color(theme::Muted()) | bold,
      text(i18n::tr("panel.performance.col.cpu")) | size(WIDTH, EQUAL, 7) | color(theme::Muted()) | bold,
      text(i18n::tr("panel.performance.col.ram")) | color(theme::Muted()) | bold,
  }));

  const int start = state->thread_scroll;
  const int end = std::min(total_threads, start + table_height);
  if (total_threads == 0) {
    lines.push_back(text(i18n::tr("panel.performance.no_thread_data")) |
                    color(theme::Muted()));
  } else {
    for (int i = start; i < end; ++i) {
      const ThreadSample& thread = snapshot.process.threads[static_cast<std::size_t>(i)];
      std::string name = thread.comm;
      if (thread.is_child_process) {
        name += i18n::tr("panel.performance.thread_proc");
        if (thread.child_thread_count > 0) {
          name += i18n::tr_fmt("panel.performance.thread_th", {std::to_string(thread.child_thread_count)});
        }
      }
      if (name.size() > 14) {
        name = name.substr(0, 14);
      }
      std::string ram = thread.is_child_process ? format_mib(thread.rss_kb) : "-";
      lines.push_back(hbox({
          text(std::to_string(thread.tid)) | size(WIDTH, EQUAL, 8) | color(theme::Header()),
          text(name) | size(WIDTH, EQUAL, 14) |
              color(thread.is_child_process ? theme::Warning() : theme::Muted()),
          text(format_percent(thread.cpu_percent)) | size(WIDTH, EQUAL, 7) | color(theme::Accent()),
          text(ram) | color(theme::Header()),
      }));
    }
  }

  return vbox(std::move(lines)) | flex;
}

Element render_system_section(const PerformanceSnapshot& snapshot, int body_height, int panel_width) {
  if (!snapshot.system.available) {
    return text(i18n::tr("panel.performance.unavailable")) |
           color(theme::Muted());
  }

  Elements lines;
  lines.push_back(render_core_grid(snapshot.system.cores, snapshot.system.core_count,
                                   snapshot.system.cpu_total_percent, panel_width));

  const int used_lines = 2 + (snapshot.system.core_count + 3) / 4;
  if (body_height > used_lines + 3) {
    lines.push_back(separator() | color(theme::AccentDim()));
    lines.push_back(render_labeled_bar(i18n::tr("panel.performance.label.ram"), static_cast<double>(snapshot.system.mem_used_kb),
                                       static_cast<double>(snapshot.system.mem_total_kb),
                                       std::max(8, panel_width / 3)));
    if (snapshot.system.swap_total_kb > 0) {
      lines.push_back(render_labeled_bar(i18n::tr("panel.performance.label.swap"), static_cast<double>(snapshot.system.swap_used_kb),
                                         static_cast<double>(snapshot.system.swap_total_kb),
                                         std::max(8, panel_width / 3)));
    }
  }

  return vbox(std::move(lines)) | flex;
}

Element render_ui_thread_section(const UiPerfSnapshot& ui, int panel_width,
                                 const std::string& dump_path) {
  (void)panel_width;
  Elements lines;
  std::ostringstream header;
  header << "UI thread  paint " << format_fps(ui.paint_fps) << " fps  tick "
         << format_fps(ui.tick_fps) << " fps  wasted "
         << format_percent(ui.ticks_without_paint_ratio * 100.0) << "  phase "
         << ui_activity_phase_label(ui.activity_phase) << " " << ui.ms_in_phase << "ms";
  lines.push_back(text(header.str()) | color(theme::Header()));
  if (ui.activity_phase == UiActivityPhase::kInhibited && !dump_path.empty()) {
    lines.push_back(text("inhibido: ver /tmp/tgdb-perf-<pid>.log (100ms, hilo perf-sampler)") |
                    color(theme::Muted()));
  }

  if (!dump_path.empty()) {
    lines.push_back(text("dump: " + dump_path) | color(theme::Muted()));
  }
  std::ostringstream counts;
  counts << "events  key=" << ui.event_counts[static_cast<std::size_t>(UiPerfEventKind::kKeyboard)]
         << " click="
         << ui.event_counts[static_cast<std::size_t>(UiPerfEventKind::kMouseClick)] << " wheel="
         << ui.event_counts[static_cast<std::size_t>(UiPerfEventKind::kMouseWheel)] << " paint="
         << ui.paints_total << " tick=" << ui.ticks_total;
  lines.push_back(text(counts.str()) | color(theme::Muted()));

  for (const UiPerfPhaseStats& phase : ui.tick_phases) {
    std::ostringstream row;
    const double p95_ms = static_cast<double>(phase.p95_us) / 1000.0;
    row << phase.name << " p95=" << std::fixed << std::setprecision(2) << p95_ms << "ms n="
        << phase.samples;
    lines.push_back(text(row.str()) | color(theme::Muted()));
  }
  return vbox(std::move(lines));
}

}  // namespace

Element RenderPerformancePanel(PerformanceSampler* sampler, UiPerfMonitor* ui_perf,
                               PerformancePanelState* state, int width, int height) {
  const int total_height = visible_height(height);
  const int panel_width = visible_width(width);

  PerformanceSnapshot snapshot;
  if (sampler != nullptr) {
    snapshot = sampler->snapshot();
  }
  UiPerfSnapshot ui_snapshot;
  if (ui_perf != nullptr) {
    ui_snapshot = ui_perf->snapshot();
  }

  const std::string dump_path = sampler != nullptr ? sampler->dump_file_path() : std::string{};
  constexpr int kUiSectionLines = 10;
  constexpr int kMinHeightForSystem = 12;
  const bool show_system = total_height >= kMinHeightForSystem + kUiSectionLines;
  const int ui_height = std::min(kUiSectionLines, std::max(2, total_height / 6));
  const int process_height =
      show_system ? std::max(4, (total_height - ui_height) * 2 / 3)
                  : std::max(3, total_height - ui_height - 1);

  Element process_body =
      render_process_section(snapshot, process_height, panel_width, state);

  Elements layout;
  layout.push_back(render_ui_thread_section(ui_snapshot, panel_width, dump_path) |
                   size(HEIGHT, EQUAL, ui_height) | bgcolor(theme::PanelBg()));
  layout.push_back(separator() | color(theme::AccentDim()) | size(HEIGHT, EQUAL, 1));
  layout.push_back(text(i18n::tr("panel.performance.tab.process")) | bold | color(theme::Accent()) | bgcolor(theme::TabIdle()) |
                   size(HEIGHT, EQUAL, 1));
  layout.push_back(process_body | size(HEIGHT, EQUAL, process_height) | bgcolor(theme::PanelBg()));

  if (show_system) {
    const int system_height = std::max(2, total_height - process_height - 2);
    Element system_body =
        render_system_section(snapshot, system_height, panel_width);
    layout.push_back(separator() | color(theme::AccentDim()) | size(HEIGHT, EQUAL, 1));
    layout.push_back(text(i18n::tr("panel.performance.tab.system")) | bold | color(theme::Accent()) | bgcolor(theme::TabIdle()) |
                     size(HEIGHT, EQUAL, 1));
    layout.push_back(system_body | size(HEIGHT, EQUAL, system_height) | bgcolor(theme::PanelBg()));
  }

  return vbox(std::move(layout)) | flex | bgcolor(theme::PanelBg());
}

Component MakePerformancePanel(PerformanceSampler* sampler, UiPerfMonitor* ui_perf,
                               std::shared_ptr<PerformancePanelState> state) {
  return CatchEvent(Renderer([] { return text(""); }), [sampler, ui_perf, state](const Event& event) {
    if (state == nullptr) {
      return false;
    }
    if (event == Event::Character('j') || event == Event::ArrowDown) {
      state->thread_scroll += 1;
      return true;
    }
    if (event == Event::Character('k') || event == Event::ArrowUp) {
      state->thread_scroll = std::max(0, state->thread_scroll - 1);
      return true;
    }
    (void)sampler;
    (void)ui_perf;
    return false;
  });
}

}  // namespace tgdb
