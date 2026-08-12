#include "ai/level0_router.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "ai/search_needles.hpp"

namespace tuide {
namespace {

std::string trim(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::string to_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool starts_with(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string after_first_space(const std::string& s) {
  const auto pos = s.find(' ');
  if (pos == std::string::npos) {
    return {};
  }
  return trim(s.substr(pos + 1));
}

// Extract `backtick` identifiers and path-like tokens (foo.cpp, Bar::baz).
std::vector<std::string> extract_literal_seeds(const std::string& input) {
  std::vector<std::string> seeds;
  for (std::size_t i = 0; i < input.size();) {
    if (input[i] == '`') {
      const auto end = input.find('`', i + 1);
      if (end == std::string::npos) {
        break;
      }
      const std::string token = input.substr(i + 1, end - i - 1);
      if (!token.empty()) {
        seeds.push_back(token);
      }
      i = end + 1;
      continue;
    }
    ++i;
  }

  std::string current;
  auto flush = [&] {
    if (current.size() >= 3 &&
        (current.find('.') != std::string::npos || current.find("::") != std::string::npos ||
         current.find('/') != std::string::npos ||
         (std::isupper(static_cast<unsigned char>(current[0])) &&
          current.find('_') == std::string::npos))) {
      seeds.push_back(current);
    }
    current.clear();
  };
  for (char ch : input) {
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == ':' || ch == '.' ||
        ch == '/' || ch == '-') {
      current.push_back(ch);
    } else {
      flush();
    }
  }
  flush();
  return seeds;
}

AiRouteResult tool(const std::string& name, const std::string& arg = {}) {
  AiRouteResult r;
  r.kind = AiRouteKind::ResolveTool;
  r.tool_name = name;
  r.arg = arg;
  return r;
}

AiRouteResult task(const std::string& name) {
  AiRouteResult r;
  r.kind = AiRouteKind::ResolveTask;
  r.task_name = name;
  return r;
}

AiRouteResult escalate(const std::string& reason = {}) {
  AiRouteResult r;
  r.kind = AiRouteKind::EscalateLevel1;
  r.message = reason.empty()
                  ? "requiere Nivel 1; aún no cargado"
                  : reason;
  return r;
}

bool asks_code_location(const std::string& lower) {
  // Canonical detector shared with L1 (map-first investigate).
  return query_asks_code_location(lower);
}

// Preguntas amplias / multi-tema: L0 no inventa needles de proyecto.
bool asks_conceptual_code_topic(const std::string& lower) {
  return lower.find("relacionad") != std::string::npos ||
         lower.find("todos los temas") != std::string::npos ||
         lower.find("todo lo relacionad") != std::string::npos ||
         lower.find("todos los aspectos") != std::string::npos ||
         lower.find("en general") != std::string::npos ||
         lower.find("arquitectura") != std::string::npos ||
         lower.find("como funciona") != std::string::npos ||
         lower.find("cómo funciona") != std::string::npos ||
         lower.find("how does") != std::string::npos ||
         lower.find("how do ") != std::string::npos ||
         lower.find("explain how") != std::string::npos ||
         lower.find("everything related") != std::string::npos ||
         lower.find("all the things") != std::string::npos ||
         lower.find("all topics") != std::string::npos ||
         ((lower.find("temas") != std::string::npos) &&
          (lower.find("código") != std::string::npos || lower.find("codigo") != std::string::npos ||
           lower.find("code") != std::string::npos)) ||
         lower.find("como se rellena") != std::string::npos ||
         lower.find("cómo se rellena") != std::string::npos ||
         lower.find("y como se") != std::string::npos ||
         lower.find("y cómo se") != std::string::npos;
}

bool is_git_repo_tool(const std::string& name) {
  return name == "git_status" || name == "git_diff" || name == "git_pull" ||
         name == "git_branches" || name == "git_log" || name == "git_show";
}

bool asks_modified_files(const std::string& lower) {
  if (asks_code_location(lower)) {
    return false;
  }
  const bool has_mod = lower.find("modific") != std::string::npos ||
                       lower.find("modified") != std::string::npos ||
                       lower.find("unstaged") != std::string::npos;
  // "archiv" / "archv" cubre typos tipo "archviso".
  const bool has_file = lower.find("archiv") != std::string::npos ||
                        lower.find("archv") != std::string::npos ||
                        lower.find("fichero") != std::string::npos ||
                        lower.find("file") != std::string::npos;
  if (has_mod && has_file) {
    return true;
  }
  // Estado general del repo / "¿tengo muchos cambios?"
  if ((lower.find("git") != std::string::npos || lower.find("repo") != std::string::npos) &&
      (lower.find("cambio") != std::string::npos || lower.find("como est") != std::string::npos ||
       lower.find("cómo est") != std::string::npos || lower.find("estado") != std::string::npos ||
       lower.find("status") != std::string::npos || lower.find("mucho") != std::string::npos ||
       lower.find("muchos") != std::string::npos)) {
    return true;
  }
  if (lower.find("muchos cambios") != std::string::npos ||
      lower.find("mucho cambio") != std::string::npos ||
      lower.find("hay cambios") != std::string::npos ||
      lower.find("hay cambio") != std::string::npos ||
      lower.find("cuantos cambios") != std::string::npos ||
      lower.find("cuántos cambios") != std::string::npos) {
    return true;
  }
  return lower == "git status" || lower == "estado git" || lower == "git status." ||
         lower == "status" || lower == "lista cambios" || starts_with(lower, "lista cambios") ||
         lower == "qué ha cambiado" || lower == "que ha cambiado" ||
         lower == "cambios del proyecto" || lower.find("working tree") != std::string::npos ||
         lower.find("numero de archivos modific") != std::string::npos ||
         lower.find("número de archivos modific") != std::string::npos;
}

bool asks_diff_content(const std::string& lower) {
  return lower.find("diff") != std::string::npos ||
         lower.find("que he cambiado") != std::string::npos ||
         lower.find("qué he cambiado") != std::string::npos ||
         lower.find("que tengo cambiado") != std::string::npos ||
         lower.find("qué tengo cambiado") != std::string::npos ||
         lower.find("cambios tiene") != std::string::npos ||
         lower.find("cambios de cada") != std::string::npos ||
         lower.find("que cambios") != std::string::npos ||
         lower.find("qué cambios") != std::string::npos ||
         (lower.find("cada") != std::string::npos &&
          (lower.find("cambio") != std::string::npos || lower.find("modific") != std::string::npos));
}

bool asks_list_directory(const std::string& lower) {
  if (asks_modified_files(lower) || asks_diff_content(lower)) {
    return false;
  }
  if (lower.find("error") != std::string::npos || lower.find("problema") != std::string::npos ||
      lower.find("diagnostic") != std::string::npos) {
    return false;
  }
  return lower.find("listame") != std::string::npos || lower.find("lístame") != std::string::npos ||
         lower.find("listar") != std::string::npos || lower.find("directorios") != std::string::npos ||
         lower.find("que hay dentro") != std::string::npos ||
         lower.find("qué hay dentro") != std::string::npos ||
         lower.find("archivos que hay") != std::string::npos ||
         (starts_with(lower, "lista ") && lower.find("modific") == std::string::npos &&
          lower.find("cambio") == std::string::npos);
}

bool is_path_stopword(const std::string& token) {
  static const char* kStops[] = {
      "el",         "la",         "los",      "las",        "un",         "una",
      "unos",       "unas",       "de",       "del",        "al",         "en",
      "lo",         "ese",        "esa",      "esos",       "esas",       "este",
      "esta",       "estos",      "estas",    "cada",       "archivo",    "archivos",
      "fichero",    "ficheros",   "file",     "files",      "remoto",     "local",
      "proyecto",   "carpeta",    "directorio","directorios","repo",      "repositorio",
      "mis",        "tus",        "sus",      "mi",         "tu",         "su",
      "uno",        "hay",        "que",      "qué",        "tiene",      "tienen",
      "he",         "tengo",      "modificados","modificado","cambiado",  "cambios",
      "dentro",     "con",        "por",      "para",       "sobre",      "entre",
      "todos",      "todas",      "todo",     "toda"};
  for (const char* s : kStops) {
    if (token == s) {
      return true;
    }
  }
  return false;
}

bool refers_to_previous_paths(const std::string& lower) {
  return lower.find("esos") != std::string::npos || lower.find("esas") != std::string::npos ||
         lower.find("estos") != std::string::npos || lower.find("estas") != std::string::npos ||
         lower.find("los mismos") != std::string::npos ||
         lower.find("las mismas") != std::string::npos;
}

std::string next_path_token(const std::string& s, std::size_t* inout_pos) {
  std::size_t i = *inout_pos;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
    ++i;
  }
  std::string token;
  while (i < s.size()) {
    const char ch = s[i];
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '/' || ch == '.' || ch == '_' ||
        ch == '-') {
      token.push_back(ch);
      ++i;
    } else {
      break;
    }
  }
  *inout_pos = i;
  return token;
}

// Extrae hint de ruta: roots conocidos (por posición en el texto), o token tras markers.
// Si hay varios roots unidos por " o "/" and ", los devuelve separados por coma.
std::string extract_path_hint(const std::string& lower) {
  static const char* kRoots[] = {"src",         "tools",      "tests", "cmake",
                                 "examples",    "docs",       "docker", "third_party",
                                 "thirdparty",  "include",    "lib",    "app"};
  struct Hit {
    std::size_t pos = 0;
    std::string root;
  };
  std::vector<Hit> hits;
  for (const char* root : kRoots) {
    const std::string r(root);
    std::size_t pos = 0;
    while ((pos = lower.find(r, pos)) != std::string::npos) {
      const bool before_ok =
          pos == 0 || !std::isalnum(static_cast<unsigned char>(lower[pos - 1]));
      const bool after_ok = pos + r.size() >= lower.size() ||
                            !std::isalnum(static_cast<unsigned char>(lower[pos + r.size()]));
      if (before_ok && after_ok) {
        hits.push_back(Hit{pos, r});
      }
      pos += r.size();
    }
  }
  if (!hits.empty()) {
    std::sort(hits.begin(), hits.end(),
              [](const Hit& a, const Hit& b) { return a.pos < b.pos; });
    // Deduplicar roots manteniendo orden de aparición.
    std::vector<std::string> ordered;
    for (const auto& h : hits) {
      if (ordered.empty() || ordered.back() != h.root) {
        ordered.push_back(h.root);
      }
    }
    const bool multi = ordered.size() > 1 &&
                       (lower.find(" o ") != std::string::npos ||
                        lower.find(" or ") != std::string::npos ||
                        lower.find(" y ") != std::string::npos ||
                        lower.find(" and ") != std::string::npos);
    if (multi) {
      std::string joined = ordered.front();
      for (std::size_t i = 1; i < ordered.size(); ++i) {
        joined += ",";
        joined += ordered[i];
      }
      return joined;
    }
    return ordered.back();  // el último mencionado en el texto
  }

  const char* markers[] = {"dentro de ", "dentro del ", "en la carpeta ", "en el directorio ",
                           "bajo ", "en "};
  for (const char* marker : markers) {
    const auto pos = lower.find(marker);
    if (pos == std::string::npos) {
      continue;
    }
    std::size_t i = pos + std::strlen(marker);
    while (i < lower.size()) {
      const std::string token = next_path_token(lower, &i);
      if (token.empty()) {
        break;
      }
      if (is_path_stopword(token) || refers_to_previous_paths(token)) {
        continue;
      }
      return token;
    }
  }
  return {};
}

std::string resolve_path_arg(const std::string& lower, const std::string& previous_arg) {
  const std::string path = extract_path_hint(lower);
  if (refers_to_previous_paths(lower) && !previous_arg.empty()) {
    return previous_arg;
  }
  if (!path.empty() && is_path_stopword(path) && !previous_arg.empty()) {
    return previous_arg;
  }
  return path;
}

bool asks_commit_files(const std::string& lower) {
  const bool files = lower.find("archivo") != std::string::npos ||
                     lower.find("fichero") != std::string::npos ||
                     lower.find("files") != std::string::npos ||
                     (lower.find("que ") != std::string::npos &&
                      lower.find("cambio") != std::string::npos);
  if (!files) {
    return false;
  }
  // Working-tree status questions are not commit-show.
  if (lower.find("modificad") != std::string::npos && lower.find("commit") == std::string::npos &&
      lower.find("ultimo") == std::string::npos && lower.find("último") == std::string::npos &&
      lower.find("penultimo") == std::string::npos &&
      lower.find("penúltimo") == std::string::npos) {
    return false;
  }
  return lower.find("commit") != std::string::npos || lower.find("penultimo") != std::string::npos ||
         lower.find("penúltimo") != std::string::npos ||
         ((lower.find("ultimo") != std::string::npos || lower.find("último") != std::string::npos) &&
          (lower.find("cambio") != std::string::npos || lower.find("modific") != std::string::npos ||
           lower.find("archivo") != std::string::npos));
}

bool asks_commit_patch(const std::string& lower, const std::string& previous_tool) {
  // Tras ver archivos de un commit, pedir el patch/hunks.
  if (previous_tool != "git_show" && previous_tool != "git_log") {
    return false;
  }
  // "en concreto … qué archivos" → listado, no patch.
  if ((lower.find("archivo") != std::string::npos || lower.find("fichero") != std::string::npos) &&
      lower.find("cada") == std::string::npos && lower.find("diff") == std::string::npos &&
      lower.find("patch") == std::string::npos && lower.find("contenido") == std::string::npos &&
      lower.find("detalle") == std::string::npos && lower.find("hunk") == std::string::npos) {
    return false;
  }
  return lower.find("concreto") != std::string::npos || lower.find("concretos") != std::string::npos ||
         lower.find("patch") != std::string::npos || lower.find("hunk") != std::string::npos ||
         lower.find("diff") != std::string::npos ||
         lower.find("cada archivo") != std::string::npos ||
         lower.find("cambios de cada") != std::string::npos ||
         lower.find("detalle") != std::string::npos ||
         (lower.find("contenido") != std::string::npos &&
          lower.find("cambio") != std::string::npos);
}

bool asks_git_log_followup_files(const std::string& lower, const std::string& previous_tool) {
  if (previous_tool != "git_log" && previous_tool != "git_show") {
    return false;
  }
  if (asks_commit_patch(lower, previous_tool)) {
    return false;
  }
  return lower.find("archivo") != std::string::npos || lower.find("fichero") != std::string::npos ||
         lower.find("metio") != std::string::npos || lower.find("metió") != std::string::npos ||
         (lower.find("cambio") != std::string::npos &&
          (starts_with(lower, "y ") || starts_with(lower, "and ") ||
           lower.find("que ") != std::string::npos || lower.find("qué ") != std::string::npos ||
           lower.find("ese") != std::string::npos || lower.find("este") != std::string::npos));
}

bool asks_commit_ordinal_followup(const std::string& lower, const std::string& previous_tool) {
  if (previous_tool != "git_log" && previous_tool != "git_show") {
    return false;
  }
  // "y el penúltimo?", "y el anterior?", "el siguiente?"
  if (lower.find("penultimo") != std::string::npos || lower.find("penúltimo") != std::string::npos ||
      lower.find("antepenultimo") != std::string::npos ||
      lower.find("antepenúltimo") != std::string::npos ||
      lower.find("anterior") != std::string::npos) {
    return true;
  }
  // Frases cortas de continuación ordinal.
  if (lower.size() <= 40 &&
      (lower.find("ultimo") != std::string::npos || lower.find("último") != std::string::npos) &&
      lower.find("commit") == std::string::npos && lower.find("main") == std::string::npos &&
      lower.find("master") == std::string::npos && lower.find("archivo") == std::string::npos) {
    return true;
  }
  return false;
}

std::string previous_commit_base(const std::string& previous_tool, const std::string& previous_arg) {
  if (previous_tool != "git_log" && previous_tool != "git_show") {
    return "HEAD";
  }
  std::string base;
  std::string tok;
  for (std::size_t i = 0; i <= previous_arg.size(); ++i) {
    const bool end = i == previous_arg.size() || std::isspace(static_cast<unsigned char>(previous_arg[i]));
    if (!end) {
      tok.push_back(previous_arg[i]);
      continue;
    }
    if (tok.empty()) {
      continue;
    }
    bool digits = true;
    for (char c : tok) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        digits = false;
        break;
      }
    }
    if (!digits && tok != "@active" && tok != "@open" && tok != "patch" && tok != "diff" &&
        tok != "--patch" && tok != "-p") {
      base = tok;
      break;
    }
    tok.clear();
  }
  return base.empty() ? std::string("HEAD") : base;
}

std::string extract_git_log_ref(const std::string& lower) {
  static const char* kRefs[] = {"origin/main", "origin/master", "main", "master", "develop",
                                "dev",         "HEAD",          "head"};
  for (const char* ref : kRefs) {
    const std::string r(ref);
    const auto pos = lower.find(r);
    if (pos == std::string::npos) {
      continue;
    }
    const bool before_ok =
        pos == 0 || !std::isalnum(static_cast<unsigned char>(lower[pos - 1]));
    const bool after_ok = pos + r.size() >= lower.size() ||
                          !std::isalnum(static_cast<unsigned char>(lower[pos + r.size()]));
    if (before_ok && after_ok) {
      return r == "head" ? "HEAD" : r;
    }
  }
  return {};
}

std::string extract_commit_rev(const std::string& lower, const std::string& previous_tool,
                               const std::string& previous_arg) {
  const bool contextual =
      lower.find("ese") != std::string::npos || lower.find("aquel") != std::string::npos ||
      lower.find("este") != std::string::npos || lower.find("esa") != std::string::npos ||
      asks_commit_ordinal_followup(lower, previous_tool) ||
      asks_git_log_followup_files(lower, previous_tool) ||
      asks_commit_patch(lower, previous_tool);

  std::string base;
  if (contextual) {
    base = previous_commit_base(previous_tool, previous_arg);
  }
  if (base.empty()) {
    base = extract_git_log_ref(lower);
  }
  if (base.empty()) {
    base = "HEAD";
  }

  if (lower.find("antepenultimo") != std::string::npos ||
      lower.find("antepenúltimo") != std::string::npos) {
    return base + "~2";
  }
  if (lower.find("penultimo") != std::string::npos || lower.find("penúltimo") != std::string::npos ||
      lower.find("anterior") != std::string::npos) {
    return base + "~1";
  }
  // Hash corto/largo si aparece (y no es solo "ese").
  for (std::size_t i = 0; i < lower.size();) {
    if (!std::isxdigit(static_cast<unsigned char>(lower[i]))) {
      ++i;
      continue;
    }
    std::size_t j = i;
    while (j < lower.size() && std::isxdigit(static_cast<unsigned char>(lower[j]))) {
      ++j;
    }
    const std::size_t len = j - i;
    if (len >= 7 && len <= 40) {
      return lower.substr(i, len);
    }
    i = j;
  }
  // "ese commit" / follow-up de archivos → el rev base del turno anterior.
  if (contextual) {
    return base;
  }
  const std::string named = extract_git_log_ref(lower);
  return named.empty() ? std::string("HEAD") : named;
}

bool asks_git_log(const std::string& lower) {
  // Historial de commits (no working tree / diffs).
  if (asks_commit_files(lower)) {
    return false;
  }
  return lower.find("commit") != std::string::npos || lower.find("commits") != std::string::npos ||
         lower.find("historial") != std::string::npos || lower.find("historico") != std::string::npos ||
         lower.find("histórico") != std::string::npos || lower.find("history") != std::string::npos ||
         lower.find("git log") != std::string::npos ||
         (lower.find("log de") != std::string::npos &&
          (lower.find("git") != std::string::npos || lower.find("main") != std::string::npos ||
           lower.find("master") != std::string::npos));
}

int git_log_default_count(const std::string& lower) {
  const bool plural = lower.find("ultimos") != std::string::npos ||
                      lower.find("últimos") != std::string::npos ||
                      lower.find("commits") != std::string::npos ||
                      lower.find("historial") != std::string::npos ||
                      lower.find("historico") != std::string::npos ||
                      lower.find("histórico") != std::string::npos ||
                      lower.find("history") != std::string::npos;
  const bool singular =
      !plural && (lower.find("ultimo") != std::string::npos ||
                  lower.find("último") != std::string::npos ||
                  (lower.find("commit") != std::string::npos &&
                   lower.find("commits") == std::string::npos));
  return singular ? 1 : 12;
}

std::string build_git_log_arg(const std::string& lower) {
  // Historial de un archivo concreto / abierto.
  if (lower.find("archivo") != std::string::npos &&
      (lower.find("abierto") != std::string::npos || lower.find("activo") != std::string::npos ||
       lower.find("actual") != std::string::npos)) {
    return "@active " + std::to_string(git_log_default_count(lower));
  }
  const std::string path = extract_path_hint(lower);
  if (!path.empty() && (lower.find("archivo") != std::string::npos ||
                        lower.find("histor") != std::string::npos ||
                        path.find('.') != std::string::npos)) {
    return path + " " + std::to_string(git_log_default_count(lower));
  }
  std::string arg = extract_git_log_ref(lower);
  const int n = git_log_default_count(lower);
  if (!arg.empty()) {
    arg += ' ';
  }
  arg += std::to_string(n);
  return arg;
}

bool asks_git_branches(const std::string& lower) {
  // No confundir "commits de main" / archivos de commit con listado de ramas.
  if (asks_git_log(lower) || asks_commit_files(lower)) {
    return false;
  }
  return lower.find("rama") != std::string::npos || lower.find("branch") != std::string::npos ||
         lower.find("branches") != std::string::npos;
}

bool looks_like_dir_followup(const std::string& lower) {
  if (asks_git_branches(lower) || asks_git_log(lower) || asks_commit_files(lower) ||
      asks_list_directory(lower)) {
    return false;
  }
  // Permitir "y en tools? tengo muchos?" aunque mencione cambios de forma vaga.
  const bool casual_count =
      lower.find("muchos") != std::string::npos || lower.find("cuantos") != std::string::npos ||
      lower.find("cuántos") != std::string::npos;
  if (asks_modified_files(lower) && !casual_count) {
    return false;
  }
  // "y qué cambios hay dentro de app?" tras git_status: path follow-up, no bloquear por diff.
  if (asks_diff_content(lower) && extract_path_hint(lower).empty()) {
    return false;
  }
  if (lower.find("pull") != std::string::npos || lower.find("compila") != std::string::npos ||
      lower.find("busca") != std::string::npos || lower.find("search") != std::string::npos) {
    return false;
  }
  if (lower.size() > 80) {
    return false;
  }
  const std::string path = extract_path_hint(lower);
  if (path.empty()) {
    return false;
  }
  return starts_with(lower, "y ") || starts_with(lower, "and ") || starts_with(lower, "ahora ") ||
         starts_with(lower, "también ") || starts_with(lower, "tambien ") ||
         lower.find("dentro de") != std::string::npos || starts_with(lower, "en ") ||
         starts_with(lower, "bajo ");
}

}  // namespace

AiRouteResult route_level0(const std::string& raw_input, const std::string& previous_tool,
                           const std::string& previous_arg, const Level0SemanticMatcher& semantic) {
  const std::string input = trim(raw_input);
  if (input.empty()) {
    AiRouteResult r;
    r.kind = AiRouteKind::Error;
    r.message = "mensaje vacío";
    return r;
  }

  if (input[0] == '/') {
    const std::string body = input.substr(1);
    const std::string cmd = to_lower(body.substr(0, body.find(' ')));
    const std::string arg = after_first_space(body);

    if (cmd == "help" || cmd == "h" || cmd == "?") {
      AiRouteResult r;
      r.kind = AiRouteKind::Help;
      return r;
    }
    if (cmd == "build" || cmd == "compile") {
      return task("compile");
    }
    if (cmd == "launch" || cmd == "run") {
      return task("launch");
    }
    if (cmd == "search" || cmd == "rg" || cmd == "grep") {
      if (arg.empty()) {
        AiRouteResult r;
        r.kind = AiRouteKind::Error;
        r.message = "uso: /search <texto>";
        return r;
      }
      return tool("search", arg);
    }
    if (cmd == "diag" || cmd == "diagnostics" || cmd == "errors") {
      return tool("diagnostics", arg);
    }
    if (cmd == "git") {
      const std::string git_arg = to_lower(arg);
      if (git_arg.empty() || git_arg == "status") {
        return tool("git_status");
      }
      if (git_arg == "pull" || starts_with(git_arg, "pull ")) {
        return tool("git_pull");
      }
      if (git_arg == "branch" || git_arg == "branches" || git_arg == "rama" ||
          git_arg == "ramas" || starts_with(git_arg, "branch ")) {
        return tool("git_branches");
      }
      if (git_arg == "log" || starts_with(git_arg, "log ") || git_arg == "history" ||
          git_arg == "commits" || starts_with(git_arg, "log")) {
        return tool("git_log", after_first_space(arg));
      }
      if (git_arg == "show" || starts_with(git_arg, "show ")) {
        const std::string show_arg = after_first_space(arg);
        return tool("git_show", show_arg.empty() ? std::string("HEAD") : show_arg);
      }
      AiRouteResult r;
      r.kind = AiRouteKind::Error;
      r.message = "uso: /git [status|pull|branch|log|show]";
      return r;
    }
    if (cmd == "status") {
      return tool("git_status");
    }
    if (cmd == "pull") {
      return tool("git_pull");
    }
    if (cmd == "branches" || cmd == "branch" || cmd == "ramas") {
      return tool("git_branches");
    }
    if (cmd == "read" || cmd == "cat") {
      if (arg.empty()) {
        AiRouteResult r;
        r.kind = AiRouteKind::Error;
        r.message = "uso: /read <ruta>";
        return r;
      }
      return tool("read_file", arg);
    }
    if (cmd == "ls" || cmd == "list") {
      return tool("list_files", arg);
    }
    if (cmd == "symbols" || cmd == "ws") {
      if (arg.empty()) {
        AiRouteResult r;
        r.kind = AiRouteKind::Error;
        r.message = "uso: /symbols <query>";
        return r;
      }
      return tool("workspace_symbols", arg);
    }
    if (cmd == "hover") {
      return tool("hover", arg);
    }
    if (cmd == "context" || cmd == "pack") {
      AiRouteResult r = tool("context_pack", arg);
      r.seeds = extract_literal_seeds(arg.empty() ? input : arg);
      return r;
    }
    if (cmd == "contextdump" || cmd == "context_dump" || cmd == "dumpctx") {
      AiRouteResult r;
      r.kind = AiRouteKind::ForceLevel1;
      r.arg = arg.empty() ? std::string("dame contexto") : ("dame contexto de " + arg);
      return r;
    }
    if (cmd == "codeof" || cmd == "get_code_of" || cmd == "code") {
      if (arg.empty()) {
        AiRouteResult r;
        r.kind = AiRouteKind::Error;
        r.message = "uso: /codeof <path:Symbol|path:line|Symbol>";
        return r;
      }
      return tool("get_code_of", arg);
    }
    if (cmd == "repomap" || cmd == "repo_map" || cmd == "map") {
      return tool("repo_map", arg);
    }
    if (cmd == "apply_demo" || cmd == "demo") {
      return tool("apply_demo");
    }
    if (cmd == "tools") {
      return tool("list_tools");
    }
    if (cmd == "l1" || cmd == "agent") {
      AiRouteResult r;
      r.kind = AiRouteKind::ForceLevel1;
      r.arg = arg.empty() ? std::string("ayuda") : arg;
      return r;
    }
    if (cmd == "cancel" || cmd == "stop") {
      AiRouteResult r;
      r.kind = AiRouteKind::CancelAgent;
      return r;
    }
    if (cmd == "model" || cmd == "models") {
      if (arg.empty() || to_lower(arg) == "status") {
        AiRouteResult r;
        r.kind = AiRouteKind::ModelStatus;
        return r;
      }
      if (to_lower(arg) == "download" || to_lower(arg) == "pull") {
        AiRouteResult r;
        r.kind = AiRouteKind::ModelDownload;
        r.arg = "model";
        return r;
      }
      if (to_lower(arg) == "download_runtime" || to_lower(arg) == "runtime") {
        AiRouteResult r;
        r.kind = AiRouteKind::ModelDownload;
        r.arg = "runtime";
        return r;
      }
      if (to_lower(arg) == "download_embed" || to_lower(arg) == "embed" ||
          to_lower(arg) == "intent" || to_lower(arg) == "l0") {
        AiRouteResult r;
        r.kind = AiRouteKind::ModelDownload;
        r.arg = "embed";
        return r;
      }
      AiRouteResult r;
      r.kind = AiRouteKind::Error;
      r.message = "uso: /model [status|download|download_runtime|download_embed]";
      return r;
    }
    if (cmd == "trace") {
      AiRouteResult r;
      r.kind = AiRouteKind::Trace;
      r.arg = to_lower(arg);
      return r;
    }
    if (cmd == "explain" || cmd == "ask") {
      AiRouteResult r;
      r.kind = AiRouteKind::ForceLevel1;
      r.arg = arg.empty() ? std::string("explica el contexto del editor") : arg;
      return r;
    }
    if (cmd == "l2") {
      return escalate("L2 coder: usa L1 (needs_level2) o /l1; dry-run hasta Fase E");
    }
    AiRouteResult r;
    r.kind = AiRouteKind::Error;
    r.message = "comando desconocido: /" + cmd + " (usa /help)";
    return r;
  }

  const std::string lower = to_lower(input);

  // Follow-ups / commits estructurales (antes que semantic o keywords).
  if (asks_commit_patch(lower, previous_tool)) {
    return tool("git_show", extract_commit_rev(lower, previous_tool, previous_arg) + " patch");
  }
  if (asks_commit_files(lower) || asks_git_log_followup_files(lower, previous_tool) ||
      asks_commit_ordinal_followup(lower, previous_tool)) {
    return tool("git_show", extract_commit_rev(lower, previous_tool, previous_arg));
  }
  if ((previous_tool == "git_status" || previous_tool == "git_diff") &&
      looks_like_dir_followup(lower)) {
    const std::string path = resolve_path_arg(lower, previous_arg);
    // "y qué cambios hay dentro de X?" → diff acotado; "y dentro de X?" → mismo tool.
    if (asks_diff_content(lower)) {
      return tool("git_diff", path);
    }
    return tool(previous_tool == "git_diff" ? "git_diff" : "git_status", path);
  }

  // Dame contexto → L1 dump con cuerpos TS (no context_pack ligero).
  if (query_asks_context_dump(lower)) {
    return escalate("contexto: Nivel 1 elabora mapa rankeado (sin bodies; L2 elige)");
  }

  // Localizar / investigar código → L1 con REPO_MAP (no ripgrep L0).
  if (asks_code_location(lower) || asks_conceptual_code_topic(lower)) {
    return escalate("investigar: Nivel 1 responde con métodos del REPO_MAP");
  }

  // Semantic intent matching (pretrained embeddings) when ready.
  // On miss: fall through to keyword heuristics (do not hard-escalate).
  if (semantic) {
    const Level0IntentMatch m = semantic(input);
    if (m.ok) {
      // "dónde está el código que gestiona git" no debe matchear git_status por embeddings.
      if (asks_code_location(lower) && is_git_repo_tool(m.name)) {
        // fall through
      } else if (m.arg_policy == "seeds") {
        const auto seeds = extract_literal_seeds(input);
        if (!seeds.empty()) {
          AiRouteResult r = tool(m.name);
          r.seeds = seeds;
          r.arg = seeds.front();
          for (std::size_t i = 1; i < seeds.size(); ++i) {
            r.arg += " " + seeds[i];
          }
          return r;
        }
        // Empty seeds → fall through to keywords.
      } else {
        std::string arg;
        if (m.arg_policy == "path_hint") {
          arg = resolve_path_arg(lower, previous_arg);
        } else if (m.arg_policy == "git_log_arg") {
          arg = build_git_log_arg(lower);
        } else if (m.arg_policy == "commit_rev") {
          arg = extract_commit_rev(lower, previous_tool, previous_arg);
        } else if (m.arg_policy == "commit_rev_patch") {
          arg = extract_commit_rev(lower, previous_tool, previous_arg) + " patch";
        } else if (m.arg_policy == "search_query") {
          if (asks_code_location(lower) || asks_conceptual_code_topic(lower)) {
            return escalate("investigar: Nivel 1 responde con métodos del REPO_MAP");
          }
          const auto sp = input.find(' ');
          arg = sp == std::string::npos ? input : trim(input.substr(sp + 1));
          const auto seeds = extract_literal_seeds(arg);
          if (seeds.empty() && arg.find(' ') != std::string::npos && arg.size() > 24) {
            return escalate("búsqueda conceptual: Nivel 1 dicta needles");
          }
        }
        if (m.is_task) {
          return task(m.name);
        }
        return tool(m.name, arg);
      }
    }
  }

  if (lower == "compila" || lower == "compile" || lower == "build" ||
      starts_with(lower, "compila ") || starts_with(lower, "compile ") ||
      starts_with(lower, "build ")) {
    return task("compile");
  }
  if (lower == "lanza" || lower == "launch" || lower == "run" || starts_with(lower, "lanza ") ||
      starts_with(lower, "launch ") || starts_with(lower, "ejecuta")) {
    return task("launch");
  }
  // Ramas (antes que status: "rama remota" no es status).
  if (asks_git_branches(lower)) {
    return tool("git_branches");
  }
  // Historial / último(s) commit(s) — antes que status/diff.
  if (asks_git_log(lower)) {
    return tool("git_log", build_git_log_arg(lower));
  }
  // Actualizar remoto → git pull (antes que status: "git pull … cambios").
  if (lower == "git pull" || lower == "pull" || lower == "actualiza el git" ||
      lower == "actualizar git" || lower == "actualiza git" || lower == "actualizar el git" ||
      lower == "update git" || lower == "refresh git" || lower == "git refresh" ||
      lower.find("git pull") != std::string::npos ||
      lower.find("git pul") != std::string::npos ||  // typo: pulll
      (lower.find("actualiza") != std::string::npos &&
       lower.find("git") != std::string::npos) ||
      (lower.find("actualizar") != std::string::npos &&
       lower.find("git") != std::string::npos) ||
      (lower.find("actualizate") != std::string::npos &&
       lower.find("git") != std::string::npos) ||
      (starts_with(lower, "haz") && lower.find("pull") != std::string::npos) ||
      (lower.find("pull") != std::string::npos && lower.find("git") != std::string::npos)) {
    return tool("git_pull");
  }
  // Listado de carpeta del FS (no confundir con git status).
  if (asks_list_directory(lower)) {
    return tool("list_files", resolve_path_arg(lower, previous_arg));
  }
  // Diffs / "qué he cambiado en cada archivo".
  if (asks_diff_content(lower)) {
    return tool("git_diff", resolve_path_arg(lower, previous_arg));
  }
  // Status / listado de cambios locales (NO pull). Acepta path hint en arg.
  if (asks_modified_files(lower)) {
    return tool("git_status", resolve_path_arg(lower, previous_arg));
  }
  if (lower == "lista errores" || lower == "list errors" || lower == "diagnostics" ||
      lower == "errores" || lower == "problemas") {
    return tool("diagnostics");
  }
  if (starts_with(lower, "busca ") || starts_with(lower, "search ") ||
      starts_with(lower, "buscar ")) {
    const std::string q = trim(input.substr(input.find(' ') + 1));
    if (q.empty()) {
      AiRouteResult r;
      r.kind = AiRouteKind::Error;
      r.message = "indica qué buscar";
      return r;
    }
    // Only literal/code-like needles; conceptual NL → escalate (D17 S1/S2).
    const auto seeds = extract_literal_seeds(q);
    if (seeds.empty() && q.find(' ') != std::string::npos && q.size() > 24) {
      return escalate("búsqueda conceptual: requiere Nivel 1 para dictar QuerySeeds");
    }
    return tool("search", q);
  }

  const auto seeds = extract_literal_seeds(input);
  // "dame contexto de Foo" must escalate to L1 dump even if Foo is a literal seed.
  if (query_asks_context_dump(lower)) {
    return escalate("contexto: Nivel 1 elabora mapa rankeado (sin bodies; L2 elige)");
  }
  if (!seeds.empty() && (starts_with(lower, "contexto") || starts_with(lower, "context") ||
                         starts_with(lower, "pack"))) {
    AiRouteResult r = tool("context_pack");
    r.seeds = seeds;
    r.arg = seeds.front();
    for (std::size_t i = 1; i < seeds.size(); ++i) {
      r.arg += " " + seeds[i];
    }
    return r;
  }

  return escalate();
}

}  // namespace tuide
