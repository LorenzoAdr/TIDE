#pragma once

#include <optional>
#include <string>

#include "lsp/diagnostics.hpp"

namespace tuide {

// Run `gfortran -fsyntax-only` on buffer contents (temp file) and parse stderr into
// diagnostics. Returns nullopt if gfortran is unavailable or the run fails to start.
std::optional<DocumentDiagnostics> run_gfortran_diagnostics(const std::string& absolute_path,
                                                            const std::string& text,
                                                            const std::string& gfortran_path);

// Parse gfortran/ifort-style diagnostic lines (exposed for tests).
std::vector<Diagnostic> parse_gfortran_stderr(const std::string& stderr_text,
                                              const std::string& source_path);

}  // namespace tuide
