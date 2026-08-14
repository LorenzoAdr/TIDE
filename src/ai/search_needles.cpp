#include "ai/search_needles.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace tuide {
namespace {

std::string trim_copy(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::string snake_to_camel(const std::string& snake) {
  std::string out;
  bool up = true;
  for (char ch : snake) {
    if (ch == '_' || ch == '-' || ch == ' ') {
      up = true;
      continue;
    }
    if (up) {
      out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
      up = false;
    } else {
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
  }
  return out;
}

std::string camel_to_snake(const std::string& camel) {
  std::string out;
  for (std::size_t i = 0; i < camel.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(camel[i]);
    if (std::isupper(c) && i > 0) {
      out.push_back('_');
    }
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

bool looks_camel(const std::string& s) {
  bool has_upper = false;
  bool has_lower = false;
  for (unsigned char c : s) {
    if (std::isupper(c)) {
      has_upper = true;
    }
    if (std::islower(c)) {
      has_lower = true;
    }
  }
  return has_upper && has_lower && s.find('_') == std::string::npos;
}

void push_unique(std::vector<std::string>* out, std::unordered_set<std::string>* seen,
                 const std::string& needle) {
  if (out == nullptr || seen == nullptr || needle.size() < 2) {
    return;
  }
  if (!seen->insert(needle).second) {
    return;
  }
  out->push_back(needle);
}

}  // namespace

std::vector<std::string> split_search_needles(const std::string& arg) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&] {
    const std::string t = trim_copy(cur);
    cur.clear();
    if (t.size() >= 2) {
      out.push_back(t);
    }
  };
  for (char ch : arg) {
    if (ch == '|' || ch == '\n' || ch == ';') {
      flush();
    } else if (ch == ',' && cur.find(' ') != std::string::npos) {
      // "a, b" style — only split on comma when used as list separator with spaces.
      flush();
    } else {
      cur.push_back(ch);
    }
  }
  flush();
  // Also support plain "a,b,c" without spaces.
  if (out.size() == 1 && arg.find('|') == std::string::npos && arg.find('\n') == std::string::npos) {
    const auto amp = arg.find(',');
    if (amp != std::string::npos && arg.find(' ') == std::string::npos) {
      out.clear();
      cur.clear();
      for (char ch : arg) {
        if (ch == ',') {
          flush();
        } else {
          cur.push_back(ch);
        }
      }
      flush();
    }
  }
  return out;
}

std::vector<std::string> expand_identifier_variants(const std::string& needle) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  const std::string n = trim_copy(needle);
  push_unique(&out, &seen, n);
  if (n.size() < 3) {
    return out;
  }

  if (n.find('_') != std::string::npos) {
    std::vector<std::string> parts;
    std::string cur;
    for (char ch : n) {
      if (ch == '_') {
        if (!cur.empty()) {
          parts.push_back(cur);
          cur.clear();
        }
      } else {
        cur.push_back(ch);
      }
    }
    if (!cur.empty()) {
      parts.push_back(cur);
    }
    if (parts.size() == 2) {
      push_unique(&out, &seen, parts[1] + "_" + parts[0]);
      push_unique(&out, &seen, snake_to_camel(n));
      push_unique(&out, &seen, snake_to_camel(parts[1] + "_" + parts[0]));
    } else if (parts.size() >= 3) {
      // Rotate first/last for longer compounds: a_b_c → c_a_b lightly; keep camel of original.
      push_unique(&out, &seen, snake_to_camel(n));
      std::string rev;
      for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) {
          rev.push_back('_');
        }
        rev += parts[parts.size() - 1 - i];
      }
      push_unique(&out, &seen, rev);
      push_unique(&out, &seen, snake_to_camel(rev));
    }
  } else if (looks_camel(n)) {
    const std::string snake = camel_to_snake(n);
    push_unique(&out, &seen, snake);
    // Also reverse snake parts if exactly two.
    const auto us = snake.find('_');
    if (us != std::string::npos && snake.find('_', us + 1) == std::string::npos) {
      push_unique(&out, &seen, snake.substr(us + 1) + "_" + snake.substr(0, us));
    }
  }

  return out;
}

std::vector<std::string> expand_search_needles(const std::string& arg, std::size_t max_n) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  auto seeds = split_search_needles(arg);
  if (seeds.empty() && !trim_copy(arg).empty()) {
    seeds.push_back(trim_copy(arg));
  }
  for (const auto& seed : seeds) {
    for (const auto& v : expand_identifier_variants(seed)) {
      push_unique(&out, &seen, v);
      if (out.size() >= max_n) {
        return out;
      }
    }
  }
  return out;
}

std::string ascii_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

int filename_seed_match_score(const std::string& relative_path,
                              const std::vector<std::string>& needles) {
  if (relative_path.empty() || needles.empty()) {
    return 0;
  }
  const auto slash = relative_path.find_last_of('/');
  const std::string base =
      slash == std::string::npos ? relative_path : relative_path.substr(slash + 1);
  const auto dot = base.find_last_of('.');
  const std::string stem = dot == std::string::npos ? base : base.substr(0, dot);
  const std::string path_l = ascii_lower(relative_path);
  const std::string base_l = ascii_lower(base);
  const std::string stem_l = ascii_lower(stem);

  int best = 0;
  for (const auto& needle : needles) {
    if (needle.size() < 3) {
      continue;
    }
    const std::string n = ascii_lower(needle);
    int score = 0;
    if (stem_l == n) {
      score = 240 + static_cast<int>(n.size()) * 4;
    } else if (stem_l.rfind(n + "_", 0) == 0 || stem_l.find("_" + n) != std::string::npos ||
               stem_l.find(n) != std::string::npos) {
      // Prefer compound id tokens in the stem (file_tree_panel ↔ file_tree).
      score = 170 + static_cast<int>(n.size()) * 5;
    } else if (base_l.find(n) != std::string::npos) {
      score = 130 + static_cast<int>(n.size()) * 3;
    } else if (path_l.find(n) != std::string::npos) {
      score = 55 + static_cast<int>(n.size()) * 2;
    }
    if (score > best) {
      best = score;
    }
  }
  return best;
}

bool query_asks_code_location(const std::string& text) {
  const std::string lower = ascii_lower(text);

  // "busca PayloadBuilder" / "search Foo" → literal L0 search, not investigate.
  auto literal_busca_ident = [&]() -> bool {
    static const char* kPref[] = {"busca ", "buscar ", "search ", "find "};
    for (const char* pref : kPref) {
      const std::size_t n = std::char_traits<char>::length(pref);
      if (lower.size() <= n || lower.compare(0, n, pref) != 0) {
        continue;
      }
      std::size_t i = n;
      while (i < lower.size() && std::isspace(static_cast<unsigned char>(lower[i]))) {
        ++i;
      }
      if (i >= lower.size()) {
        return false;
      }
      std::size_t j = i;
      while (j < lower.size() &&
             (std::isalnum(static_cast<unsigned char>(lower[j])) || lower[j] == '_' ||
              lower[j] == ':')) {
        ++j;
      }
      while (j < lower.size() && std::isspace(static_cast<unsigned char>(lower[j]))) {
        ++j;
      }
      // Single identifier token, nothing else.
      if (j == lower.size() && j - i >= 3) {
        return true;
      }
    }
    return false;
  };
  if (literal_busca_ident()) {
    return false;
  }

  auto has_token = [&](std::string_view tok) {
    if (tok.empty()) {
      return false;
    }
    std::size_t pos = 0;
    while ((pos = lower.find(tok, pos)) != std::string::npos) {
      const bool before =
          pos == 0 || !std::isalnum(static_cast<unsigned char>(lower[pos - 1]));
      const bool after = pos + tok.size() >= lower.size() ||
                         !std::isalnum(static_cast<unsigned char>(lower[pos + tok.size()]));
      if (before && after) {
        return true;
      }
      ++pos;
    }
    return false;
  };

  const bool where = lower.find("dónde") != std::string::npos ||
                     lower.find("donde") != std::string::npos ||
                     lower.find("where is") != std::string::npos ||
                     lower.find("where's") != std::string::npos ||
                     lower.find("where are") != std::string::npos ||
                     lower.find("en qué archivo") != std::string::npos ||
                     lower.find("en que archivo") != std::string::npos ||
                     lower.find("qué archivo") != std::string::npos ||
                     lower.find("que archivo") != std::string::npos ||
                     lower.find("which file") != std::string::npos ||
                     lower.find("what file") != std::string::npos;
  // Short EN tokens use word boundaries so PayloadBuilder ≠ "build".
  const bool codeish = lower.find("código") != std::string::npos ||
                       lower.find("codigo") != std::string::npos ||
                       has_token("code") ||
                       lower.find("implement") != std::string::npos ||
                       lower.find("funcionalidad") != std::string::npos ||
                       has_token("feature") ||
                       lower.find("investig") != std::string::npos ||
                       lower.find("gestiona") != std::string::npos ||
                       lower.find("gestión") != std::string::npos ||
                       lower.find("gestion") != std::string::npos ||
                       lower.find("maneja") != std::string::npos ||
                       lower.find("manejo") != std::string::npos ||
                       has_token("lsp") ||
                       lower.find("call_hierarchy") != std::string::npos ||
                       lower.find("call hierarchy") != std::string::npos ||
                       lower.find("jerarqu") != std::string::npos ||
                       lower.find("hierarchy") != std::string::npos ||
                       has_token("gutter") ||
                       has_token("panel") ||
                       lower.find("módulo") != std::string::npos ||
                       lower.find("modulo") != std::string::npos ||
                       lower.find("clase") != std::string::npos ||
                       lower.find("función") != std::string::npos ||
                       lower.find("funcion") != std::string::npos ||
                       lower.find("método") != std::string::npos ||
                       lower.find("metodo") != std::string::npos ||
                       lower.find("fuente") != std::string::npos ||
                       has_token("source") ||
                       lower.find("compil") != std::string::npos ||
                       has_token("compile") ||
                       has_token("build") ||
                       has_token("cmake") ||
                       has_token("makefile") ||
                       has_token("script");
  if (where && codeish) {
    return true;
  }
  // "dónde está X" / "dónde se hace X" / "where is X" without an explicit code word still means locate.
  if (where && (lower.find("está") != std::string::npos || lower.find("esta") != std::string::npos ||
                lower.find("hace") != std::string::npos || lower.find(" is ") != std::string::npos)) {
    return true;
  }
  if ((lower.find("busca") != std::string::npos || lower.find("buscar") != std::string::npos ||
       lower.find("localiza") != std::string::npos || lower.find("find") != std::string::npos ||
       lower.find("investig") != std::string::npos) &&
      codeish) {
    return true;
  }
  // Follow-ups: "y la gestión del gutter?", "and the hover?"
  if ((lower.rfind("y ", 0) == 0 || lower.rfind("and ", 0) == 0 ||
       lower.rfind("what about", 0) == 0 || lower.rfind("how about", 0) == 0) &&
      codeish) {
    return true;
  }
  return lower.find("busca el código") != std::string::npos ||
         lower.find("busca el codigo") != std::string::npos ||
         lower.find("busca en el código") != std::string::npos ||
         lower.find("busca en el codigo") != std::string::npos ||
         lower.find("en el código") != std::string::npos ||
         lower.find("en el codigo") != std::string::npos ||
         lower.find("find the code") != std::string::npos ||
         lower.find("localizar el código") != std::string::npos ||
         lower.find("localizar el codigo") != std::string::npos ||
         lower.find("investigar el código") != std::string::npos ||
         lower.find("investigar el codigo") != std::string::npos ||
         lower.find("investigate the code") != std::string::npos;
}

bool query_asks_context_dump(const std::string& text) {
  std::string lower = ascii_lower(text);
  // Common typos for "contexto".
  auto replace_all = [&](const char* from, const char* to) {
    const std::size_t from_n = std::char_traits<char>::length(from);
    const std::size_t to_n = std::char_traits<char>::length(to);
    for (;;) {
      const auto pos = lower.find(from);
      if (pos == std::string::npos) {
        break;
      }
      lower.replace(pos, from_n, to, to_n);
    }
  };
  replace_all("conexto", "contexto");
  replace_all("contexo", "contexto");
  replace_all("contxto", "contexto");

  // Strong / explicit dumps only — avoid bare "contexto de" / "código de" false positives.
  if (lower.find("dame contexto") != std::string::npos ||
      lower.find("dame el contexto") != std::string::npos ||
      lower.find("give me context") != std::string::npos ||
      lower.find("dump context") != std::string::npos ||
      lower.find("context dump") != std::string::npos) {
    return true;
  }
  if (lower.find("dame código de") != std::string::npos ||
      lower.find("dame codigo de") != std::string::npos) {
    return true;
  }

  // "código de Foo" / "context of Foo" only when followed by an identifier-like token.
  auto followed_by_ident = [&](std::string_view marker) {
    const auto pos = lower.find(marker);
    if (pos == std::string::npos) {
      return false;
    }
    std::size_t i = pos + marker.size();
    while (i < lower.size() && std::isspace(static_cast<unsigned char>(lower[i]))) {
      ++i;
    }
    if (i >= lower.size() ||
        !(std::isalpha(static_cast<unsigned char>(lower[i])) || lower[i] == '_')) {
      return false;
    }
    std::size_t n = 0;
    while (i + n < lower.size() &&
           (std::isalnum(static_cast<unsigned char>(lower[i + n])) || lower[i + n] == '_' ||
            lower[i + n] == ':' || lower[i + n] == '/' || lower[i + n] == '.')) {
      ++n;
    }
    return n >= 3;
  };
  return followed_by_ident("código de ") || followed_by_ident("codigo de ") ||
         followed_by_ident("context of ");
}

bool query_asks_code_edit(const std::string& text) {
  const std::string lower = ascii_lower(text);

  // Pure VCS / status must not become "edit".
  const bool vcs =
      lower.find("git status") != std::string::npos || lower.find("git diff") != std::string::npos ||
      lower.find("git pull") != std::string::npos || lower.find("git commit") != std::string::npos ||
      (lower.find("commit") != std::string::npos && lower.find("git") != std::string::npos) ||
      lower.find("working tree") != std::string::npos ||
      lower.find("archivos modific") != std::string::npos;
  if (vcs) {
    return false;
  }

  auto has = [&](std::string_view tok) { return lower.find(tok) != std::string::npos; };

  const bool addish = has("añade") || has("anade") || has("agrega") || has("agregar") ||
                      has("añadir") || has("anadir") || has("crea ") || has("crear ") ||
                      has("implementa") || has("implement ") || has("pon ") || has("poner ") ||
                      has("mete ") || has("inserta") || has("new tab") || has("add a tab") ||
                      has("add tab") || has("add a new");
  const bool changeish = has("cambia") || has("cambiar") || has("modifica") || has("modificar") ||
                         has("renombra") || has("rename") || has("actualiza el label") ||
                         has("change the") || has("update the label");
  const bool uish = has("pestaña") || has("pestana") || has("tab ") || has(" tab") ||
                    has("nuevo tab") || has("nueva pest") || has("panel") || has("consola") ||
                    has("terminal") || has("botón") || has("boton") || has("texto fijo") ||
                    has("label") || has("console.tab") || has("ui ");
  const bool codeish = has("código") || has("codigo") || has("fuente") || has("archivo .cpp") ||
                       has("en el código") || has("en el codigo") || has("src/");

  if ((addish || changeish) && (uish || codeish)) {
    return true;
  }
  // "haz que el panel muestre X" / "quiero un tab llamado…"
  if ((has("quiero") || has("necesito") || has("haz que")) && uish &&
      (addish || changeish || has("llamad") || has("llame") || has("nombre"))) {
    return true;
  }
  return false;
}

bool query_asks_git_repo(const std::string& text) {
  const std::string lower = ascii_lower(text);
  const bool vcs_op = lower.find("commit") != std::string::npos ||
                      lower.find("diff") != std::string::npos ||
                      lower.find("branch") != std::string::npos ||
                      lower.find("rama") != std::string::npos ||
                      lower.find("pull") != std::string::npos ||
                      lower.find("git status") != std::string::npos ||
                      lower.find("working tree") != std::string::npos ||
                      lower.find("archivos modific") != std::string::npos ||
                      lower.find("modified files") != std::string::npos ||
                      lower.find("unstaged") != std::string::npos;

  // "gutter de git" / resalte de líneas editadas: feature de editor, no tools VCS.
  const bool git_ui_feature =
      lower.find("gutter") != std::string::npos || lower.find("blame") != std::string::npos ||
      ((lower.find("git") != std::string::npos) &&
       (lower.find("resalte") != std::string::npos || lower.find("realce") != std::string::npos ||
        lower.find("editada") != std::string::npos || lower.find("editado") != std::string::npos ||
        lower.find("decorat") != std::string::npos || lower.find("highlight") != std::string::npos));
  if (git_ui_feature && !vcs_op) {
    return false;
  }

  if (query_asks_code_location(lower) && lower.find("git") == std::string::npos && !vcs_op &&
      lower.find("status") == std::string::npos) {
    return false;
  }
  if (query_asks_context_dump(lower) && !vcs_op &&
      (lower.find("git") == std::string::npos || git_ui_feature)) {
    return false;
  }
  return lower.find("git") != std::string::npos || vcs_op ||
         lower.find("status") != std::string::npos;
}

bool is_git_repo_tool_name(const std::string& name) {
  return name == "git_status" || name == "git_diff" || name == "git_pull" ||
         name == "git_branches" || name == "git_log" || name == "git_show";
}

std::vector<std::string> extract_code_tokens(const std::string& text, std::size_t max_n) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  std::string cur;
  auto flush = [&] {
    if (cur.size() < 3) {
      cur.clear();
      return;
    }
    const bool snake = cur.find('_') != std::string::npos;
    bool camel = false;
    bool has_lower = false;
    bool has_upper = false;
    for (unsigned char c : cur) {
      if (std::islower(c)) {
        has_lower = true;
      }
      if (std::isupper(c)) {
        has_upper = true;
      }
    }
    camel = has_lower && has_upper;
    // Keep ALLCAPS ids like LSP if length >= 3, and snake/Camel.
    const bool all_upper = has_upper && !has_lower && cur.size() >= 3;
    if (!(snake || camel || all_upper)) {
      cur.clear();
      return;
    }
    push_unique(&out, &seen, cur);
    cur.clear();
  };
  for (char ch : text) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (std::isalnum(c) || ch == '_') {
      cur.push_back(ch);
    } else {
      flush();
    }
    if (out.size() >= max_n) {
      return out;
    }
  }
  flush();
  return out;
}

int score_search_hit(const std::string& relative_path, const std::string& matching_needle,
                     const std::vector<std::string>& all_needles) {
  int score = 0;
  if (relative_path.rfind("src/", 0) == 0) {
    score += 100;
  } else if (relative_path.rfind("include/", 0) == 0) {
    score += 90;
  } else if (relative_path.rfind("tests/", 0) == 0) {
    score += 20;
  } else if (relative_path.rfind("docs/", 0) == 0) {
    score -= 40;
  }

  score += filename_seed_match_score(relative_path, all_needles);

  // Extra nudge if this hit's needle itself appears in the name.
  if (!matching_needle.empty()) {
    const std::vector<std::string> one{matching_needle};
    const int self = filename_seed_match_score(relative_path, one);
    if (self > 0) {
      score += 15;
    }
  }

  const auto slash = relative_path.find_last_of('/');
  const std::string base =
      slash == std::string::npos ? relative_path : relative_path.substr(slash + 1);
  const auto dot = base.find_last_of('.');
  const std::string ext = dot == std::string::npos ? std::string{} : base.substr(dot);
  if (ext == ".hpp" || ext == ".h" || ext == ".hh") {
    score += 25;
  } else if (ext == ".cpp" || ext == ".cc" || ext == ".c") {
    score += 15;
  } else if (ext == ".md") {
    score -= 20;
  }
  return score;
}

std::vector<std::string> expand_nl_retrieval_tokens(const std::vector<std::string>& tokens,
                                                   std::size_t max_n) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  auto push = [&](std::string s) {
    for (char& c : s) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (s.size() < 3 || !seen.insert(s).second) {
      return;
    }
    out.push_back(std::move(s));
  };
  for (const auto& t : tokens) {
    push(t);
  }

  // General bilingual / synonym bridge for code retrieval (NOT project file aliases).
  auto add_if_has = [&](std::initializer_list<const char*> triggers,
                        std::initializer_list<const char*> extras) {
    bool hit = false;
    for (const char* tr : triggers) {
      for (const auto& t : tokens) {
        if (t == tr || t.find(tr) != std::string::npos) {
          hit = true;
          break;
        }
      }
      if (hit) {
        break;
      }
    }
    // Also scan raw joined form via tokens already pushed.
    if (!hit) {
      return;
    }
    for (const char* ex : extras) {
      push(ex);
    }
  };

  add_if_has({"cierre", "cerrar", "cierro", "salir", "salida", "quit"},
             {"quit", "close", "exit", "shutdown", "confirm", "overlay"});
  add_if_has({"lanzar", "lanza", "arrancar", "ejecutar"},
             {"launch", "run", "start", "exec", "debug"});
  add_if_has({"busqueda", "buscador", "buscar", "search"},
             {"search", "find", "ripgrep", "rg"});
  // Ambiguous UI words: only generic English, never concrete file stems (editor_tab, …).
  add_if_has({"pestaña", "pestana", "pestanas", "tabs"}, {"tab", "tabs"});
  add_if_has({"explorador", "explorer", "arbol"}, {"tree", "explorer"});
  add_if_has({"terminal", "consola", "pty"}, {"terminal", "console", "pty", "shell"});
  add_if_has({"compilar", "compilacion", "compilando", "compile", "compilation", "build",
               "builder", "cmake", "makefile"},
              {"compile", "build", "cmake", "makefile", "ninja", "launch"});
  add_if_has({"ajustes", "settings", "preferencias", "configuracion", "configuration", "config"},
             {"settings", "preferences", "config"});
  // "menú" en UI de ajustes ≠ context_menu: mapear a settings/config.
  add_if_has({"menu", "menus"}, {"settings", "config", "preferences"});
  add_if_has({"atajos", "shortcuts", "keymap"}, {"shortcut", "shortcuts", "keybind"});
  add_if_has({"modal", "dialog", "dialogs"}, {"modal", "dialog", "overlay"});
  // Performance / threads UI (ES→EN stem tokens).
  add_if_has({"rendimiento", "rendimientos", "performance", "cpu", "hilo", "hilos", "thread",
               "threads"},
              {"performance", "thread", "threads", "cpu"});
  // I/O / wire path: recepción → transport; paquetes/monitor → packet monitor UI.
  add_if_has({"recepcion", "recepciones", "recibir", "recibo", "receive", "received", "recv",
               "incoming"},
              {"transport", "notification", "message", "incoming", "receive", "reader"});
  add_if_has({"paquete", "paquetes", "packet", "packets", "frame", "frames", "payload", "monitor"},
             {"packet", "monitor", "pkt", "frame", "payload", "message"});
  // Syntax coloring: coloreado/resaltado → highlight layer (not symbol provider APIs).
  add_if_has({"coloreado", "colorear", "coloracion", "resaltado", "resaltar", "highlight",
               "highlighting", "highlighter", "syntax"},
              {"highlight", "highlighter", "syntax", "highlights"});
  // Async UI redraw / invalidation (mechanism), not LSP protocol providers.
  add_if_has({"wake", "wakes", "despierta", "despertar", "despertando", "redibuj", "redibujar",
               "redibujado", "redraw", "async", "asincron", "asincrono", "asincronos", "invalidate",
               "invalidacion", "invalidar", "repaint", "refresh_ui", "politica", "policy"},
              {"wake", "ui_wake", "policy", "invalidation", "repaint"});

  if (out.size() > max_n) {
    out.resize(max_n);
  }
  return out;
}

namespace {

std::string fold_nl_ascii(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size();) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c < 0x80) {
      out.push_back(static_cast<char>(std::tolower(c)));
      ++i;
      continue;
    }
    if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
      const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
      const unsigned cp = (static_cast<unsigned>(c & 0x1F) << 6) | (c1 & 0x3F);
      char mapped = 0;
      switch (cp) {
        case 0xE1: case 0xC1: case 0xE0: case 0xC0: case 0xE2: case 0xC2:
        case 0xE3: case 0xC3: case 0xE4: case 0xC4:
          mapped = 'a';
          break;
        case 0xE9: case 0xC9: case 0xE8: case 0xC8: case 0xEA: case 0xCA:
        case 0xEB: case 0xCB:
          mapped = 'e';
          break;
        case 0xED: case 0xCD: case 0xEC: case 0xCC: case 0xEE: case 0xCE:
        case 0xEF: case 0xCF:
          mapped = 'i';
          break;
        case 0xF3: case 0xD3: case 0xF2: case 0xD2: case 0xF4: case 0xD4:
        case 0xF5: case 0xD5: case 0xF6: case 0xD6:
          mapped = 'o';
          break;
        case 0xFA: case 0xDA: case 0xF9: case 0xD9: case 0xFB: case 0xDB:
        case 0xFC: case 0xDC:
          mapped = 'u';
          break;
        case 0xF1: case 0xD1:
          mapped = 'n';
          break;
        default:
          break;
      }
      if (mapped != 0) {
        out.push_back(mapped);
      }
      i += 2;
      continue;
    }
    ++i;
  }
  return out;
}

bool is_facet_stopword(const std::string& w) {
  static const std::unordered_set<std::string> kStop = {
      "a",        "al",       "an",        "and",       "about",     "ahora",      "aplicacion",
      "archivo",  "archivos", "as",        "at",        "be",        "by",         "code",
      "codigo",   "como",     "con",       "contexto",  "context",   "cual",       "cuando",
      "dame",     "de",       "del",       "distintas", "distintos", "donde",      "el",
      "en",       "es",       "esta",      "este",      "for",       "from",       "give",
      "hace",     "hay",      "how",       "in",        "is",        "la",         "las",
      "llega",    "llegado",  "llegar",    "llegue",    "lo",        "los",        "me",
      "of",       "on",       "or",        "para",      "por",       "que",        "se",
      "si",       "sin",      "sobre",     "sus",       "the",       "this",       "to",
      "un",       "una",      "what",      "where",     "which",     "with",       "y",
  };
  return kStop.count(w) > 0;
}

// Cheap English/Spanish plural → singular for facet matching (wakes→wake, hilos→hilo).
std::string facet_simple_singular(const std::string& w) {
  if (w.size() < 4 || w.back() != 's') {
    return {};
  }
  // Avoid class/process/diagnostics-ish endings that are not plain plurals.
  if (w.size() >= 2) {
    const std::string tail2 = w.substr(w.size() - 2);
    if (tail2 == "ss" || tail2 == "us" || tail2 == "is") {
      return {};
    }
  }
  return w.substr(0, w.size() - 1);
}

}  // namespace

std::vector<std::string> extract_query_facets(const std::string& text, std::size_t max_n) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  const std::string folded = fold_nl_ascii(text);
  std::string cur;
  auto push_facet = [&](std::string token) {
    if (token.size() < 3 || is_facet_stopword(token) || !seen.insert(token).second) {
      return;
    }
    out.push_back(token);
    if (out.size() >= max_n) {
      return;
    }
    const std::string sing = facet_simple_singular(token);
    if (!sing.empty() && sing.size() >= 3 && !is_facet_stopword(sing) && seen.insert(sing).second) {
      out.push_back(sing);
    }
  };
  auto flush = [&] {
    if (!cur.empty()) {
      push_facet(std::move(cur));
      cur.clear();
    }
  };
  for (char ch : folded) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (std::isalnum(c) || ch == '_') {
      cur.push_back(static_cast<char>(std::tolower(c)));
    } else {
      flush();
    }
    if (out.size() >= max_n) {
      return out;
    }
  }
  flush();
  return out;
}

int facet_coverage_score(const std::string& file, const std::string& name,
                         const std::string& signature, const std::vector<std::string>& facets) {
  if (facets.empty()) {
    return 0;
  }
  const std::string hay =
      ascii_lower(file) + " " + ascii_lower(name) + " " + ascii_lower(signature);
  int covered = 0;
  int strength = 0;
  for (const auto& facet : facets) {
    if (facet.size() < 3) {
      continue;
    }
    bool hit = false;
    int local = 0;
    // Prefer NL expansions so "menu"→settings can beat a raw "context_menu" substring.
    const auto expanded = expand_nl_retrieval_tokens({facet}, 12);
    for (const auto& ex : expanded) {
      if (ex == facet) {
        continue;
      }
      if (hay.find(ex) != std::string::npos) {
        hit = true;
        local = std::max(local, static_cast<int>(ex.size()));
        break;
      }
    }
    if (!hit && hay.find(facet) != std::string::npos) {
      // "menu" inside context_menu is a false friend for ajustes/configuración.
      const bool menu_false_friend =
          (facet == "menu" || facet == "menus") && hay.find("context_menu") != std::string::npos;
      if (!menu_false_friend) {
        hit = true;
        local = static_cast<int>(facet.size());
      }
    }
    if (hit) {
      ++covered;
      strength += local;
    }
  }
  return covered * 1000 + strength;
}
}  // namespace tuide
