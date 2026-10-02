#include "ai/action_json.hpp"

#include <cctype>
#include <cstring>

#include <nlohmann/json.hpp>

namespace tuide {
namespace {

std::string trim_ws(std::string s) {
  std::size_t a = 0;
  while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) {
    ++a;
  }
  std::size_t b = s.size();
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
    --b;
  }
  return s.substr(a, b - a);
}

std::string scan_balanced_object(const std::string& raw, std::size_t start) {
  if (start >= raw.size() || raw[start] != '{') {
    return {};
  }
  int depth = 0;
  bool in_string = false;
  bool escape = false;
  for (std::size_t i = start; i < raw.size(); ++i) {
    const char c = raw[i];
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
        return raw.substr(start, i - start + 1);
      }
    }
  }
  return {};
}

std::string strip_md_json_fence(std::string s) {
  s = trim_ws(std::move(s));
  if (s.size() < 7 || s.compare(0, 3, "```") != 0) {
    return s;
  }
  const auto nl = s.find('\n');
  if (nl == std::string::npos) {
    return s;
  }
  const auto end = s.rfind("```");
  if (end == std::string::npos || end <= nl) {
    return s;
  }
  return trim_ws(s.substr(nl + 1, end - nl - 1));
}

bool is_action_object(const nlohmann::json& j, bool ola_only) {
  if (!j.is_object() || !j.contains("action") || !j["action"].is_string()) {
    return false;
  }
  const std::string a = j["action"].get<std::string>();
  if (ola_only) {
    return a.size() >= 5 && a.compare(0, 5, "ola_v") == 0;
  }
  return true;
}

std::string last_action_object(const std::string& raw, bool ola_only) {
  std::string best;
  std::size_t search = 0;
  while (search < raw.size()) {
    const auto brace = raw.find('{', search);
    if (brace == std::string::npos) {
      break;
    }
    const std::string obj = scan_balanced_object(raw, brace);
    if (!obj.empty()) {
      try {
        const auto j = nlohmann::json::parse(obj);
        if (is_action_object(j, ola_only)) {
          best = obj;
        }
      } catch (const std::exception&) {
      }
    }
    search = brace + 1;
  }
  return best;
}

}  // namespace

std::string strip_model_think(const std::string& raw) {
  std::string s = raw;
  const char* tags[] = {"</think>", "</thinking>", "</redacted_thinking>"};
  std::size_t cut = std::string::npos;
  for (const char* t : tags) {
    const auto p = s.rfind(t);
    if (p == std::string::npos) {
      continue;
    }
    const auto after = p + std::strlen(t);
    if (cut == std::string::npos || after > cut) {
      cut = after;
    }
  }
  if (cut != std::string::npos) {
    s = s.substr(cut);
  }
  return strip_md_json_fence(std::move(s));
}

std::string extract_action_json(const std::string& raw) {
  const std::string stripped = strip_model_think(raw);
  std::string found = last_action_object(stripped, false);
  if (!found.empty()) {
    return found;
  }
  return last_action_object(raw, false);
}

std::string extract_ola_json(const std::string& raw) {
  const std::string stripped = strip_model_think(raw);
  std::string found = last_action_object(stripped, true);
  if (!found.empty()) {
    return found;
  }
  return last_action_object(raw, true);
}

}  // namespace tuide
