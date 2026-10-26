#include "ai/ai_mentions.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace tuide {
namespace {

// Mismo criterio que la búsqueda del explorador: `.claude/worktrees` son copias
// completas del repo y resolverían una mención a la copia rancia.
bool skip_dir_name(const std::string& name) {
  static const std::array<const char*, 8> kSkip = {".git",   ".tuide", ".claude", "build",
                                                   "node_modules", ".cache", "CMakeFiles",
                                                   "dist"};
  for (const char* s : kSkip) {
    if (name == s) {
      return true;
    }
  }
  return false;
}

std::string ascii_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

std::string path_generic(std::string s) {
  for (char& c : s) {
    if (c == '\\') {
      c = '/';
    }
  }
  return s;
}

// Un token de mención admite ruta, nombre y los caracteres habituales de
// fichero. Se corta en el primer carácter que no encaje, así que "@a.cpp," o
// "@a.cpp." funcionan.
bool mention_char(char c) {
  const auto u = static_cast<unsigned char>(c);
  return std::isalnum(u) != 0 || c == '_' || c == '-' || c == '.' || c == '/' || c == '+';
}

std::string trim_trailing_punct(std::string s) {
  while (!s.empty() && (s.back() == '.' || s.back() == ',' || s.back() == ';' ||
                        s.back() == ':' || s.back() == ')')) {
    // Una extensión legítima no deja el punto al final.
    s.pop_back();
  }
  return s;
}

std::vector<std::string> extract_tokens(const std::string& message) {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < message.size(); ++i) {
    if (message[i] != '@') {
      continue;
    }
    // Una @ pegada a texto (correo, decorador) no es una mención.
    if (i > 0 && mention_char(message[i - 1])) {
      continue;
    }
    std::size_t j = i + 1;
    std::string token;
    while (j < message.size() && mention_char(message[j])) {
      token.push_back(message[j]);
      ++j;
    }
    token = trim_trailing_punct(std::move(token));
    if (token.size() >= 2 &&
        std::find(out.begin(), out.end(), token) == out.end()) {
      out.push_back(token);
    }
    i = j;
  }
  return out;
}

struct Candidate {
  std::string rel;
  int score = 0;  // mayor = mejor
};

std::vector<std::string> list_files(const fs::path& root, std::size_t cap) {
  std::vector<std::string> out;
  std::error_code ec;
  fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
  const fs::recursive_directory_iterator end;
  for (; it != end && out.size() < cap; it.increment(ec)) {
    if (ec) {
      break;
    }
    std::error_code dec;
    if (it->is_directory(dec)) {
      if (skip_dir_name(it->path().filename().string())) {
        it.disable_recursion_pending();
      }
      continue;
    }
    const std::string rel = path_generic(fs::relative(it->path(), root, ec).string());
    if (!rel.empty() && rel.rfind("..", 0) != 0) {
      out.push_back(rel);
    }
  }
  return out;
}

std::string read_head(const fs::path& path, int max_chars, bool* truncated, int* lines) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::string body;
  body.resize(static_cast<std::size_t>(max_chars) + 1);
  in.read(body.data(), static_cast<std::streamsize>(body.size()));
  body.resize(static_cast<std::size_t>(in.gcount()));
  if (static_cast<int>(body.size()) > max_chars) {
    body.resize(static_cast<std::size_t>(max_chars));
    // No cortar a mitad de línea.
    const auto nl = body.rfind('\n');
    if (nl != std::string::npos && nl > body.size() / 2) {
      body.resize(nl + 1);
    }
    if (truncated) {
      *truncated = true;
    }
  }
  if (lines) {
    *lines = static_cast<int>(std::count(body.begin(), body.end(), '\n'));
  }
  return body;
}

}  // namespace

std::vector<AiMention> ai_resolve_mentions(const std::string& message,
                                           const std::string& workspace_root, int max_files,
                                           int max_chars_each) {
  std::vector<AiMention> out;
  const auto tokens = extract_tokens(message);
  if (tokens.empty() || workspace_root.empty()) {
    return out;
  }
  const fs::path root(workspace_root);
  std::error_code ec;
  if (!fs::is_directory(root, ec)) {
    return out;
  }
  // Un solo recorrido para todas las menciones de la línea.
  const auto files = list_files(root, 60000);

  const int cap_files = max_files > 0 ? max_files : kAiMentionMaxFiles;
  const int cap_chars = max_chars_each > 0 ? max_chars_each : kAiMentionCharsEach;

  for (const auto& token : tokens) {
    if (static_cast<int>(out.size()) >= cap_files) {
      break;
    }
    AiMention m;
    m.token = token;
    const std::string low_tok = ascii_lower(path_generic(token));
    std::vector<Candidate> cands;
    for (const auto& rel : files) {
      const std::string low = ascii_lower(rel);
      int score = 0;
      if (low == low_tok) {
        score = 100;  // ruta relativa exacta
      } else if (low.size() > low_tok.size() &&
                 low.compare(low.size() - low_tok.size(), low_tok.size(), low_tok) == 0 &&
                 low[low.size() - low_tok.size() - 1] == '/') {
        score = 80;  // sufijo de ruta (src/ai/x.cpp para "ai/x.cpp")
      } else {
        const auto slash = low.find_last_of('/');
        const std::string base = slash == std::string::npos ? low : low.substr(slash + 1);
        if (base == low_tok) {
          score = 60;  // nombre de fichero exacto
        } else if (base.find(low_tok) != std::string::npos) {
          score = 30;  // subcadena del nombre
        } else if (low.find(low_tok) != std::string::npos) {
          score = 10;  // subcadena de la ruta
        }
      }
      if (score > 0) {
        // Entre dos iguales, gana la ruta más corta: suele ser la fuente y no
        // un duplicado enterrado.
        cands.push_back({rel, score * 1000 - static_cast<int>(rel.size())});
      }
    }
    if (!cands.empty()) {
      std::sort(cands.begin(), cands.end(),
                [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
      m.path = cands.front().rel;
      for (std::size_t i = 1; i < cands.size() && i <= 3; ++i) {
        m.other_candidates.push_back(cands[i].rel);
      }
      m.excerpt = read_head(root / m.path, cap_chars, &m.truncated, &m.lines);
    }
    out.push_back(std::move(m));
  }
  return out;
}

std::string ai_mentions_block(const std::vector<AiMention>& mentions) {
  std::ostringstream out;
  bool any = false;
  for (const auto& m : mentions) {
    if (m.path.empty() || m.excerpt.empty()) {
      continue;
    }
    any = true;
    out << "### `" << m.path << "` (el usuario lo señaló con @" << m.token << ")\n";
    out << "```\n" << m.excerpt;
    if (!m.excerpt.empty() && m.excerpt.back() != '\n') {
      out << "\n";
    }
    out << "```\n";
    if (m.truncated) {
      out << "(solo el principio; pide `read " << m.path << ":N-M` para el resto)\n";
    }
  }
  if (!any) {
    return {};
  }
  return out.str();
}

std::vector<std::string> ai_mentions_report(const std::vector<AiMention>& mentions) {
  std::vector<std::string> out;
  for (const auto& m : mentions) {
    if (m.path.empty()) {
      out.push_back("  · @" + m.token + " → no encontrado en el workspace");
      continue;
    }
    std::string line = "  · @" + m.token + " → " + m.path;
    if (m.lines > 0) {
      line += " (" + std::to_string(m.lines) + " líneas";
      line += m.truncated ? ", recortado)" : ")";
    }
    if (!m.other_candidates.empty()) {
      line += "  [también: " + m.other_candidates.front();
      if (m.other_candidates.size() > 1) {
        line += " +" + std::to_string(m.other_candidates.size() - 1);
      }
      line += "]";
    }
    out.push_back(line);
  }
  return out;
}

}  // namespace tuide
