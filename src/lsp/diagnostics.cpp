#include "lsp/diagnostics.hpp"

#include <chrono>

#include "lsp/lsp_sync.hpp"
#include "lsp/lsp_uri.hpp"
#include "symbols/symbol_provider.hpp"
#include "util/include_tree.hpp"
#include "i18n/tr.hpp"

namespace tgdb {

namespace {

int64_t steady_now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

bool diagnostics_display_allowed(const int64_t last_content_edit_ms, ISymbolProvider* symbols,
                                 const std::string& path, const bool lsp_ui_allowed) {
  if (!lsp_ui_allowed) {
    return false;
  }
  if (path.empty() || symbols == nullptr || !symbols->supports_diagnostics()) {
    return false;
  }
  if (last_content_edit_ms > 0 &&
      steady_now_ms() - last_content_edit_ms < kLspDiagnosticsDisplayDebounceMs) {
    return false;
  }
  return true;
}

bool diagnostics_reveal_allowed(const int64_t last_content_edit_ms, ISymbolProvider* symbols,
                                const std::string& path, const bool lsp_ui_allowed) {
  if (!lsp_ui_allowed || path.empty() || symbols == nullptr || !symbols->supports_diagnostics()) {
    return false;
  }
  if (symbols->diagnostics_display_ready(path)) {
    return true;
  }
  return diagnostics_display_allowed(last_content_edit_ms, symbols, path, lsp_ui_allowed);
}

int count_errors(const DocumentDiagnostics& doc) {
  int n = 0;
  for (const auto& item : doc.items) {
    if (item.severity == DiagnosticSeverity::kError) {
      ++n;
    }
  }
  return n;
}

int count_warnings(const DocumentDiagnostics& doc) {
  int n = 0;
  for (const auto& item : doc.items) {
    if (item.severity == DiagnosticSeverity::kWarning) {
      ++n;
    }
  }
  return n;
}

void count_workspace_diagnostics(const std::vector<DocumentDiagnostics>& docs, int* errors,
                                 int* warnings) {
  if (errors != nullptr) {
    *errors = 0;
  }
  if (warnings != nullptr) {
    *warnings = 0;
  }
  for (const auto& doc : docs) {
    if (errors != nullptr) {
      *errors += count_errors(doc);
    }
    if (warnings != nullptr) {
      *warnings += count_warnings(doc);
    }
  }
}

std::vector<DocumentDiagnostics> filter_diagnostics_by_paths(
    const std::vector<DocumentDiagnostics>& docs,
    const std::unordered_set<std::string>& allowed_paths) {
  if (allowed_paths.empty()) {
    return {};
  }
  std::vector<DocumentDiagnostics> out;
  out.reserve(docs.size());
  for (const auto& doc : docs) {
    const std::string key = normalize_lsp_path(doc.path);
    if (allowed_paths.find(key) == allowed_paths.end()) {
      continue;
    }
    out.push_back(doc);
  }
  return out;
}

std::vector<DocumentDiagnostics> diagnostics_for_translation_unit(
    const std::vector<DocumentDiagnostics>& all, const std::string& active_file,
    const std::string& workspace_root,
    const std::vector<std::string>& workspace_relative_files,
    const std::string& active_file_text_override) {
  if (active_file.empty()) {
    return {};
  }
  const auto allowed = build_include_tree(active_file, workspace_root, workspace_relative_files,
                                          active_file_text_override);
  return filter_diagnostics_by_paths(all, allowed);
}

std::vector<Diagnostic> diagnostics_on_line(const DocumentDiagnostics& doc, int line) {
  std::vector<Diagnostic> out;
  for (const auto& item : doc.items) {
    if (item.line == line) {
      out.push_back(item);
    }
  }
  return out;
}

std::string diagnostic_severity_label(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::kError:
      return i18n::tr("diagnostic.severity.error");
    case DiagnosticSeverity::kWarning:
      return i18n::tr("diagnostic.severity.warning");
    case DiagnosticSeverity::kInfo:
      return i18n::tr("diagnostic.severity.info");
    case DiagnosticSeverity::kHint:
    default:
      return i18n::tr("diagnostic.severity.hint");
  }
}

std::string build_diagnostic_suffix(const std::vector<Diagnostic>& items, int max_chars) {
  if (items.empty() || max_chars <= 2) {
    return {};
  }
  std::string suffix = "  ";
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i > 0) {
      suffix += " | ";
    }
    suffix += items[i].message;
  }
  if (static_cast<int>(suffix.size()) > max_chars) {
    suffix.resize(static_cast<std::size_t>(max_chars - 1));
    suffix.push_back('\xE2');
    suffix.push_back('\x80');
    suffix.push_back('\xA6');
  }
  return suffix;
}

}  // namespace tgdb
