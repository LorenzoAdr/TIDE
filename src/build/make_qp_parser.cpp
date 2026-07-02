#include "build/make_qp_parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace tgdb {

namespace {

std::string trim(const std::string& value) {
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }
  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(begin, end - begin);
}

std::string unquote_make_value(std::string value) {
  value = trim(value);
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

void split_tokens(const std::string& text, std::vector<std::string>* out) {
  if (out == nullptr) {
    return;
  }
  std::istringstream stream(text);
  std::string token;
  while (stream >> token) {
    out->push_back(token);
  }
}

bool looks_like_output_dir(const std::string& name) {
  static const char* kNames[] = {"build", "out", "obj", "objs", "output", "bin", "dist"};
  const std::string lower = [&] {
    std::string copy = name;
    std::transform(copy.begin(), copy.end(), copy.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return copy;
  }();
  for (const auto* candidate : kNames) {
    if (lower.find(candidate) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

MakeQpInfo parse_make_qp_output(const std::string& text) {
  MakeQpInfo info;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') {
      continue;
    }

  const auto assign = trimmed.find('=');
    if (assign != std::string::npos && assign > 0) {
      std::string key = trim(trimmed.substr(0, assign));
      const auto colon = key.rfind(':');
      if (colon != std::string::npos) {
        key = trim(key.substr(0, colon));
      }
      if (key.empty() || key.find('$') != std::string::npos) {
        continue;
      }
      const std::string value = unquote_make_value(trimmed.substr(assign + 1));
      info.variables[key] = value;
      if (key == "BUILD_DIR" || key == "OUT_DIR" || key == "OBJDIR" || key == "O") {
        if (!value.empty()) {
          info.output_dirs.push_back(value);
        }
      }
      continue;
    }

    const auto colon = trimmed.find(':');
    if (colon != std::string::npos && colon > 0) {
      const std::string target = trim(trimmed.substr(0, colon));
      if (!target.empty() && target.find('%') == std::string::npos &&
          target.find('$') == std::string::npos) {
        info.targets.push_back(target);
      }
    }
  }

  info.compile_flags = extract_compile_flags_from_make_vars(info.variables);
  for (const auto& entry : info.variables) {
    if (looks_like_output_dir(entry.first) && !entry.second.empty()) {
      info.output_dirs.push_back(entry.second);
    }
  }
  return info;
}

std::vector<std::string> extract_compile_flags_from_make_vars(
    const std::map<std::string, std::string>& variables) {
  std::vector<std::string> flags;
  const char* kKeys[] = {"CFLAGS", "CXXFLAGS", "CPPFLAGS", "INCLUDES"};
  for (const auto* key : kKeys) {
    const auto it = variables.find(key);
    if (it == variables.end() || it->second.empty()) {
      continue;
    }
    std::vector<std::string> tokens;
    split_tokens(it->second, &tokens);
    for (const auto& token : tokens) {
      if (token.rfind("-I", 0) == 0 || token.rfind("-D", 0) == 0 ||
          token.rfind("-isystem", 0) == 0) {
        flags.push_back(token);
      }
    }
  }

  const auto cross = variables.find("CROSS_COMPILE");
  if (cross != variables.end() && !cross->second.empty()) {
    flags.push_back("-D TGDB_CROSS_COMPILE=" + cross->second);
  }
  return flags;
}

}  // namespace tgdb
