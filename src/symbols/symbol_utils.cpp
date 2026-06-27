#include "symbols/symbol_utils.hpp"

#include <cctype>

namespace tgdb {

std::string symbol_insert_name(const std::string& display_name) {
  std::string name = display_name;
  static const char* prefixes[] = {"ns ", "f ", "C ", "S ", "M ", "v "};
  for (const char* prefix : prefixes) {
    if (name.rfind(prefix, 0) == 0) {
      name = name.substr(std::char_traits<char>::length(prefix));
      break;
    }
  }

  std::string out;
  bool at_start = true;
  for (char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
      out.push_back(c);
      at_start = false;
      continue;
    }
    if (at_start && c == '~') {
      out.push_back(c);
      at_start = false;
      continue;
    }
    if (c == ':' && !out.empty() && out.back() == ':') {
      out.push_back(c);
      at_start = false;
      continue;
    }
    if (c == ':' && !out.empty() && out.back() != ':') {
      out.push_back(':');
      at_start = false;
      continue;
    }
    break;
  }

  while (out.size() >= 2 && out.back() == ':') {
    out.pop_back();
  }

  const auto scope = out.rfind("::");
  if (scope != std::string::npos) {
    out = out.substr(scope + 2);
  }

  return out.empty() ? name : out;
}

}  // namespace tgdb
