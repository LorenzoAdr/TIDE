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

}  // namespace

const char* l2_action_kind_name(L2ActionKind kind) {
  switch (kind) {
    case L2ActionKind::Tool:
      return "tool";
    case L2ActionKind::Tools:
      return "tools";
    case L2ActionKind::Done:
      return "done";
    case L2ActionKind::Edit:
      return "edit";
    case L2ActionKind::Error:
      return "error";
    case L2ActionKind::Unknown:
      return "unknown";
  }
  return "unknown";
}

L2Action parse_l2_action(const std::string& model_text) {
  L2Action out;
  out.raw = model_text;
  const std::string json_text = extract_json_object(model_text);
  if (json_text.empty()) {
    out.kind = L2ActionKind::Error;
    out.error = "sin objeto JSON {\"action\":…}";
    return out;
  }
  try {
    const auto j = nlohmann::json::parse(json_text);
    const std::string action = j.value("action", "");
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
      // Prefer batch if model sent calls[] with action=tool.
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
        out.kind = L2ActionKind::Error;
        out.error = err.empty() ? "edit sin hunks" : err;
      }
      return out;
    }
    out.kind = L2ActionKind::Unknown;
    out.error = "action desconocida: " + action;
    return out;
  } catch (const std::exception& ex) {
    out.kind = L2ActionKind::Error;
    out.error = std::string("JSON inválido: ") + ex.what();
    return out;
  }
}

}  // namespace tuide
