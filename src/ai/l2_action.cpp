#include "ai/l2_action.hpp"

#include <cctype>

#include <nlohmann/json.hpp>

namespace tuide {
namespace {

std::size_t find_action_json_start(const std::string& text) {
  const auto tagged = text.find("{\"action\"");
  if (tagged != std::string::npos) {
    return tagged;
  }
  return text.find('{');
}

std::string extract_json_object(const std::string& text) {
  const std::size_t start = find_action_json_start(text);
  if (start == std::string::npos) {
    return {};
  }
  int depth = 0;
  bool in_string = false;
  bool escape = false;
  for (std::size_t i = start; i < text.size(); ++i) {
    const char c = text[i];
    if (in_string) {
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
      continue;
    }
    if (c == '{') {
      ++depth;
    } else if (c == '}') {
      --depth;
      if (depth == 0) {
        return text.substr(start, i - start + 1);
      }
    }
  }
  return {};
}

// Close truncated JSON from small models (cut mid-array / mid-string by max_tokens).
std::string repair_truncated_json(std::string json) {
  if (json.empty()) {
    return json;
  }
  int brace = 0;
  int bracket = 0;
  bool in_string = false;
  bool escape = false;
  for (char c : json) {
    if (in_string) {
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
    } else if (c == '{') {
      ++brace;
    } else if (c == '}') {
      --brace;
    } else if (c == '[') {
      ++bracket;
    } else if (c == ']') {
      --bracket;
    }
  }
  if (!in_string && brace == 0 && bracket == 0) {
    return json;
  }
  if (in_string) {
    json.push_back('"');
  }
  while (!json.empty()) {
    const char c = json.back();
    if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
      json.pop_back();
      continue;
    }
    break;
  }
  while (bracket > 0) {
    json.push_back(']');
    --bracket;
  }
  while (brace > 0) {
    json.push_back('}');
    --brace;
  }
  return json;
}

std::string strip_replacement_chars(std::string s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    if (i + 2 < s.size() && static_cast<unsigned char>(s[i]) == 0xEF &&
        static_cast<unsigned char>(s[i + 1]) == 0xBF &&
        static_cast<unsigned char>(s[i + 2]) == 0xBD) {
      i += 3;
      continue;
    }
    if (static_cast<unsigned char>(s[i]) == 0xFF) {
      ++i;
      continue;
    }
    out.push_back(s[i]);
    ++i;
  }
  return out;
}

// Models often emit \s / \s* (invalid JSON) or raw newlines inside strings.
// Rewrite to legal escapes so nlohmann can parse edit hunks.
std::string repair_json_string_escapes(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 16);
  bool in_string = false;
  for (std::size_t i = 0; i < in.size(); ++i) {
    const char c = in[i];
    if (!in_string) {
      out.push_back(c);
      if (c == '"') {
        in_string = true;
      }
      continue;
    }
    if (c == '\\' && i + 1 < in.size()) {
      const char n = in[i + 1];
      const bool valid = n == '"' || n == '\\' || n == '/' || n == 'b' || n == 'f' || n == 'n' ||
                         n == 'r' || n == 't' || n == 'u';
      if (valid) {
        out.push_back('\\');
        out.push_back(n);
        ++i;
        if (n == 'u') {
          for (int k = 0; k < 4 && i + 1 < in.size(); ++k) {
            out.push_back(in[++i]);
          }
        }
        continue;
      }
      // \s or \s* → \n (common coder-model whitespace thinko)
      if (n == 's') {
        out.push_back('\\');
        out.push_back('n');
        ++i;
        if (i + 1 < in.size() && in[i + 1] == '*') {
          ++i;
        }
        continue;
      }
      // Other illegal escapes → treat as newline separator
      out.push_back('\\');
      out.push_back('n');
      ++i;
      continue;
    }
    if (c == '"') {
      in_string = false;
      out.push_back(c);
      continue;
    }
    if (c == '\r' || c == '\n') {
      if (c == '\r' && i + 1 < in.size() && in[i + 1] == '\n') {
        ++i;
      }
      out.push_back('\\');
      out.push_back('n');
      continue;
    }
    out.push_back(c);
  }
  return out;
}

bool parse_calls_array(const nlohmann::json& j, std::vector<L2ToolCall>* out, std::string* err) {
  if (out == nullptr) {
    return false;
  }
  out->clear();
  if (!j.contains("calls") || !j["calls"].is_array()) {
    if (err) {
      *err = "tools sin array calls";
    }
    return false;
  }
  for (const auto& c : j["calls"]) {
    if (!c.is_object()) {
      continue;
    }
    L2ToolCall call;
    call.name = c.value("name", "");
    call.arg = c.value("arg", "");
    if (call.name.empty()) {
      continue;
    }
    out->push_back(std::move(call));
    if (static_cast<int>(out->size()) >= kL2MaxToolBatch) {
      break;
    }
  }
  if (out->empty()) {
    if (err) {
      *err = "tools.calls vacío o sin name";
    }
    return false;
  }
  return true;
}

bool parse_targets_array(const nlohmann::json& j, std::vector<std::string>* out, std::string* err) {
  if (out == nullptr) {
    return false;
  }
  out->clear();
  if (!j.contains("targets") || !j["targets"].is_array()) {
    if (err) {
      *err = "plan sin array targets";
    }
    return false;
  }
  for (const auto& t : j["targets"]) {
    std::string target;
    if (t.is_string()) {
      target = t.get<std::string>();
    } else if (t.is_object()) {
      const std::string path = t.value("path", "");
      const std::string sym = t.value("symbol", t.value("name", ""));
      const int line = t.value("line", 0);
      if (!path.empty() && !sym.empty()) {
        target = path + ":" + sym;
      } else if (!path.empty() && line > 0) {
        target = path + ":" + std::to_string(line);
      } else {
        target = path;
      }
    }
    while (!target.empty() && (target.front() == ' ' || target.front() == '`')) {
      target.erase(target.begin());
    }
    while (!target.empty() && (target.back() == ' ' || target.back() == '`')) {
      target.pop_back();
    }
    if (target.empty()) {
      continue;
    }
    out->push_back(std::move(target));
    if (static_cast<int>(out->size()) >= kL2MaxPlanTargets) {
      break;
    }
  }
  if (out->empty()) {
    if (err) {
      *err = "plan.targets vacío";
    }
    return false;
  }
  return true;
}

}  // namespace

const char* l2_action_kind_name(L2ActionKind kind) {
  switch (kind) {
    case L2ActionKind::Tool:
      return "tool";
    case L2ActionKind::Tools:
      return "tools";
    case L2ActionKind::Plan:
      return "plan";
    case L2ActionKind::AJudge:
      return "a_judge";
    case L2ActionKind::ATrailJudge:
      return "a_trail_judge";
    case L2ActionKind::ADone:
      return "a_done";
    case L2ActionKind::Done:
      return "done";
    case L2ActionKind::Edit:
      return "edit";
    case L2ActionKind::Synthesize:
      return "synthesize";
    case L2ActionKind::Error:
      return "error";
    case L2ActionKind::Unknown:
      return "unknown";
  }
  return "unknown";
}

L2Action parse_aider_edit(const std::string& text) {
  L2Action out;
  out.raw = text;
  std::string err;
  auto hunks = parse_search_replace_aider(text, &err);
  if (!hunks.empty()) {
    out.kind = L2ActionKind::Edit;
    out.hunks = std::move(hunks);
    return out;
  }
  out.kind = L2ActionKind::Error;
  out.error = err.empty() ? "sin bloques SEARCH/REPLACE" : err;
  return out;
}

L2Action parse_l2_action(const std::string& model_text) {
  L2Action out;
  out.raw = model_text;
  const std::string cleaned = strip_replacement_chars(model_text);
  if (cleaned.find("<<<<<<< SEARCH") != std::string::npos) {
    const L2Action aider = parse_aider_edit(cleaned);
    if (aider.kind == L2ActionKind::Edit) {
      return aider;
    }
  }
  std::string json_text = extract_json_object(cleaned);
  if (json_text.empty()) {
    const std::size_t start = find_action_json_start(cleaned);
    if (start != std::string::npos) {
      json_text = repair_truncated_json(cleaned.substr(start));
    }
  }
  if (json_text.empty()) {
    const L2Action aider = parse_aider_edit(cleaned);
    if (aider.kind == L2ActionKind::Edit) {
      return aider;
    }
    out.kind = L2ActionKind::Error;
    out.error = "sin objeto JSON {\"action\":…}";
    return out;
  }
  try {
    nlohmann::json j;
    try {
      j = nlohmann::json::parse(json_text);
    } catch (const std::exception&) {
      try {
        j = nlohmann::json::parse(repair_json_string_escapes(json_text));
      } catch (const std::exception&) {
        j = nlohmann::json::parse(repair_truncated_json(repair_json_string_escapes(json_text)));
      }
    }
    const std::string action = j.value("action", "");
    if (action == "plan" || action == "watchlist") {
      out.kind = L2ActionKind::Plan;
      out.summary = j.value("summary", "");
      std::string err;
      if (!parse_targets_array(j, &out.targets, &err)) {
        out.kind = L2ActionKind::Error;
        out.error = err;
      }
      return out;
    }
    if (action == "a_judge" || action == "judge") {
      out.kind = L2ActionKind::AJudge;
      out.summary = j.value("summary", "");
      out.a_turn_done = j.value("done", false);
      std::string err;
      if (!parse_a_verdicts_array(j, &out.a_verdicts, &err)) {
        out.kind = L2ActionKind::Error;
        out.error = err;
      }
      return out;
    }
    if (action == "a_trail_judge" || action == "trail_judge") {
      out.kind = L2ActionKind::ATrailJudge;
      out.summary = j.value("summary", "");
      std::string err;
      if (!parse_a_trail_verdicts_array(j, &out.a_verdicts, &err)) {
        out.kind = L2ActionKind::Error;
        out.error = err;
      }
      return out;
    }
    if (action == "a_done" || action == "locate_done") {
      out.kind = L2ActionKind::ADone;
      out.summary = j.value("summary", "");
      std::string err;
      if (!parse_a_loci_array(j, &out.a_loci, &err)) {
        out.kind = L2ActionKind::Error;
        out.error = err;
      }
      return out;
    }
    if (action == "tools") {
      out.kind = L2ActionKind::Tools;
      std::string err;
      if (!parse_calls_array(j, &out.calls, &err)) {
        out.kind = L2ActionKind::Error;
        out.error = err;
      }
      return out;
    }
    if (action == "tool") {
      if (j.contains("calls") && j["calls"].is_array()) {
        out.kind = L2ActionKind::Tools;
        std::string err;
        if (!parse_calls_array(j, &out.calls, &err)) {
          out.kind = L2ActionKind::Error;
          out.error = err;
        }
        return out;
      }
      out.kind = L2ActionKind::Tool;
      out.name = j.value("name", "");
      out.arg = j.value("arg", "");
      if (out.name.empty()) {
        out.kind = L2ActionKind::Error;
        out.error = "tool sin name";
        return out;
      }
      out.calls.push_back(L2ToolCall{out.name, out.arg});
      return out;
    }
    if (action == "done") {
      out.kind = L2ActionKind::Done;
      out.summary = j.value("summary", "");
      out.next = j.value("next", "");
      return out;
    }
    if (action == "edit") {
      out.kind = L2ActionKind::Edit;
      std::string err;
      out.hunks = parse_search_replace_json(j, &err);
      if (out.hunks.empty()) {
        const L2Action aider = parse_aider_edit(cleaned);
        if (aider.kind == L2ActionKind::Edit) {
          return aider;
        }
        out.kind = L2ActionKind::Error;
        out.error = err.empty() ? "edit sin hunks" : err;
      }
      return out;
    }
    if (action == "synthesize" || action == "answer" || action == "explain" ||
        action == "plan_doc") {
      out.kind = L2ActionKind::Synthesize;
      out.summary = j.value("summary", j.value("text", j.value("answer", "")));
      if (out.summary.empty()) {
        out.kind = L2ActionKind::Error;
        out.error = "synthesize sin summary/text";
      }
      return out;
    }
    out.kind = L2ActionKind::Unknown;
    out.error = "action desconocida: " + action;
    return out;
  } catch (const std::exception& ex) {
    const L2Action aider = parse_aider_edit(cleaned);
    if (aider.kind == L2ActionKind::Edit) {
      return aider;
    }
    out.kind = L2ActionKind::Error;
    out.error = std::string("JSON inválido: ") + ex.what();
    return out;
  }
}

}  // namespace tuide
