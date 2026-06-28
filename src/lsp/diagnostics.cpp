#include "lsp/diagnostics.hpp"

namespace tgdb {

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
      return "Error";
    case DiagnosticSeverity::kWarning:
      return "Aviso";
    case DiagnosticSeverity::kInfo:
      return "Info";
    case DiagnosticSeverity::kHint:
    default:
      return "Sugerencia";
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
