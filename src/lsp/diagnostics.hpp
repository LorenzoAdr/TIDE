#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace tgdb {

class ISymbolProvider;

enum class DiagnosticSeverity { kError = 1, kWarning = 2, kInfo = 3, kHint = 4 };

struct Diagnostic {
  int line = 0;
  int start_col = 0;
  int end_col = 0;
  std::string message;
  std::string source;
  DiagnosticSeverity severity = DiagnosticSeverity::kError;
};

struct DocumentDiagnostics {
  std::string path;
  std::vector<Diagnostic> items;
};

int count_errors(const DocumentDiagnostics& doc);
int count_warnings(const DocumentDiagnostics& doc);
void count_workspace_diagnostics(const std::vector<DocumentDiagnostics>& docs, int* errors,
                                 int* warnings);

std::vector<DocumentDiagnostics> filter_diagnostics_by_paths(
    const std::vector<DocumentDiagnostics>& docs,
    const std::unordered_set<std::string>& allowed_paths);

std::vector<DocumentDiagnostics> diagnostics_for_translation_unit(
    const std::vector<DocumentDiagnostics>& all, const std::string& active_file,
    const std::string& workspace_root,
    const std::vector<std::string>& workspace_relative_files,
    const std::string& active_file_text_override = {});

std::vector<Diagnostic> diagnostics_on_line(const DocumentDiagnostics& doc, int line);
std::string diagnostic_severity_label(DiagnosticSeverity severity);
std::string build_diagnostic_suffix(const std::vector<Diagnostic>& items, int max_chars);

bool diagnostics_display_allowed(int64_t last_content_edit_ms, ISymbolProvider* symbols,
                                 const std::string& path);

}  // namespace tgdb
