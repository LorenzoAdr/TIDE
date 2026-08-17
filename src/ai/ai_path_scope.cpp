#include "ai/ai_path_scope.hpp"

#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace tuide {
namespace {

std::string strip_trailing_slashes(std::string s) {
  while (s.size() > 1 && (s.back() == '/' || s.back() == '\\')) {
    s.pop_back();
  }
  return s;
}

std::string to_generic(std::string s) {
  for (char& c : s) {
    if (c == '\\') {
      c = '/';
    }
  }
  return strip_trailing_slashes(std::move(s));
}

}  // namespace

std::string ai_normalize_scope_path(const std::string& workspace_root, const std::string& path) {
  if (path.empty()) {
    return {};
  }
  fs::path p(path);
  std::error_code ec;
  if (!workspace_root.empty()) {
    const fs::path root(workspace_root);
    if (p.is_absolute()) {
      const auto rel = fs::relative(p.lexically_normal(), root.lexically_normal(), ec);
      if (!ec && !rel.empty() && rel.native().rfind("..", 0) != 0) {
        return to_generic(rel.generic_string());
      }
    } else {
      return to_generic(p.lexically_normal().generic_string());
    }
  }
  return to_generic(p.lexically_normal().generic_string());
}

bool ai_path_in_scope(const std::string& workspace_root, const std::string& path,
                      const std::vector<std::string>& path_scope) {
  if (path_scope.empty()) {
    return true;
  }
  if (path.empty()) {
    return false;
  }
  const std::string rel = ai_normalize_scope_path(workspace_root, path);
  if (rel.empty() || rel == ".") {
    return false;
  }
  for (const auto& raw : path_scope) {
    const std::string prefix = ai_normalize_scope_path(workspace_root, raw);
    if (prefix.empty()) {
      continue;
    }
    if (rel == prefix) {
      return true;
    }
    if (rel.size() > prefix.size() && rel.compare(0, prefix.size(), prefix) == 0 &&
        rel[prefix.size()] == '/') {
      return true;
    }
  }
  return false;
}

std::string ai_path_scope_prompt_line(const std::vector<std::string>& path_scope) {
  if (path_scope.empty()) {
    return {};
  }
  std::ostringstream out;
  out << "Ámbito restringido a:";
  for (const auto& p : path_scope) {
    out << " `" << p << "`";
  }
  out << ". No explores ni edites fuera de estos directorios.";
  return out.str();
}

}  // namespace tuide
