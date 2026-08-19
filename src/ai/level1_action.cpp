#include "ai/level1_action.hpp"

#include <cctype>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace tuide {
namespace {

bool is_reserved_action(const std::string& act) {
  return act == "tool" || act == "call_tool" || act == "seeds" || act == "query_seeds" ||
         act == "pick_stem" || act == "choose_stem" || act == "final" || act == "answer" ||
         act == "done" || act == "needs_level2" || act == "level2" || act == "escalate_l2" ||
         act == "error";
}

// Prefer a real action object; models often echo prose / code before JSON.
// When they echo the user prompt, the FIRST {"action" is the example — take the LAST.
std::size_t find_json_start(const std::string& text) {
  std::size_t best = std::string::npos;
  std::size_t pos = 0;
  while (pos < text.size()) {
    const auto action_pos = text.find("{\"action\"", pos);
    const auto action_pos2 = text.find("{\"action\" :", pos);
    const auto name_pos = text.find("{\"name\"", pos);
    std::size_t next = std::string::npos;
    if (action_pos != std::string::npos) {
      next = action_pos;
    }
    if (action_pos2 != std::string::npos && (next == std::string::npos || action_pos2 < next)) {
      next = action_pos2;
    }
    if (name_pos != std::string::npos && (next == std::string::npos || name_pos < next)) {
      next = name_pos;
    }
    if (next == std::string::npos) {
      break;
    }
    best = next;
    pos = next + 1;
  }
  if (best != std::string::npos) {
    return best;
  }
  return text.find('{');
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
  // Drop trailing incomplete token after last comma inside array/object.
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

std::string extract_json_object(const std::string& text) {
  const std::size_t start = find_json_start(text);
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
  // Truncated: take from start to end and repair.
  return repair_truncated_json(text.substr(start));
}

// Tiny models copy prompt ellipsis literally: needles:[...] or "text":"...".
std::string sanitize_prompt_placeholders(std::string json) {
  for (;;) {
    const auto p = json.find("[...]");
    if (p == std::string::npos) {
      break;
    }
    json.replace(p, 5, "[]");
  }
  for (;;) {
    const auto p = json.find("\"...\"");
    if (p == std::string::npos) {
      break;
    }
    json.replace(p, 5, "\"\"");
  }
  return json;
}

std::vector<std::string> json_string_array(const nlohmann::json& j, const char* key) {
  std::vector<std::string> out;
  if (!j.contains(key) || !j[key].is_array()) {
    return out;
  }
  for (const auto& item : j[key]) {
    if (item.is_string()) {
      out.push_back(item.get<std::string>());
    }
  }
  return out;
}

void fill_tool_arg(const nlohmann::json& j, Level1Action* action) {
  if (action == nullptr) {
    return;
  }
  if (j.contains("needles") && j["needles"].is_array()) {
    std::string joined;
    std::unordered_set<std::string> seen;
    constexpr int kMaxNeedles = 8;
    int n = 0;
    for (const auto& item : j["needles"]) {
      if (!item.is_string()) {
        continue;
      }
      const std::string needle = item.get<std::string>();
      if (needle.size() < 2 || !seen.insert(needle).second) {
        continue;
      }
      if (!joined.empty()) {
        joined.push_back('|');
      }
      joined += needle;
      if (++n >= kMaxNeedles) {
        break;
      }
    }
    if (!joined.empty()) {
      action->arg = std::move(joined);
      return;
    }
  }
  if (j.contains("arg") && j["arg"].is_string()) {
    action->arg = j["arg"].get<std::string>();
  } else if (j.contains("args")) {
    if (j["args"].is_string()) {
      action->arg = j["args"].get<std::string>();
    } else {
      action->arg = j["args"].dump();
    }
  } else if (j.contains("query") && j["query"].is_string()) {
    action->arg = j["query"].get<std::string>();
  } else if (j.contains("path") && j["path"].is_string()) {
    action->arg = j["path"].get<std::string>();
  }
}

Level1Action make_tool_action(const nlohmann::json& j, std::string tool_name) {
  Level1Action action;
  action.kind = Level1ActionKind::Tool;
  action.tool_name = std::move(tool_name);
  fill_tool_arg(j, &action);
  return action;
}

}  // namespace

Level1Action parse_level1_action(const std::string& model_text) {
  Level1Action action;
  action.raw = model_text;

  const bool runtime_err =
      model_text.find("exceeds the available context") != std::string::npos ||
      model_text.find("Error: request (") != std::string::npos;
  if (runtime_err) {
    action.kind = Level1ActionKind::Error;
    action.text = "error de contexto/runtime del backend L1 (sube ai.level1.n_ctx)";
    return action;
  }

  std::string json_text = extract_json_object(model_text);
  if (json_text.empty()) {
    // Prose-only answer from tiny models → treat as final rather than hard error.
    const std::string trimmed = [&] {
      std::size_t i = 0;
      while (i < model_text.size() &&
             std::isspace(static_cast<unsigned char>(model_text[i]))) {
        ++i;
      }
      return model_text.substr(i);
    }();
    // Reject prompt-echo "answers" (contain our own instructions).
    const bool looks_echo =
        trimmed.find("Responde SOLO") != std::string::npos ||
        trimmed.find("LOCALIZAR CÓDIGO") != std::string::npos ||
        trimmed.find("archivo_activo:") != std::string::npos;
    if (trimmed.size() >= 20 && trimmed.find('{') == std::string::npos && !looks_echo) {
      action.kind = Level1ActionKind::Final;
      action.text = trimmed;
      return action;
    }
    action.kind = Level1ActionKind::Error;
    action.text = "sin objeto JSON en la respuesta del modelo";
    return action;
  }
  json_text = sanitize_prompt_placeholders(std::move(json_text));
  // Truncated echo often leaves "client ... (truncated)" inside a string — drop junk tails.
  {
    const auto trunc = json_text.find(" ... (truncated)");
    if (trunc != std::string::npos) {
      json_text = repair_truncated_json(json_text.substr(0, trunc));
    }
  }
  try {
    const auto j = nlohmann::json::parse(json_text);
    std::string act;
    if (j.contains("action") && j["action"].is_string()) {
      act = j["action"].get<std::string>();
    } else if (j.contains("type") && j["type"].is_string()) {
      act = j["type"].get<std::string>();
    }

    if (act == "tool" || act == "call_tool") {
      std::string name;
      if (j.contains("name") && j["name"].is_string()) {
        name = j["name"].get<std::string>();
      } else if (j.contains("tool") && j["tool"].is_string()) {
        name = j["tool"].get<std::string>();
      }
      action = make_tool_action(j, std::move(name));
      action.raw = model_text;
      if (action.tool_name == "search" && action.arg.empty()) {
        action.kind = Level1ActionKind::Error;
        action.text =
            "search sin needles (no copies placeholders como [...]; usa strings reales)";
        action.tool_name.clear();
      }
      return action;
    }
    if (act == "seeds" || act == "query_seeds") {
      action.kind = Level1ActionKind::Seeds;
      action.seeds = json_string_array(j, "seeds");
      if (action.seeds.empty()) {
        action.seeds = json_string_array(j, "seeds_primary");
      }
      action.semantic_tokens = json_string_array(j, "semantic_tokens");
      if (action.semantic_tokens.empty()) {
        action.semantic_tokens = json_string_array(j, "tokens");
      }
      if (action.semantic_tokens.empty()) {
        action.semantic_tokens = json_string_array(j, "semantic");
      }
      if (j.contains("note") && j["note"].is_string()) {
        action.text = j["note"].get<std::string>();
      } else if (j.contains("plan") && j["plan"].is_string()) {
        action.text = j["plan"].get<std::string>();
      }
      return action;
    }
    if (act == "pick_stem" || act == "choose_stem") {
      action.kind = Level1ActionKind::PickStem;
      if (j.contains("stem") && j["stem"].is_string()) {
        action.stem = j["stem"].get<std::string>();
      } else if (j.contains("file_stem") && j["file_stem"].is_string()) {
        action.stem = j["file_stem"].get<std::string>();
      } else if (j.contains("text") && j["text"].is_string()) {
        action.stem = j["text"].get<std::string>();
      }
      return action;
    }
    if (act == "final" || act == "answer" || act == "done") {
      action.kind = Level1ActionKind::Final;
      if (j.contains("text") && j["text"].is_string()) {
        action.text = j["text"].get<std::string>();
      } else if (j.contains("message") && j["message"].is_string()) {
        action.text = j["message"].get<std::string>();
      }
      return action;
    }
    if (act == "needs_level2" || act == "level2" || act == "escalate_l2") {
      action.kind = Level1ActionKind::NeedsLevel2;
      if (j.contains("instruction") && j["instruction"].is_string()) {
        action.instruction = j["instruction"].get<std::string>();
      } else if (j.contains("text") && j["text"].is_string()) {
        action.instruction = j["text"].get<std::string>();
      }
      action.seeds = json_string_array(j, "seeds");
      return action;
    }
    if (act == "error") {
      action.kind = Level1ActionKind::Error;
      if (j.contains("message") && j["message"].is_string()) {
        action.text = j["message"].get<std::string>();
      }
      return action;
    }

    // Modelos pequeños: {"action":"git_status","arg":"..."} en vez de
    // {"action":"tool","name":"git_status"}.
    if (!act.empty() && !is_reserved_action(act)) {
      action = make_tool_action(j, act);
      action.raw = model_text;
      if (action.tool_name == "search" && action.arg.empty()) {
        action.kind = Level1ActionKind::Error;
        action.text =
            "search sin needles (no copies placeholders como [...]; usa strings reales)";
        action.tool_name.clear();
      }
      return action;
    }
    if (act.empty() && j.contains("name") && j["name"].is_string()) {
      action = make_tool_action(j, j["name"].get<std::string>());
      action.raw = model_text;
      if (action.tool_name == "search" && action.arg.empty()) {
        action.kind = Level1ActionKind::Error;
        action.text =
            "search sin needles (no copies placeholders como [...]; usa strings reales)";
        action.tool_name.clear();
      }
      return action;
    }

    action.kind = Level1ActionKind::Unknown;
    action.text = "action desconocida: " + act;
    return action;
  } catch (const std::exception& ex) {
    action.kind = Level1ActionKind::Error;
    action.text = std::string("JSON inválido: ") + ex.what();
    return action;
  }
}

}  // namespace tuide
