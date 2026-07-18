#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "lsp/gfortran_diagnostics.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <regex>
#include <unistd.h>

#include "util/shell_utils.hpp"

namespace fs = std::filesystem;

namespace tgdb {
namespace {

bool is_fixed_form_extension(const std::string& ext) {
  return ext == ".f" || ext == ".for" || ext == ".ftn" || ext == ".F" || ext == ".FOR" ||
         ext == ".FTN";
}

std::string temp_suffix_for_path(const std::string& absolute_path) {
  const std::string ext = fs::path(absolute_path).extension().string();
  if (ext.empty()) {
    return ".f90";
  }
  // Keep the original extension so gfortran classifies free/fixed form and preprocessing
  // the same way as the real file (a bare mkstemp path is treated as a linker input!).
  return ext;
}

DiagnosticSeverity severity_from_label(const std::string& label) {
  if (label == "Error" || label == "Fatal Error" || label == "internal compiler error") {
    return DiagnosticSeverity::kError;
  }
  if (label == "Warning") {
    return DiagnosticSeverity::kWarning;
  }
  if (label == "Note") {
    return DiagnosticSeverity::kInfo;
  }
  return DiagnosticSeverity::kHint;
}

bool path_refers_to_source(const std::string& reported, const std::string& source_path) {
  if (reported == source_path) {
    return true;
  }
  const std::string base = fs::path(source_path).filename().string();
  if (reported == base) {
    return true;
  }
  if (fs::path(reported).filename().string() == base) {
    return true;
  }
  return reported.size() >= base.size() &&
         reported.compare(reported.size() - base.size(), base.size(), base) == 0;
}

}  // namespace

std::vector<Diagnostic> parse_gfortran_stderr(const std::string& stderr_text,
                                              const std::string& source_path) {
  std::vector<Diagnostic> out;
  // Examples:
  //   file.f90:4:7:
  //   file.f90:4.7:
  //   file.f90:4:7: Error: message
  static const std::regex kLoc(
      R"(^((?:[A-Za-z]:)?[^:\n]+):([0-9]+)(?:[:.]([0-9]+))?(?::\s*(?:(Error|Warning|Fatal Error|Note|internal compiler error):\s*(.*))?)?$)");
  static const std::regex kMsg(
      R"(^(Error|Warning|Fatal Error|Note|internal compiler error):\s*(.*)$)");

  int pending_line = -1;
  int pending_col = 0;

  std::size_t start = 0;
  while (start <= stderr_text.size()) {
    const std::size_t end = stderr_text.find('\n', start);
    std::string line = stderr_text.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    start = end == std::string::npos ? stderr_text.size() + 1 : end + 1;
    if (line.empty()) {
      continue;
    }

    std::smatch match;
    if (std::regex_match(line, match, kLoc)) {
      if (!path_refers_to_source(match[1].str(), source_path)) {
        pending_line = -1;
        continue;
      }
      pending_line = std::max(0, std::stoi(match[2].str()) - 1);
      pending_col = match[3].matched ? std::max(0, std::stoi(match[3].str()) - 1) : 0;
      if (match[4].matched) {
        Diagnostic diag;
        diag.line = pending_line;
        diag.start_col = pending_col;
        diag.end_col = pending_col + 1;
        diag.severity = severity_from_label(match[4].str());
        diag.message = match[5].str();
        diag.source = "gfortran";
        if (!diag.message.empty()) {
          out.push_back(std::move(diag));
        }
        pending_line = -1;
      }
      continue;
    }
    if (pending_line >= 0 && std::regex_match(line, match, kMsg)) {
      Diagnostic diag;
      diag.line = pending_line;
      diag.start_col = pending_col;
      diag.end_col = pending_col + 1;
      diag.severity = severity_from_label(match[1].str());
      diag.message = match[2].str();
      diag.source = "gfortran";
      if (!diag.message.empty()) {
        out.push_back(std::move(diag));
      }
      pending_line = -1;
    }
  }
  return out;
}

std::optional<DocumentDiagnostics> run_gfortran_diagnostics(const std::string& absolute_path,
                                                            const std::string& text,
                                                            const std::string& gfortran_path) {
  if (absolute_path.empty() || gfortran_path.empty()) {
    return std::nullopt;
  }

  const std::string ext = fs::path(absolute_path).extension().string();
  const std::string suffix = temp_suffix_for_path(absolute_path);
  std::string mk_template = (fs::temp_directory_path() / ("tgdb-gf-XXXXXX" + suffix)).string();
  std::vector<char> mk_buf(mk_template.begin(), mk_template.end());
  mk_buf.push_back('\0');
  const int fd = mkstemps(mk_buf.data(), static_cast<int>(suffix.size()));
  if (fd < 0) {
    return std::nullopt;
  }
  const std::string temp_path(mk_buf.data());
  FILE* out = fdopen(fd, "w");
  if (out == nullptr) {
    close(fd);
    fs::remove(temp_path);
    return std::nullopt;
  }
  if (!text.empty()) {
    fwrite(text.data(), 1, text.size(), out);
  }
  fclose(out);

  std::string cmd = shell_quote(gfortran_path) + " -fsyntax-only -fmax-errors=20";
  if (is_fixed_form_extension(ext)) {
    cmd += " -ffixed-form";
  } else {
    cmd += " -ffree-form";
  }
  cmd += " " + shell_quote(temp_path) + " 2>&1";

  const std::string output = run_shell_capture(cmd, 8);
  fs::remove(temp_path);

  DocumentDiagnostics doc;
  doc.path = absolute_path;
  doc.items = parse_gfortran_stderr(output, temp_path);
  return doc;
}

}  // namespace tgdb
