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

}  // namespace

const char* l2_action_kind_name(L2ActionKind kind) {
  switch (kind) {
    case L2ActionKind::Tool:
      return "tool";
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
    if (action == "tool") {
      out.kind = L2ActionKind::Tool;
      out.name = j.value("name", "");
      out.arg = j.value("arg", "");
      if (out.name.empty()) {
        out.kind = L2ActionKind::Error;
        out.error = "tool sin name";
      }
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
