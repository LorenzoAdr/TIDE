#pragma once

#include <string>
#include <vector>

namespace tgdb {

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

std::vector<Diagnostic> diagnostics_on_line(const DocumentDiagnostics& doc, int line);
std::string diagnostic_severity_label(DiagnosticSeverity severity);
std::string build_diagnostic_suffix(const std::vector<Diagnostic>& items, int max_chars);

}  // namespace tgdb
