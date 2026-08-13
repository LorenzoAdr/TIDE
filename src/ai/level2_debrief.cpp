#include "ai/level2_debrief.hpp"

#include <fstream>
#include <sstream>
#include <string_view>

#include <nlohmann/json.hpp>

#include "ai/level2_session.hpp"

namespace tuide {
namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

std::string trim_copy(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n' ||
                        s.front() == '\r')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' ||
                        s.back() == '\r')) {
    s.remove_suffix(1);
  }
  return std::string(s);
}

bool contains_ci(std::string_view hay, std::string_view needle) {
  if (needle.empty()) {
    return true;
  }
  auto lower = [](char c) {
    return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  };
  for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
    bool ok = true;
    for (std::size_t j = 0; j < needle.size(); ++j) {
      if (lower(hay[i + j]) != lower(needle[j])) {
        ok = false;
        break;
      }
    }
    if (ok) {
      return true;
    }
  }
  return false;
}

void push_unique(std::vector<Level2DebriefFact>& facts, std::string tag, std::string detail) {
  for (const auto& f : facts) {
    if (f.tag == tag && f.detail == detail) {
      return;
    }
  }
  facts.push_back(Level2DebriefFact{std::move(tag), std::move(detail)});
}

std::string outcome_from_result(const Level2AutonomousLoopResult& result) {
  if (result.phase == "cancelled" || contains_ci(result.error, "cancel")) {
    return "cancelled";
  }
  if (result.phase == "clarify") {
    return "clarify";
  }
  if (contains_ci(result.summary, "FAIL compile") || contains_ci(result.error, "FAIL compile")) {
    return "compile_fail";
  }
  if (result.ok && result.phase == "done") {
    return "success";
  }
  if (!result.ok && !result.error.empty()) {
    return "error";
  }
  if (result.phase == "done") {
    return "success";
  }
  return "incomplete";
}

void collect_from_state(const std::string& workspace_root, Level2Debrief& out) {
  const std::string raw = read_file(Level2Session::state_path(workspace_root));
  if (raw.empty()) {
    return;
  }
  try {
    const auto j = nlohmann::json::parse(raw);
    const int edit_attempt = j.value("edit_attempt", 0);
    const int compile_attempt = j.value("compile_attempt", 0);
    const std::string last = j.value("last_action", "");
    if (edit_attempt > 0) {
      push_unique(out.facts, "edit_attempts",
                  "intentos de edit: " + std::to_string(edit_attempt));
    }
    if (compile_attempt > 0) {
      push_unique(out.facts, "compile_attempts",
                  "intentos de compile: " + std::to_string(compile_attempt));
    }
    if (!last.empty()) {
      push_unique(out.facts, "last_action", "última acción: " + last);
    }
  } catch (...) {
  }
}

void collect_from_session_observations(const std::string& workspace_root, Level2Debrief& out) {
  const std::string body = read_file(Level2Session::session_path(workspace_root));
  if (body.empty()) {
    return;
  }

  for (std::size_t pos = 0; (pos = body.find("outline:", pos)) != std::string::npos;) {
    const auto eol = body.find('\n', pos);
    const std::string line =
        trim_copy(body.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos));
    pos = eol == std::string::npos ? body.size() : eol + 1;

    std::string path;
    int symbols = -1;
    std::string note;
    {
      const auto sym = line.find("symbols=");
      if (sym == std::string::npos) {
        continue;
      }
      path = trim_copy(line.substr(8, sym - 8));
      std::size_t i = sym + 8;
      while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
        ++i;
      }
      try {
        symbols = std::stoi(line.substr(sym + 8, i - (sym + 8)));
      } catch (...) {
        continue;
      }
      const auto paren = line.find('(', i);
      if (paren != std::string::npos) {
        note = trim_copy(line.substr(paren));
      }
    }
    std::ostringstream detail;
    detail << "file_outline(" << path << ") → symbols=" << symbols;
    if (!note.empty()) {
      detail << " " << note;
    }
    push_unique(out.facts, symbols == 0 ? "outline_empty" : "outline", detail.str());
  }

  for (std::size_t pos = 0; (pos = body.find("— edit_feedback", pos)) != std::string::npos;) {
    const auto head_end = body.find('\n', pos);
    std::size_t cursor = head_end == std::string::npos ? body.size() : head_end + 1;
    std::string err_line;
    while (cursor < body.size()) {
      const auto eol = body.find('\n', cursor);
      const std::string line =
          trim_copy(body.substr(cursor, eol == std::string::npos ? std::string::npos : eol - cursor));
      cursor = eol == std::string::npos ? body.size() : eol + 1;
      if (line.empty() || line.rfind("###", 0) == 0) {
        if (line.rfind("###", 0) == 0) {
          break;
        }
        continue;
      }
      if (line.rfind("```", 0) == 0) {
        continue;
      }
      err_line = line;
      break;
    }
    if (!err_line.empty()) {
      if (err_line.rfind("error:", 0) == 0) {
        err_line = trim_copy(err_line.substr(6));
      }
      push_unique(out.facts, "edit_fail", "edit falló: " + err_line);
    }
    pos = cursor;
  }

  if (body.find("— compile_feedback") != std::string::npos ||
      body.find("FAIL compile") != std::string::npos) {
    push_unique(out.facts, "compile_fail", "compile falló (ver Observations / compile.log)");
  }
  if (body.find("OK compile") != std::string::npos) {
    push_unique(out.facts, "compile_ok", "compile OK");
  }

  for (std::size_t pos = 0; (pos = body.find("— clarify", pos)) != std::string::npos;) {
    const auto head_end = body.find('\n', pos);
    std::size_t cursor = head_end == std::string::npos ? body.size() : head_end + 1;
    while (cursor < body.size()) {
      const auto eol = body.find('\n', cursor);
      const std::string line =
          trim_copy(body.substr(cursor, eol == std::string::npos ? std::string::npos : eol - cursor));
      cursor = eol == std::string::npos ? body.size() : eol + 1;
      if (line.empty() || line.rfind("_", 0) == 0 || line.rfind("###", 0) == 0) {
        if (line.rfind("###", 0) == 0) {
          break;
        }
        continue;
      }
      push_unique(out.facts, "clarify", "clarify: " + line);
      break;
    }
    pos = cursor;
  }
}

void collect_from_trace(const std::string& workspace_root, Level2Debrief& out) {
  const std::string path = Level2Session::trace_path(workspace_root);
  std::ifstream in(path);
  if (!in) {
    return;
  }
  int tools = 0;
  int edits_ok = 0;
  int edits_fail = 0;
  int compiles_ok = 0;
  int compiles_fail = 0;
  long long last_compile_ms = -1;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    try {
      const auto j = nlohmann::json::parse(line);
      const std::string ev = j.value("event", "");
      if (ev == "tool") {
        ++tools;
        const std::string name = j.value("name", "");
        const bool ok = j.value("ok", true);
        if (!ok) {
          push_unique(out.facts, "tool_fail",
                      "tool falló: " + (name.empty() ? std::string("(desconocido)") : name));
        }
      } else if (ev == "edit") {
        ++edits_ok;
      } else if (ev == "edit_fail") {
        ++edits_fail;
        const std::string err = j.value("error", "");
        if (!err.empty()) {
          push_unique(out.facts, "edit_fail", "edit falló: " + err);
        } else {
          push_unique(out.facts, "edit_fail", "edit falló (search no único/no encontrado)");
        }
      } else if (ev == "compile_ok") {
        ++compiles_ok;
        last_compile_ms = j.value("ms", -1);
      } else if (ev == "compile_fail") {
        ++compiles_fail;
        last_compile_ms = j.value("ms", -1);
        const int exit_code = j.value("exit", -1);
        std::ostringstream d;
        d << "compile falló exit=" << exit_code;
        if (last_compile_ms >= 0) {
          d << " (" << last_compile_ms << " ms)";
        }
        push_unique(out.facts, "compile_fail", d.str());
      } else if (ev == "need_clarification") {
        push_unique(out.facts, "clarify", "sesión cerrada en clarify (hace falta más detalle)");
      }
    } catch (...) {
    }
  }
  if (tools > 0) {
    push_unique(out.facts, "tools", "tools ejecutados: " + std::to_string(tools));
  }
  if (edits_ok > 0) {
    push_unique(out.facts, "edits_ok", "edits OK: " + std::to_string(edits_ok));
  }
  if (edits_fail > 0) {
    push_unique(out.facts, "edits_fail_count",
                "edits fallidos: " + std::to_string(edits_fail));
  }
  if (compiles_ok > 0) {
    std::ostringstream d;
    d << "compile OK";
    if (last_compile_ms >= 0 && compiles_fail == 0) {
      d << " (" << last_compile_ms << " ms)";
    }
    push_unique(out.facts, "compile_ok", d.str());
  }
}

}  // namespace

Level2Debrief build_level2_debrief(const std::string& workspace_root,
                                   const Level2AutonomousLoopResult& result) {
  Level2Debrief out;
  out.outcome_tag = outcome_from_result(result);

  {
    std::ostringstream head;
    head << "resultado: " << out.outcome_tag << "  phase=" << result.phase
         << "  steps=" << result.steps;
    push_unique(out.facts, "outcome", head.str());
  }
  if (!result.summary.empty()) {
    push_unique(out.facts, "summary", "summary: " + result.summary);
  }
  if (!result.error.empty()) {
    push_unique(out.facts, "error", "error: " + result.error);
  }

  collect_from_state(workspace_root, out);
  collect_from_trace(workspace_root, out);
  collect_from_session_observations(workspace_root, out);
  return out;
}

std::string format_level2_debrief(const Level2Debrief& debrief) {
  std::ostringstream oss;
  oss << "### L2 resumen (hechos)\n";
  oss << "outcome: `" << debrief.outcome_tag << "`\n\n";
  for (const auto& f : debrief.facts) {
    if (f.tag == "outcome") {
      continue;
    }
    oss << "- [" << f.tag << "] " << f.detail << '\n';
  }
  return oss.str();
}

}  // namespace tuide
