#include "ai/l2_admin.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ai/action_json.hpp"
#include "ai/search_replace.hpp"

namespace tuide {
namespace {

namespace fs = std::filesystem;

std::string trim_copy(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  return s;
}

std::string shell_single_quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  out.push_back('\'');
  return out;
}

bool admin_path_is_noise(const std::string& path) {
  // Artefactos de sesión/batería: no son fuente del producto.
  if (path.find("/.tuide/") != std::string::npos || path.rfind(".tuide/", 0) == 0 ||
      path.rfind("./.tuide/", 0) == 0) {
    return true;
  }
  if (path.find("/build/") != std::string::npos || path.rfind("build/", 0) == 0 ||
      path.rfind("./build/", 0) == 0) {
    return true;
  }
  return false;
}

std::string path_generic(std::string s) {
  for (char& c : s) {
    if (c == '\\') {
      c = '/';
    }
  }
  while (s.size() > 1 && s.back() == '/') {
    s.pop_back();
  }
  return s;
}

std::string ascii_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

void utf8_sanitize_inplace(std::string* s) {
  if (s == nullptr || s->empty()) {
    return;
  }
  std::string out;
  out.reserve(s->size());
  for (std::size_t i = 0; i < s->size();) {
    const auto c = static_cast<unsigned char>((*s)[i]);
    std::size_t need = 1;
    if ((c & 0x80) == 0) {
      need = 1;
    } else if ((c & 0xe0) == 0xc0 && c >= 0xc2) {
      need = 2;
    } else if ((c & 0xf0) == 0xe0) {
      need = 3;
    } else if ((c & 0xf8) == 0xf0 && c <= 0xf4) {
      need = 4;
    } else {
      ++i;
      continue;
    }
    if (i + need > s->size()) {
      break;
    }
    bool ok = true;
    for (std::size_t j = 1; j < need; ++j) {
      if ((static_cast<unsigned char>((*s)[i + j]) & 0xc0) != 0x80) {
        ok = false;
        break;
      }
    }
    if (!ok) {
      ++i;
      continue;
    }
    out.append(*s, i, need);
    i += need;
  }
  *s = std::move(out);
}

void utf8_resize(std::string* s, std::size_t max_bytes) {
  if (s == nullptr) {
    return;
  }
  utf8_sanitize_inplace(s);
  if (s->size() <= max_bytes) {
    return;
  }
  s->resize(max_bytes);
  while (!s->empty() && (static_cast<unsigned char>(s->back()) & 0xC0) == 0x80) {
    s->pop_back();
  }
}

std::string json_str(const nlohmann::json& j, const char* key) {
  if (!j.contains(key)) {
    return {};
  }
  if (j[key].is_string()) {
    return j[key].get<std::string>();
  }
  if (j[key].is_number() || j[key].is_boolean()) {
    return j[key].dump();
  }
  return {};
}

bool write_file(const fs::path& path, const std::string& body, std::string* err) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path);
  if (!out) {
    if (err) {
      *err = "no se pudo escribir " + path.string();
    }
    return false;
  }
  out << body;
  return true;
}

std::string read_file(const fs::path& path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void append_line(const AdminLoopOpts& opts, const std::string& line) {
  if (opts.on_line) {
    opts.on_line(line);
  }
}

std::string ui_clip(std::string s, std::size_t n) {
  utf8_resize(&s, n);
  return s;
}

void ui_note(const AdminLoopOpts& opts, const std::string& line) {
  append_line(opts, line);
}

std::string spawn_tipo_ui(AdminSpawnTipo t) {
  switch (t) {
    case AdminSpawnTipo::Explore:
      return "explorar";
    case AdminSpawnTipo::Search:
      return "buscar";
    case AdminSpawnTipo::Read:
      return "leer";
    case AdminSpawnTipo::Shell:
      return "shell";
    case AdminSpawnTipo::Build:
      return "build";
    case AdminSpawnTipo::Git:
      return "git";
    case AdminSpawnTipo::Diagnostics:
      return "diagnósticos";
    case AdminSpawnTipo::Test:
      return "tests";
    case AdminSpawnTipo::Edit:
      return "editar";
    case AdminSpawnTipo::Web:
      return "buscar en la web";
    case AdminSpawnTipo::WebFetch:
      return "abrir URL";
    default:
      return "tarea";
  }
}

std::string verdicto_explore_ui(const std::string& v) {
  if (v == "encontrado") {
    return "encontrado";
  }
  if (v == "parcial") {
    return "parcial";
  }
  if (v == "no_encontrado") {
    return "no encontrado";
  }
  return v.empty() ? "sin veredicto" : v;
}

std::string verdicto_verify_ui(const std::string& v) {
  if (v == "sostiene") {
    return "sostiene la explicación";
  }
  if (v == "refuta") {
    return "refuta la explicación";
  }
  if (v == "dudoso") {
    return "queda en duda";
  }
  return v.empty() ? "sin veredicto" : v;
}

std::string humanize_admin_reject(const std::string& err) {
  if (err.find("tope de explore") != std::string::npos) {
    return "Ya hay bastantes exploraciones; sigue con buscar/leer o cierra.";
  }
  if (err.find("do ilegal") != std::string::npos && err.find("spawn") != std::string::npos) {
    return "En este turno no cabe otra investigación (cierra, pregunta o cambia de enfoque).";
  }
  if (err.find("do ilegal") != std::string::npos) {
    return "Esa acción no aplica ahora.";
  }
  return err;
}

bool cancelled(const AdminLoopOpts& opts) {
  return opts.cancel != nullptr && opts.cancel->load();
}

std::vector<std::string> json_str_array(const nlohmann::json& j, const char* key) {
  std::vector<std::string> out;
  if (!j.contains(key) || !j[key].is_array()) {
    return out;
  }
  for (const auto& s : j[key]) {
    if (s.is_string()) {
      out.push_back(s.get<std::string>());
    }
  }
  return out;
}

void admin_normalize_spawn(AdminSpawn* sp) {
  if (sp == nullptr) {
    return;
  }
  if (sp->tipo == AdminSpawnTipo::Shell) {
    const std::string raw = trim_copy(sp->arg);
    const std::string low = ascii_lower(raw);
    if (low.find("cmake") != std::string::npos || low.find("--build") != std::string::npos) {
      sp->tipo = AdminSpawnTipo::Build;
      sp->arg = "compile";
      return;
    }
    sp->arg = raw;
    return;
  }
  if (sp->tipo == AdminSpawnTipo::Edit) {
    sp->arg = trim_copy(sp->arg);
    sp->search = trim_copy(sp->search);
    sp->replace = trim_copy(sp->replace);
    return;
  }
  std::string arg = ascii_lower(trim_copy(sp->arg));
  if (sp->tipo == AdminSpawnTipo::Git) {
    if (arg.rfind("git ", 0) == 0) {
      arg = trim_copy(arg.substr(4));
    }
    if (arg == "gitstatus" || arg == "st") {
      arg = "status";
    }
    if (arg.rfind("status", 0) == 0) {
      arg = "status";
    } else if (arg.rfind("log", 0) == 0) {
      arg = "log";
    } else if (arg.rfind("diff", 0) == 0) {
      arg = "diff";
    } else if (arg.rfind("show", 0) == 0) {
      arg = "show";
    } else if (arg == "branch" || arg == "branches" || arg.find("branch") == 0) {
      arg = "branches";
    }
    sp->arg = arg;
  } else if (sp->tipo == AdminSpawnTipo::Build || sp->tipo == AdminSpawnTipo::Test) {
    if (arg.find("cmake") != std::string::npos || arg.find("--build") != std::string::npos) {
      arg = sp->tipo == AdminSpawnTipo::Test ? "test" : "compile";
    }
    if (sp->tipo == AdminSpawnTipo::Build && (arg == "build" || arg.empty())) {
      arg = "compile";
    }
    if (sp->tipo == AdminSpawnTipo::Test && arg.empty()) {
      arg = "test";
    }
    sp->arg = arg;
  } else if (sp->tipo == AdminSpawnTipo::Search || sp->tipo == AdminSpawnTipo::Read ||
             sp->tipo == AdminSpawnTipo::Diagnostics) {
    sp->arg = trim_copy(sp->arg);
  }
}

}  // namespace

bool admin_path_inside_workspace(const std::string& workspace_root, const std::string& path) {
  if (workspace_root.empty()) {
    return true;  // sin root no hay perímetro
  }
  if (path.empty()) {
    return false;
  }
  std::string abs;
  std::string rel;
  return admin_resolve_in_workspace(workspace_root, path, &abs, &rel, nullptr);
}

bool admin_resolve_in_workspace(const std::string& workspace_root, const std::string& path,
                                std::string* abs_out, std::string* rel_out, std::string* err) {
  if (path.empty()) {
    if (err) {
      *err = "path vacío";
    }
    return false;
  }
  std::error_code ec;
  fs::path root_p;
  if (!workspace_root.empty()) {
    root_p = fs::weakly_canonical(workspace_root, ec);
    if (ec || root_p.empty()) {
      root_p = fs::path(workspace_root).lexically_normal();
    }
  }
  fs::path p(path);
  if (!p.is_absolute()) {
    if (root_p.empty()) {
      p = p.lexically_normal();
    } else {
      p = root_p / p;
    }
  }
  fs::path abs = fs::weakly_canonical(p, ec);
  if (ec || abs.empty()) {
    abs = p.lexically_normal();
  }
  if (!root_p.empty()) {
    const auto rel = fs::relative(abs, root_p, ec);
    const bool rel_bad = ec || rel.empty() || (!rel.empty() && *rel.begin() == "..");
    if (rel_bad) {
      const std::string a = path_generic(abs.string());
      const std::string r = path_generic(root_p.string());
      const bool under =
          a == r || (a.size() > r.size() && a.compare(0, r.size(), r) == 0 && a[r.size()] == '/');
      if (!under) {
        if (err) {
          *err = "fuera del workspace root: " + path;
        }
        return false;
      }
    }
    if (rel_out) {
      if (!rel_bad) {
        *rel_out = path_generic(rel.generic_string());
      } else {
        *rel_out = path_generic(fs::relative(abs, root_p, ec).generic_string());
      }
      if (rel_out->empty() || *rel_out == ".") {
        *rel_out = path_generic(fs::path(path).lexically_normal().generic_string());
      }
    }
  } else if (rel_out) {
    *rel_out = path_generic(abs.generic_string());
  }
  if (abs_out) {
    *abs_out = abs.string();
  }
  return true;
}

bool admin_shell_stays_in_workspace(const std::string& cmd, const std::string& workspace_root,
                                    std::string* err) {
  if (workspace_root.empty()) {
    return true;
  }
  const std::string c = trim_copy(cmd);
  if (c.find("..") != std::string::npos) {
    if (err) {
      *err = "shell: '..' fuera del workspace";
    }
    return false;
  }
  const std::string low = ascii_lower(c);
  if (low.find("cd /") != std::string::npos || low.find("cd ~") != std::string::npos ||
      low.find("cd $home") != std::string::npos || low.find("cd${home") != std::string::npos) {
    if (err) {
      *err = "shell: cd fuera del workspace";
    }
    return false;
  }
  std::string tok;
  auto flush = [&]() {
    if (tok.empty()) {
      return true;
    }
    std::string t = tok;
    tok.clear();
    if (t.size() >= 2 && ((t.front() == '"' && t.back() == '"') ||
                          (t.front() == '\'' && t.back() == '\''))) {
      t = t.substr(1, t.size() - 2);
    }
    if (t.empty() || t[0] != '/') {
      return true;
    }
    if (!admin_path_inside_workspace(workspace_root, t)) {
      if (err) {
        *err = "shell: path absoluto fuera del workspace: " + t;
      }
      return false;
    }
    return true;
  };
  for (char ch : c) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      if (!flush()) {
        return false;
      }
    } else {
      tok.push_back(ch);
    }
  }
  return flush();
}

const char* admin_do_name(AdminDo d) {
  switch (d) {
    case AdminDo::Spawn:
      return "spawn";
    case AdminDo::Cerrar:
      return "cerrar";
    case AdminDo::AskUser:
      return "ask_user";
    case AdminDo::Editar:
      return "editar";
    case AdminDo::ConfirmarEditar:
      return "confirmar_editar";
    case AdminDo::SeguirExplorando:
      return "seguir_explorando";
    case AdminDo::Invalid:
    default:
      return "invalid";
  }
}

const char* admin_spawn_tipo_name(AdminSpawnTipo t) {
  switch (t) {
    case AdminSpawnTipo::Explore:
      return "explore";
    case AdminSpawnTipo::Build:
      return "build";
    case AdminSpawnTipo::Git:
      return "git";
    case AdminSpawnTipo::Shell:
      return "shell";
    case AdminSpawnTipo::Search:
      return "search";
    case AdminSpawnTipo::Read:
      return "read";
    case AdminSpawnTipo::Diagnostics:
      return "diagnostics";
    case AdminSpawnTipo::Test:
      return "test";
    case AdminSpawnTipo::Edit:
      return "edit";
    case AdminSpawnTipo::Web:
      return "web";
    case AdminSpawnTipo::WebFetch:
      return "web_fetch";
    case AdminSpawnTipo::Invalid:
    default:
      return "invalid";
  }
}

AdminSpawnTipo admin_spawn_tipo_parse(const std::string& s) {
  const std::string t = ascii_lower(trim_copy(s));
  if (t == "explore") {
    return AdminSpawnTipo::Explore;
  }
  if (t == "build") {
    return AdminSpawnTipo::Build;
  }
  if (t == "git") {
    return AdminSpawnTipo::Git;
  }
  if (t == "shell") {
    return AdminSpawnTipo::Shell;
  }
  if (t == "search") {
    return AdminSpawnTipo::Search;
  }
  if (t == "read") {
    return AdminSpawnTipo::Read;
  }
  if (t == "diagnostics" || t == "diag") {
    return AdminSpawnTipo::Diagnostics;
  }
  if (t == "test") {
    return AdminSpawnTipo::Test;
  }
  if (t == "edit") {
    return AdminSpawnTipo::Edit;
  }
  if (t == "web" || t == "web_search" || t == "internet") {
    return AdminSpawnTipo::Web;
  }
  if (t == "web_fetch" || t == "fetch" || t == "web-fetch") {
    return AdminSpawnTipo::WebFetch;
  }
  return AdminSpawnTipo::Invalid;
}

std::string admin_dir(const std::string& workspace_root) {
  return (fs::path(workspace_root) / ".tuide" / "ai" / "l2_admin").string();
}

std::string admin_state_path(const std::string& workspace_root) {
  return (fs::path(admin_dir(workspace_root)) / "state.json").string();
}

std::string admin_notebook_path(const std::string& workspace_root) {
  return (fs::path(admin_dir(workspace_root)) / "notebook.md").string();
}

bool admin_is_continuable(const std::string& workspace_root) {
  if (workspace_root.empty()) {
    return false;
  }
  AdminState st;
  std::string err;
  if (!admin_load_state(workspace_root, &st, &err)) {
    return false;
  }
  return st.clarify || !st.done || !st.notebook.empty() || !st.episodes.empty();
}

namespace {

std::vector<std::string> admin_content_tokens(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&]() {
    if (cur.size() >= 4) {
      out.push_back(cur);
    }
    cur.clear();
  };
  for (unsigned char uc : text) {
    const char c = static_cast<char>(std::tolower(uc));
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        static_cast<unsigned char>(c) >= 0x80) {
      cur.push_back(c);
    } else {
      flush();
    }
  }
  flush();
  return out;
}

bool admin_has_followup_cue(const std::string& low) {
  // Cues fuertes (pueden aparecer en cualquier sitio).
  static const char* kStrong[] = {
      "sobre eso",   "de eso",       "lo mismo",     "lo de",      "el anterior",
      "la anterior", "como dijiste", "como antes",   "más detalle", "mas detalle",
      "explica más", "explica mas",
  };
  for (const char* cue : kStrong) {
    if (low.find(cue) != std::string::npos) {
      return true;
    }
  }
  // Anáforas cortas: solo al inicio del mensaje (si no, "y el" dispara en prosa normal).
  static const char* kLead[] = {
      "eso ",    "esa ",    "ese ",   "esos ",  "esas ",  "ahí ",   "ahi ",
      "aquí ",   "aqui ",   "y qué",  "y que",  "y los ", "y las ", "y el ",
      "también", "tambien", "además", "ademas", "sigue ", "continúa", "continua ",
  };
  for (const char* cue : kLead) {
    if (low.rfind(cue, 0) == 0) {
      return true;
    }
  }
  // "eso"/"ahí" como mensaje casi entero.
  if (low == "eso" || low == "esa" || low == "ese" || low == "ahí" || low == "ahi" ||
      low == "aquí" || low == "aqui") {
    return true;
  }
  return false;
}

double admin_token_overlap(const std::vector<std::string>& a,
                           const std::vector<std::string>& b) {
  if (a.empty() || b.empty()) {
    return 0.0;
  }
  std::size_t hits = 0;
  for (const auto& t : a) {
    if (std::find(b.begin(), b.end(), t) != b.end()) {
      ++hits;
    }
  }
  const std::size_t denom = std::max(a.size(), b.size());
  return static_cast<double>(hits) / static_cast<double>(denom);
}

}  // namespace

bool admin_should_keep_session(const AdminState& st, const std::string& message) {
  const std::string msg = trim_copy(message);
  if (msg.empty()) {
    return true;
  }
  // Respuesta a ask_user: siempre mismo hilo.
  if (st.clarify) {
    return true;
  }
  const std::string low = ascii_lower(msg);
  if (admin_has_followup_cue(low)) {
    return true;
  }
  // Mensajes muy cortos sin anáfora rara vez son un tema nuevo completo.
  if (msg.size() < 36) {
    return true;
  }

  std::string prior = st.consulta;
  for (const auto& ep : st.episodes) {
    if (!ep.consulta.empty()) {
      prior += ' ';
      prior += ep.consulta;
    }
  }
  const auto prior_tok = admin_content_tokens(prior);
  const auto msg_tok = admin_content_tokens(msg);
  const double overlap = admin_token_overlap(msg_tok, prior_tok);
  // Misma familia léxica → follow-up; si no, tema nuevo (no mezclar notebooks).
  if (overlap >= 0.22) {
    return true;
  }
  return false;
}

bool admin_clear_session(const std::string& workspace_root, std::string* err) {
  if (workspace_root.empty()) {
    if (err) {
      *err = "sin workspace";
    }
    return false;
  }
  std::error_code ec;
  const fs::path dir = admin_dir(workspace_root);
  if (fs::exists(dir)) {
    fs::remove_all(dir, ec);
    if (ec) {
      if (err) {
        *err = ec.message();
      }
      return false;
    }
  }
  return true;
}

nlohmann::json admin_state_to_json(const AdminState& st) {
  nlohmann::json jobs = nlohmann::json::array();
  for (const auto& j : st.jobs) {
    jobs.push_back({{"id", j.id},
                    {"tipo", j.tipo},
                    {"ok", j.ok},
                    {"summary", j.summary},
                    {"veredicto", j.veredicto},
                    {"simbolos", j.simbolos},
                    {"evidencia", j.evidencia},
                    {"log_tail", j.log_tail},
                    {"truncated", j.truncated},
                    {"raw_bytes", j.raw_bytes}});
  }
  nlohmann::json nb = nlohmann::json::array();
  for (const auto& e : st.notebook) {
    nb.push_back({{"job_id", e.job_id},
                  {"tipo", e.tipo},
                  {"summary", e.summary},
                  {"paths", e.paths},
                  {"simbolos", e.simbolos},
                  {"facts", e.facts}});
  }
  nlohmann::json clarifies = nlohmann::json::array();
  for (const auto& c : st.clarifies) {
    clarifies.push_back({{"question", c.question}, {"answer", c.answer}});
  }
  nlohmann::json episodes = nlohmann::json::array();
  for (const auto& e : st.episodes) {
    episodes.push_back({{"consulta", e.consulta}, {"reply", e.reply}});
  }
  return nlohmann::json{
      {"consulta", st.consulta},
      {"jobs", std::move(jobs)},
      {"notebook", std::move(nb)},
      {"ui",
       {{"active_path", st.ui.active_path},
        {"cursor_line", st.ui.cursor_line},
        {"selection", st.ui.selection},
        {"git_branch", st.ui.git_branch}}},
      {"proposes", st.proposes},
      {"spawns", st.spawns},
      {"done", st.done},
      {"clarify", st.clarify},
      {"pending_question", st.pending_question},
      {"clarifies", std::move(clarifies)},
      {"episodes", std::move(episodes)},
      {"awaiting_edit_confirm", st.awaiting_edit_confirm},
      {"edit_confirmed", st.edit_confirmed},
      {"verify_reject_pending", st.verify_reject_pending},
      {"verify_passes", st.verify_passes},
      {"last_verify_verdict", st.last_verify_verdict},
      {"last_verify_why", st.last_verify_why},
      {"edit_cubre", st.edit_cubre},
      {"edit_falta", st.edit_falta},
      {"reply", st.reply},
      {"last_error", st.last_error}};
}

bool admin_state_from_json(const nlohmann::json& j, AdminState* st, std::string* err) {
  if (st == nullptr || !j.is_object()) {
    if (err) {
      *err = "state JSON inválido";
    }
    return false;
  }
  st->consulta = j.value("consulta", "");
  st->proposes = j.value("proposes", 0);
  st->spawns = j.value("spawns", 0);
  st->done = j.value("done", false);
  st->clarify = j.value("clarify", false);
  st->pending_question = j.value("pending_question", "");
  st->awaiting_edit_confirm = j.value("awaiting_edit_confirm", false);
  st->edit_confirmed = j.value("edit_confirmed", false);
  st->verify_reject_pending = j.value("verify_reject_pending", false);
  st->verify_passes = j.value("verify_passes", 0);
  st->last_verify_verdict = j.value("last_verify_verdict", "");
  st->last_verify_why = j.value("last_verify_why", "");
  st->edit_cubre = j.value("edit_cubre", "");
  st->edit_falta = j.value("edit_falta", "");
  st->reply = j.value("reply", "");
  st->last_error = j.value("last_error", "");
  st->jobs.clear();
  st->notebook.clear();
  st->clarifies.clear();
  st->episodes.clear();
  if (j.contains("clarifies") && j["clarifies"].is_array()) {
    for (const auto& row : j["clarifies"]) {
      if (!row.is_object()) {
        continue;
      }
      AdminClarifyTurn t;
      t.question = row.value("question", "");
      t.answer = row.value("answer", "");
      if (!t.question.empty() || !t.answer.empty()) {
        st->clarifies.push_back(std::move(t));
      }
    }
  }
  if (j.contains("episodes") && j["episodes"].is_array()) {
    for (const auto& row : j["episodes"]) {
      if (!row.is_object()) {
        continue;
      }
      AdminEpisode ep;
      ep.consulta = row.value("consulta", "");
      ep.reply = row.value("reply", "");
      if (!ep.consulta.empty() || !ep.reply.empty()) {
        st->episodes.push_back(std::move(ep));
      }
    }
  }
  // Compat: ask_user antiguo marcaba done+clarify → reabrir como pendiente.
  if (st->clarify && st->done) {
    st->done = false;
    if (st->pending_question.empty() && !st->reply.empty()) {
      st->pending_question = st->reply;
    }
  }
  if (j.contains("ui") && j["ui"].is_object()) {
    st->ui.active_path = j["ui"].value("active_path", "");
    st->ui.cursor_line = j["ui"].value("cursor_line", -1);
    st->ui.selection = j["ui"].value("selection", "");
    st->ui.git_branch = j["ui"].value("git_branch", "");
  }
  if (j.contains("jobs") && j["jobs"].is_array()) {
    for (const auto& row : j["jobs"]) {
      if (!row.is_object()) {
        continue;
      }
      AdminJob job;
      job.id = row.value("id", 0);
      job.tipo = row.value("tipo", "");
      job.ok = row.value("ok", false);
      job.summary = row.value("summary", "");
      job.veredicto = row.value("veredicto", "");
      job.log_tail = row.value("log_tail", "");
      job.truncated = row.value("truncated", false);
      job.raw_bytes = row.value("raw_bytes", 0);
      job.simbolos = json_str_array(row, "simbolos");
      job.evidencia = json_str_array(row, "evidencia");
      st->jobs.push_back(std::move(job));
    }
  }
  if (j.contains("notebook") && j["notebook"].is_array()) {
    for (const auto& row : j["notebook"]) {
      if (!row.is_object()) {
        continue;
      }
      AdminEvidenceItem e;
      e.job_id = row.value("job_id", 0);
      e.tipo = row.value("tipo", "");
      e.summary = row.value("summary", "");
      e.paths = json_str_array(row, "paths");
      e.simbolos = json_str_array(row, "simbolos");
      e.facts = json_str_array(row, "facts");
      st->notebook.push_back(std::move(e));
    }
  }
  return true;
}

bool admin_save_state(const std::string& workspace_root, const AdminState& st, std::string* err) {
  try {
    if (!write_file(admin_state_path(workspace_root),
                    admin_state_to_json(st).dump(2, ' ', false,
                                                 nlohmann::json::error_handler_t::replace),
                    err)) {
      return false;
    }
  } catch (const std::exception& e) {
    if (err) {
      *err = std::string("state dump: ") + e.what();
    }
    return false;
  }
  return write_file(admin_notebook_path(workspace_root), admin_notebook_markdown(st), err);
}

bool admin_load_state(const std::string& workspace_root, AdminState* st, std::string* err) {
  if (st == nullptr) {
    if (err) {
      *err = "state null";
    }
    return false;
  }
  const std::string raw = read_file(admin_state_path(workspace_root));
  if (raw.empty()) {
    if (err) {
      *err = "sin state.json";
    }
    return false;
  }
  try {
    return admin_state_from_json(nlohmann::json::parse(raw), st, err);
  } catch (const std::exception& e) {
    if (err) {
      *err = std::string("state JSON: ") + e.what();
    }
    return false;
  }
}

void admin_notebook_append(AdminState* st, const AdminJob& job, const AdminJobResult& jr) {
  if (st == nullptr) {
    return;
  }
  AdminEvidenceItem e;
  e.job_id = job.id;
  e.tipo = job.tipo;
  e.summary = job.summary;
  utf8_resize(&e.summary, static_cast<std::size_t>(kAdminNotebookSummaryChars));
  e.simbolos = jr.simbolos.empty() ? job.simbolos : jr.simbolos;
  e.paths = jr.paths;
  e.facts = jr.facts;
  if (e.facts.empty() && !job.log_tail.empty()) {
    std::istringstream iss(job.log_tail);
    std::string line;
    int n = 0;
    while (std::getline(iss, line) && n < 6) {
      line = trim_copy(line);
      if (line.empty() || line.rfind("---", 0) == 0 || line.rfind("[truncated", 0) == 0) {
        continue;
      }
      utf8_resize(&line, static_cast<std::size_t>(kAdminNotebookFactChars));
      e.facts.push_back(line);
      ++n;
    }
  }
  // Infer paths from evidencia strings "path:…"
  for (const auto& ev : job.evidencia) {
    const auto col = ev.find(':');
    if (col != std::string::npos && col > 0) {
      const std::string p = ev.substr(0, col);
      if (p.find('/') != std::string::npos || p.find('.') != std::string::npos) {
        if (std::find(e.paths.begin(), e.paths.end(), p) == e.paths.end()) {
          e.paths.push_back(p);
        }
      }
    }
  }
  st->notebook.push_back(std::move(e));
  if (static_cast<int>(st->notebook.size()) > kAdminNotebookMaxItems) {
    st->notebook.erase(st->notebook.begin(),
                       st->notebook.begin() +
                           (static_cast<int>(st->notebook.size()) - kAdminNotebookMaxItems));
  }
}

std::string admin_notebook_markdown(const AdminState& st) {
  std::ostringstream out;
  out << "# Notebook admin (evidencias acumuladas)\n\n";
  out << "Consulta: " << st.consulta << "\n\n";
  if (!st.ui.active_path.empty() || !st.ui.git_branch.empty()) {
    out << "## UI\n";
    if (!st.ui.active_path.empty()) {
      out << "- active: `" << st.ui.active_path << "`";
      if (st.ui.cursor_line >= 0) {
        out << " L" << (st.ui.cursor_line + 1);
      }
      out << "\n";
    }
    if (!st.ui.git_branch.empty()) {
      out << "- branch: `" << st.ui.git_branch << "`\n";
    }
    if (!st.ui.selection.empty()) {
      out << "- selection: " << st.ui.selection << "\n";
    }
    out << "\n";
  }
  if (st.notebook.empty()) {
    out << "(vacío — aún no hay evidencias)\n";
    return out.str();
  }
  out << "## Evidencias\n";
  for (const auto& e : st.notebook) {
    std::string verd;
    for (const auto& j : st.jobs) {
      if (j.id == e.job_id) {
        verd = j.veredicto;
        break;
      }
    }
    out << "- #" << e.job_id << " `" << e.tipo << "`";
    if (!verd.empty()) {
      out << " veredicto=" << verd;
    }
    out << " " << e.summary << "\n";
    for (const auto& p : e.paths) {
      out << "  - path: `" << p << "`\n";
    }
    for (const auto& s : e.simbolos) {
      out << "  - sym: `" << s << "`\n";
    }
    for (const auto& f : e.facts) {
      out << "  - fact: " << f << "\n";
    }
  }
  return out.str();
}

bool admin_notebook_has_path(const AdminState& st, const std::string& path) {
  if (path.empty()) {
    return false;
  }
  const std::string want = path;
  for (const auto& e : st.notebook) {
    for (const auto& p : e.paths) {
      if (p == want) {
        return true;
      }
      if (p.size() >= want.size() &&
          p.compare(p.size() - want.size(), want.size(), want) == 0) {
        return true;
      }
      if (want.size() >= p.size() &&
          want.compare(want.size() - p.size(), p.size(), p) == 0) {
        return true;
      }
    }
  }
  if (!st.ui.active_path.empty() &&
      (st.ui.active_path == path || st.ui.active_path.find(path) != std::string::npos ||
       path.find(st.ui.active_path) != std::string::npos)) {
    return true;
  }
  return false;
}

std::vector<std::string> admin_notebook_paths(const AdminState& st) {
  std::vector<std::string> out;
  for (const auto& e : st.notebook) {
    for (const auto& p : e.paths) {
      if (std::find(out.begin(), out.end(), p) == out.end()) {
        out.push_back(p);
      }
    }
  }
  return out;
}

AdminOla admin_parse(const std::string& raw) {
  AdminOla out;
  std::string blob = extract_action_json(raw);
  if (blob.empty()) {
    const auto pos = raw.rfind("\"do\"");
    if (pos != std::string::npos) {
      auto bra = raw.rfind('{', pos);
      if (bra != std::string::npos) {
        int depth = 0;
        for (std::size_t i = bra; i < raw.size(); ++i) {
          if (raw[i] == '{') {
            ++depth;
          } else if (raw[i] == '}') {
            --depth;
            if (depth == 0) {
              blob = raw.substr(bra, i - bra + 1);
              break;
            }
          }
        }
      }
    }
  }
  if (blob.empty()) {
    out.error = "admin sin objeto JSON";
    return out;
  }
  out.raw_json = blob;
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(blob);
  } catch (const std::exception& e) {
    out.error = std::string("JSON admin inválido: ") + e.what();
    return out;
  }
  std::string action = trim_copy(j.value("action", ""));
  std::string d = ascii_lower(trim_copy(json_str(j, "do")));
  // Heal frecuente: el modelo pone el gesto en "action" y omite admin_v1/do.
  // p.ej. {"action":"ask_user","why":"…","reply":"…"} → do=ask_user.
  const auto action_as_do = ascii_lower(action);
  const bool action_is_do =
      action_as_do == "spawn" || action_as_do == "cerrar" || action_as_do == "ask_user" ||
      action_as_do == "editar" || action_as_do == "confirmar_editar" ||
      action_as_do == "seguir_explorando";
  if (action_is_do) {
    if (d.empty()) {
      d = action_as_do;
    }
    action = "admin_v1";
  }
  if (!action.empty() && action != "admin_v1") {
    out.error = "contrato admin_v1 inválido (action debe ser \"admin_v1\"; do=gesto)";
    return out;
  }
  if (d == "spawn") {
    out.do_kind = AdminDo::Spawn;
  } else if (d == "cerrar") {
    out.do_kind = AdminDo::Cerrar;
  } else if (d == "ask_user") {
    out.do_kind = AdminDo::AskUser;
  } else if (d == "editar") {
    out.do_kind = AdminDo::Editar;
  } else if (d == "confirmar_editar") {
    out.do_kind = AdminDo::ConfirmarEditar;
  } else if (d == "seguir_explorando") {
    out.do_kind = AdminDo::SeguirExplorando;
  } else {
    out.error = "admin do inválido (spawn|cerrar|editar|confirmar_editar|seguir_explorando|ask_user)";
    return out;
  }
  out.why = trim_copy(json_str(j, "why"));
  if (static_cast<int>(out.why.size()) < kAdminWhyMin) {
    out.error = "why demasiado corto";
    return out;
  }
  utf8_resize(&out.why, static_cast<std::size_t>(kAdminWhyMax));
  out.reply = trim_copy(json_str(j, "reply"));
  utf8_resize(&out.reply, static_cast<std::size_t>(kAdminReplyMax));
  out.cubre = trim_copy(json_str(j, "cubre"));
  utf8_resize(&out.cubre, static_cast<std::size_t>(kAdminWhyMax));
  out.falta = trim_copy(json_str(j, "falta"));
  utf8_resize(&out.falta, static_cast<std::size_t>(kAdminWhyMax));

  if (out.do_kind == AdminDo::Spawn || out.do_kind == AdminDo::SeguirExplorando) {
    if (!j.contains("spawn") || !j["spawn"].is_object()) {
      if (out.do_kind == AdminDo::SeguirExplorando) {
        out.error = "seguir_explorando exige spawn.brief";
        return out;
      }
      out.error = "spawn sin objeto";
      return out;
    }
    const auto& sp = j["spawn"];
    if (out.do_kind == AdminDo::SeguirExplorando) {
      out.spawn.tipo = AdminSpawnTipo::Explore;
      out.spawn.brief = trim_copy(json_str(sp, "brief"));
      utf8_resize(&out.spawn.brief, static_cast<std::size_t>(kAdminBriefMax));
      out.spawn.arg = trim_copy(json_str(sp, "arg"));
      if (out.spawn.brief.size() < static_cast<std::size_t>(kAdminWhyMin)) {
        out.error = "seguir_explorando exige spawn.brief = hueco a cazar";
        return out;
      }
      out.ok = true;
      return out;
    }
    out.spawn.tipo = admin_spawn_tipo_parse(json_str(sp, "tipo"));
    if (out.spawn.tipo == AdminSpawnTipo::Invalid) {
      out.error =
          "spawn.tipo inválido (explore|build|git|shell|search|read|diagnostics|test|edit|web|web_fetch)";
      return out;
    }
    out.spawn.brief = trim_copy(json_str(sp, "brief"));
    utf8_resize(&out.spawn.brief, static_cast<std::size_t>(kAdminBriefMax));
    out.spawn.arg = trim_copy(json_str(sp, "arg"));
    out.spawn.search = sp.value("search", "");
    out.spawn.replace = sp.value("replace", "");
    if (out.spawn.tipo == AdminSpawnTipo::Build || out.spawn.tipo == AdminSpawnTipo::Git ||
        out.spawn.tipo == AdminSpawnTipo::Shell || out.spawn.tipo == AdminSpawnTipo::Search ||
        out.spawn.tipo == AdminSpawnTipo::Read || out.spawn.tipo == AdminSpawnTipo::Test ||
        out.spawn.tipo == AdminSpawnTipo::Edit || out.spawn.tipo == AdminSpawnTipo::Web ||
        out.spawn.tipo == AdminSpawnTipo::WebFetch) {
      if (out.spawn.arg.empty() && out.spawn.tipo != AdminSpawnTipo::Diagnostics) {
        out.error = "spawn.arg obligatorio para este tipo";
        return out;
      }
    }
    if (out.spawn.tipo == AdminSpawnTipo::Edit) {
      if (out.spawn.search.empty() || out.spawn.replace.empty()) {
        out.error = "edit exige search y replace";
        return out;
      }
    }
    admin_normalize_spawn(&out.spawn);
  } else if (out.do_kind == AdminDo::ConfirmarEditar) {
    if (out.cubre.size() < static_cast<std::size_t>(kAdminWhyMin)) {
      out.error = "confirmar_editar exige cubre (qué del pedido cubre el notebook)";
      return out;
    }
    if (out.falta.empty()) {
      out.error = "confirmar_editar exige falta (qué falta, o la palabra nada)";
      return out;
    }
  } else if (out.do_kind == AdminDo::Editar) {
    // sin reply; el runtime pedirá confirmación
  } else if (out.reply.empty()) {
    out.error = "reply obligatorio para cerrar|ask_user";
    return out;
  }
  out.ok = true;
  return out;
}

std::vector<std::string> admin_legal_dos(const AdminState& st, int max_proposes, int max_spawns) {
  std::vector<std::string> out;
  if (st.awaiting_edit_confirm) {
    out.push_back("confirmar_editar");
    out.push_back("seguir_explorando");
    out.push_back("cerrar");
    out.push_back("ask_user");
    return out;
  }
  out.push_back("cerrar");
  out.push_back("ask_user");
  out.push_back("editar");
  const bool at_propose_cap = st.proposes >= max_proposes;
  const bool at_spawn_cap = st.spawns >= max_spawns;
  if (!at_propose_cap && !at_spawn_cap && !st.done && !st.clarify) {
    out.insert(out.begin(), "spawn");
  }
  if (st.verify_reject_pending) {
    out.push_back("seguir_explorando");
  }
  return out;
}

bool admin_legal(const AdminState& st, const AdminOla& ola, int max_proposes, int max_spawns,
                 std::string* err) {
  if (!ola.ok) {
    if (err) {
      *err = ola.error.empty() ? "ola inválida" : ola.error;
    }
    return false;
  }
  if (st.done) {
    if (err) {
      *err = "sesión ya cerrada";
    }
    return false;
  }
  const auto legal = admin_legal_dos(st, max_proposes, max_spawns);
  const std::string want = admin_do_name(ola.do_kind);
  if (std::find(legal.begin(), legal.end(), want) == legal.end()) {
    if (err) {
      *err = "do ilegal en este turno: " + want;
    }
    return false;
  }
  if (ola.do_kind == AdminDo::Spawn && ola.spawn.tipo == AdminSpawnTipo::Explore) {
    int explores = 0;
    for (const auto& j : st.jobs) {
      if (j.tipo == "explore") {
        ++explores;
      }
    }
    if (explores >= kAdminMaxExplores) {
      if (err) {
        *err = "tope de explore; cierra, ask_user, o usa search/read/edit";
      }
      return false;
    }
  }
  if (ola.do_kind == AdminDo::Spawn && ola.spawn.tipo == AdminSpawnTipo::Edit) {
    if (!st.edit_confirmed) {
      if (err) {
        *err = "edit: primero editar→confirmar_editar (cubre/falta)";
      }
      return false;
    }
    if (!admin_notebook_has_path(st, ola.spawn.arg)) {
      if (err) {
        *err = "edit: path no está en notebook/UI; search/read/explore antes";
      }
      return false;
    }
  }
  if (ola.do_kind == AdminDo::SeguirExplorando) {
    int explores = 0;
    for (const auto& j : st.jobs) {
      if (j.tipo == "explore") {
        ++explores;
      }
    }
    if (explores >= kAdminMaxExplores) {
      if (err) {
        *err = "tope de explore; confirma edición, cierra o ask_user";
      }
      return false;
    }
  }
  if (ola.do_kind == AdminDo::Spawn && ola.spawn.tipo == AdminSpawnTipo::WebFetch) {
    if (!admin_url_fetch_allowed(ola.spawn.arg)) {
      if (err) {
        *err = "web_fetch: solo http(s) público (no localhost/privado)";
      }
      return false;
    }
    if (!admin_notebook_has_path(st, ola.spawn.arg)) {
      if (err) {
        *err = "web_fetch: URL no anclada; haz web (search) antes";
      }
      return false;
    }
  }
  return true;
}

AdminJobResult admin_explore_stub(const AdminSpawn& spawn) {
  AdminJobResult r;
  r.ok = true;
  r.veredicto = "no_concluyente";
  r.summary =
      "explore pendiente (stub). Acumula en notebook; NO re-explore — usa search/read o cierra";
  if (!spawn.brief.empty()) {
    r.summary += "; brief=" + spawn.brief;
    r.facts.push_back("brief:" + spawn.brief);
  }
  return r;
}

AdminJobResult admin_run_explore_lite(const AdminSpawn& spawn, L2Brain& brain,
                                      const std::string& workspace_root,
                                      const AdminLoopOpts& opts,
                                      AdminGrepFn grep_fn) {
  AdminJobResult r;
  const std::string consulta =
      !spawn.brief.empty() ? spawn.brief : (!spawn.arg.empty() ? spawn.arg : std::string("explore"));
  const std::string sys =
      "Eres el EXPLORADOR del WORKSPACE abierto (C++ u otros). Tools: grep|read|cerrar. UN JSON:\n"
      "{\"do\":\"grep\",\"pattern\":\"…\",\"why\":\"…\"}\n"
      "{\"do\":\"read\",\"path\":\"src/…\",\"offset\":1,\"why\":\"…\"}\n"
      "{\"do\":\"cerrar\",\"veredicto\":\"encontrado|no_encontrado|parcial\","
      "\"simbolos\":[\"path:symbol\"],\"evidencia\":[\"path:línea\"],"
      "\"falta\":[],\"why\":\"…\"}\n"
      "Solo paths bajo WORKSPACE_ROOT. Caza identificadores reales (FileTree, file_tree_panel, "
      "MakeFileTreePanel, workspace_indexer, …). Ignora .tuide/ y build/. "
      "Responde SOLO a TU consulta (brief). why = hechos; falta obligatorio al cerrar.";

  std::string conversation =
      "## Consulta (brief del piloto)\n" + consulta +
      "\n\nWORKSPACE_ROOT=`" + workspace_root +
      "` — solo paths bajo este root (src/ preferido). "
      "Empieza por grep de símbolos (FileTree|file_tree|indexer) en src/.\n"
      "Elige UNA acción JSON.\n";
  int greps = 0;
  int reads = 0;
  std::vector<std::string> seen_paths;
  std::vector<std::string> read_paths;

  ui_note(opts, "→ Explorador: " + ui_clip(consulta, 160));

  for (int step = 0; step < kAdminExploreMaxSteps; ++step) {
    if (opts.cancel != nullptr && opts.cancel->load()) {
      r.ok = false;
      r.error = "cancel";
      r.veredicto = "no_concluyente";
      r.summary = "explore cancelado";
      return r;
    }
    const bool last = (step >= kAdminExploreMaxSteps - 1);
    if (last) {
      conversation +=
          "\n\n## Usuario\nÚLTIMA OLA — cierra YA con do=cerrar, veredicto, simbolos, "
          "evidencia, falta, why.\n";
    }
    L2BrainRequest req;
    req.system_prompt = sys;
    req.user_prompt = conversation;
    req.phase = "admin_explore";
    req.max_tokens = opts.settings.max_tokens > 0 ? opts.settings.max_tokens : 700;
    req.n_ctx = opts.settings.n_ctx_remote > 0 ? opts.settings.n_ctx_remote : opts.settings.n_ctx;
    req.temperature = 0.1f;
    req.enable_thinking = false;
    L2BrainResult br = brain.propose(req, opts.cancel);
    if (!br.ok) {
      r.ok = false;
      r.error = br.error.empty() ? "explore brain falló" : br.error;
      r.veredicto = "no_concluyente";
      r.summary = r.error;
      return r;
    }
    const std::string raw = br.text.empty() ? br.raw : br.text;
    std::string blob = raw;
    const auto brace = blob.find('{');
    if (brace != std::string::npos) {
      blob = blob.substr(brace);
    }
    nlohmann::json j;
    try {
      j = nlohmann::json::parse(blob);
    } catch (...) {
      conversation += "\n\n## Asistente\n" + raw.substr(0, 600) +
                      "\n\n## Usuario\nJSON inválido. grep|read|cerrar.\n";
      continue;
    }
    const std::string do_kind = ascii_lower(trim_copy(json_str(j, "do")));
    if (do_kind == "cerrar") {
      std::string verd = ascii_lower(trim_copy(json_str(j, "veredicto")));
      if (verd != "encontrado" && verd != "no_encontrado" && verd != "parcial") {
        verd = "no_concluyente";
      }
      r.ok = true;
      r.veredicto = verd;
      r.simbolos = json_str_array(j, "simbolos");
      r.evidencia = json_str_array(j, "evidencia");
      r.summary = trim_copy(json_str(j, "why"));
      if (r.summary.size() < 4) {
        r.summary = "explore " + verd;
      }
      for (const auto& s : r.simbolos) {
        const auto col = s.find(':');
        if (col != std::string::npos && s.find('/') != std::string::npos) {
          const std::string p = s.substr(0, col);
          if (!admin_path_is_noise(p) &&
              std::find(r.paths.begin(), r.paths.end(), p) == r.paths.end()) {
            r.paths.push_back(p);
          }
        }
      }
      for (const auto& ev : r.evidencia) {
        const auto col = ev.find(':');
        if (col != std::string::npos && ev.find('/') != std::string::npos) {
          const std::string p = ev.substr(0, col);
          if (!admin_path_is_noise(p) &&
              std::find(r.paths.begin(), r.paths.end(), p) == r.paths.end()) {
            r.paths.push_back(p);
          }
        }
      }
      // Solo paths leídos (no todos los hits de grep: envenenan el notebook con .tuide/).
      for (const auto& p : read_paths) {
        if (!admin_path_is_noise(p) &&
            std::find(r.paths.begin(), r.paths.end(), p) == r.paths.end()) {
          r.paths.push_back(p);
        }
      }
      if (verd == "no_encontrado") {
        // No adjuntar rastros de grep fallido.
        r.paths.clear();
      }
      r.facts.push_back("explore:veredicto=" + verd);
      r.facts.push_back("brief:" + consulta.substr(0, 160));
      {
        std::ostringstream line;
        line << "  · conclusión del explorador (" << verdicto_explore_ui(verd) << "): "
             << ui_clip(r.summary, 220);
        ui_note(opts, line.str());
        if (!r.simbolos.empty()) {
          std::ostringstream syms;
          syms << "  · anclas: ";
          for (std::size_t i = 0; i < r.simbolos.size() && i < 4; ++i) {
            if (i) {
              syms << ", ";
            }
            syms << r.simbolos[i];
          }
          if (r.simbolos.size() > 4) {
            syms << "…";
          }
          ui_note(opts, syms.str());
        }
      }
      return r;
    }
    if (last) {
      conversation += "\n\n## Usuario\nSolo cerrar en última ola.\n";
      continue;
    }
    if (do_kind == "grep") {
      if (greps >= kAdminExploreMaxGrep) {
        conversation += "\n\n## Usuario\nTope grep. Cierra o read.\n";
        continue;
      }
      ++greps;
      const std::string pattern = trim_copy(json_str(j, "pattern"));
      AdminJobResult hit =
          grep_fn ? grep_fn(pattern) : admin_run_search_rg(pattern, workspace_root);
      {
        std::ostringstream line;
        line << "  · buscar «" << ui_clip(pattern, 80) << "»";
        if (!hit.paths.empty()) {
          line << " → " << hit.paths.size() << " archivo"
               << (hit.paths.size() == 1 ? "" : "s");
          line << " (p.ej. `" << hit.paths.front() << "`)";
        } else if (hit.summary.find("(0 hits)") != std::string::npos) {
          line << " → sin coincidencias";
        }
        ui_note(opts, line.str());
      }
      for (const auto& p : hit.paths) {
        if (admin_path_is_noise(p)) {
          continue;
        }
        if (std::find(seen_paths.begin(), seen_paths.end(), p) == seen_paths.end()) {
          seen_paths.push_back(p);
        }
      }
      // Resume al hijo: solo paths útiles (src/), no basura .tuide.
      std::string grep_body = hit.summary.substr(0, 2500);
      if (!hit.facts.empty()) {
        std::ostringstream useful;
        int n = 0;
        for (const auto& f : hit.facts) {
          if (admin_path_is_noise(f)) {
            continue;
          }
          useful << f << "\n";
          if (++n >= 12) {
            break;
          }
        }
        if (n > 0) {
          grep_body = useful.str();
        } else if (hit.facts.empty() && hit.summary.find("(0 hits)") != std::string::npos) {
          grep_body = hit.summary;
        } else {
          grep_body = hit.summary + "\n(sin hits fuera de .tuide/build — prueba otro pattern en src/)\n";
        }
      }
      conversation += "\n\n## Asistente\n" + raw.substr(0, 400) + "\n\n## Usuario\n## grep `" +
                      pattern + "`\n" + grep_body +
                      "\n\nSiguiente (grep|read|cerrar).\n";
      continue;
    }
    if (do_kind == "read") {
      if (reads >= kAdminExploreMaxRead) {
        conversation += "\n\n## Usuario\nTope read. Cierra.\n";
        continue;
      }
      ++reads;
      std::string path = trim_copy(json_str(j, "path"));
      const auto colon = path.find(':');
      if (colon != std::string::npos) {
        path = path.substr(0, colon);
      }
      if (admin_path_is_noise(path)) {
        conversation += "\n\n## Asistente\n" + raw.substr(0, 400) +
                        "\n\n## Usuario\npath bajo .tuide/build ignorado. Lee src/…\n";
        continue;
      }
      AdminJobResult hit = admin_run_read_file(path, workspace_root);
      ui_note(opts, "  · leer `" + path + "`" +
                        (hit.ok ? (hit.truncated ? " (recorte)" : "") : " — falló"));
      if (!path.empty() &&
          std::find(seen_paths.begin(), seen_paths.end(), path) == seen_paths.end()) {
        seen_paths.push_back(path);
      }
      if (!path.empty() &&
          std::find(read_paths.begin(), read_paths.end(), path) == read_paths.end()) {
        read_paths.push_back(path);
      }
      conversation += "\n\n## Asistente\n" + raw.substr(0, 400) + "\n\n## Usuario\n## read `" +
                      path + "`\n" + hit.summary.substr(0, 3000) +
                      "\n\nSiguiente (grep|read|cerrar).\n";
      continue;
    }
    conversation += "\n\n## Asistente\n" + raw.substr(0, 400) +
                    "\n\n## Usuario\ndo inválido. grep|read|cerrar.\n";
  }
  r.ok = true;
  r.veredicto = "no_concluyente";
  r.summary = "explore: tope de olas sin cerrar";
  r.paths = seen_paths;
  r.facts.push_back("explore:veredicto=no_concluyente");
  return r;
}

bool admin_shell_cmd_allowed(const std::string& cmd) {
  const std::string c = trim_copy(cmd);
  if (c.empty() || c.size() > 500) {
    return false;
  }
  if (c == "launch" || c == "compile" || c == "test") {
    return true;
  }
  // Pipe seguro opcional: "<allowlisted> … | head -N" / "| tail -N" (solo truncar).
  std::string primary = c;
  const auto pipe = c.find('|');
  if (pipe != std::string::npos) {
    if (c.find('|', pipe + 1) != std::string::npos) {
      return false;
    }
    primary = trim_copy(c.substr(0, pipe));
    const std::string rest = ascii_lower(trim_copy(c.substr(pipe + 1)));
    const bool headish = rest.rfind("head", 0) == 0;
    const bool tailish = rest.rfind("tail", 0) == 0;
    if (!headish && !tailish) {
      return false;
    }
    for (char ch : rest) {
      if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == ' ' || ch == '-')) {
        return false;
      }
    }
  }
  // Meta: '&' solo (background) prohibido; '&&' permitido (se valida cada tramo abajo).
  static const char* kMeta[] = {"`", "$(", "${", ">", "<", ";", "\n", "\r"};
  for (const char* m : kMeta) {
    if (primary.find(m) != std::string::npos) {
      return false;
    }
  }
  for (std::size_t i = 0; i < primary.size(); ++i) {
    if (primary[i] != '&') {
      continue;
    }
    if (i + 1 < primary.size() && primary[i + 1] == '&') {
      ++i;
      continue;
    }
    return false;
  }
  // '|' fuera del caso head/tail ya filtrado arriba.
  if (pipe == std::string::npos && c.find('|') != std::string::npos) {
    return false;
  }
  static const char* kBan[] = {"sudo", " rm", "rm ", "\trm", "mv ", "chmod", "chown",
                               "curl", "wget", "ssh ", "scp ", "dd ", "mkfs"};
  static const char* kOk[] = {"ls",    "find", "pwd",      "echo",     "wc",       "head",
                              "tail",  "cat",  "file",     "du",       "which",    "printf",
                              "basename", "dirname", "realpath", "tree", "test",  "true",
                              "false", "stat", "readlink", "env",      "printenv", "id",
                              "uname", "date", "seq",      "rg",       "grep"};
  auto segment_ok = [&](const std::string& seg) -> bool {
    const std::string s = trim_copy(seg);
    if (s.empty()) {
      return false;
    }
    const std::string low = ascii_lower(s);
    for (const char* b : kBan) {
      if (low.find(b) != std::string::npos) {
        return false;
      }
    }
    if (low.rfind("rm ", 0) == 0 || low == "rm") {
      return false;
    }
    std::string first;
    for (char ch : s) {
      if (std::isspace(static_cast<unsigned char>(ch))) {
        break;
      }
      first.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    for (const char* ok : kOk) {
      if (first == ok) {
        return true;
      }
    }
    return false;
  };
  // Cadena "cmd1 && cmd2 && …": cada verbo allowlisted.
  std::size_t start = 0;
  while (start <= primary.size()) {
    const auto amp = primary.find("&&", start);
    const std::string seg =
        amp == std::string::npos ? primary.substr(start) : primary.substr(start, amp - start);
    if (!segment_ok(seg)) {
      return false;
    }
    if (amp == std::string::npos) {
      break;
    }
    start = amp + 2;
  }
  return true;
}

std::string admin_clip_output(const std::string& text, bool* truncated, int* raw_bytes) {
  const int raw = static_cast<int>(text.size());
  if (raw_bytes) {
    *raw_bytes = raw;
  }
  const int budget = kAdminClipHeadChars + kAdminClipTailChars + 80;
  if (raw <= budget) {
    if (truncated) {
      *truncated = false;
    }
    return text;
  }
  if (truncated) {
    *truncated = true;
  }
  std::string head = text.substr(0, static_cast<std::size_t>(kAdminClipHeadChars));
  std::string tail =
      text.substr(text.size() - static_cast<std::size_t>(kAdminClipTailChars));
  std::ostringstream out;
  out << "[truncated raw_bytes=" << raw << " head=" << kAdminClipHeadChars
      << " tail=" << kAdminClipTailChars << "]\n--- head ---\n"
      << head << "\n--- tail ---\n"
      << tail << "\n";
  return out.str();
}

namespace {

constexpr int kShellEnrichMaxPaths = 12;
constexpr int kShellEnrichMaxFacts = 8;

bool looks_like_path_token(const std::string& t) {
  if (t.empty() || t == "." || t == "..") {
    return false;
  }
  if (t[0] == '-') {
    return false;
  }
  // Flags like -n20 already skipped; reject pure numbers.
  bool all_digit = true;
  for (char c : t) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      all_digit = false;
      break;
    }
  }
  if (all_digit) {
    return false;
  }
  return t.find('/') != std::string::npos || t.find('.') != std::string::npos ||
         t[0] == '.' || std::isalnum(static_cast<unsigned char>(t[0]));
}

std::vector<std::string> shell_tokenize(const std::string& cmd) {
  std::vector<std::string> out;
  std::string cur;
  bool in_sq = false;
  bool in_dq = false;
  for (char c : cmd) {
    if (c == '\'' && !in_dq) {
      in_sq = !in_sq;
      continue;
    }
    if (c == '"' && !in_sq) {
      in_dq = !in_dq;
      continue;
    }
    if (!in_sq && !in_dq && std::isspace(static_cast<unsigned char>(c))) {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
      continue;
    }
    cur.push_back(c);
  }
  if (!cur.empty()) {
    out.push_back(cur);
  }
  return out;
}

void push_unique_path(std::vector<std::string>* paths, const std::string& p) {
  if (paths == nullptr || p.empty()) {
    return;
  }
  std::string norm = p;
  while (norm.size() > 2 && norm.rfind("./", 0) == 0) {
    norm = norm.substr(2);
  }
  while (!norm.empty() && norm.back() == '/') {
    norm.pop_back();
  }
  if (norm.empty() || norm == "." || norm == "..") {
    return;
  }
  if (std::find(paths->begin(), paths->end(), norm) != paths->end()) {
    return;
  }
  if (static_cast<int>(paths->size()) >= kShellEnrichMaxPaths) {
    return;
  }
  paths->push_back(norm);
}

void push_fact(std::vector<std::string>* facts, std::string f) {
  if (facts == nullptr || f.empty()) {
    return;
  }
  utf8_resize(&f, static_cast<std::size_t>(kAdminNotebookFactChars));
  if (static_cast<int>(facts->size()) >= kShellEnrichMaxFacts) {
    return;
  }
  facts->push_back(std::move(f));
}

bool path_exists_under(const std::string& cwd, const std::string& rel) {
  std::error_code ec;
  fs::path p = rel;
  if (!p.is_absolute() && !cwd.empty()) {
    p = fs::path(cwd) / rel;
  }
  return fs::exists(p, ec);
}

std::string join_dir_entry(const std::string& dir, const std::string& entry) {
  if (entry.find('/') != std::string::npos || dir.empty() || dir == ".") {
    return entry;
  }
  if (!dir.empty() && dir.back() == '/') {
    return dir + entry;
  }
  return dir + "/" + entry;
}

}  // namespace

void admin_shell_enrich_result(const std::string& cmd, const std::string& captured,
                               const std::string& cwd, AdminJobResult* r) {
  if (r == nullptr) {
    return;
  }
  const auto toks = shell_tokenize(trim_copy(cmd));
  if (toks.empty()) {
    return;
  }
  const std::string verb = ascii_lower(toks[0]);

  std::vector<std::string> argv_paths;
  for (std::size_t i = 1; i < toks.size(); ++i) {
    const std::string& t = toks[i];
    if (t.empty() || t[0] == '-') {
      // head -n 20 file → skip -n and next if pure number already handled by looks_like
      continue;
    }
    if (looks_like_path_token(t)) {
      push_unique_path(&argv_paths, t);
    }
  }

  auto add_content_facts = [&](const char* kind, const std::string& path_hint) {
    if (!path_hint.empty()) {
      push_fact(&r->facts, std::string("peek:path=") + path_hint + " kind=" + kind);
    }
    std::istringstream iss(captured);
    std::string line;
    int n = 0;
    while (std::getline(iss, line) && n < 4) {
      line = trim_copy(line);
      if (line.empty() || line.rfind("---", 0) == 0 || line.rfind("[truncated", 0) == 0) {
        continue;
      }
      push_fact(&r->facts, line);
      ++n;
    }
  };

  if (verb == "ls" || verb == "find" || verb == "tree") {
    const std::string list_dir = argv_paths.empty() ? std::string(".") : argv_paths.front();
    for (const auto& p : argv_paths) {
      push_unique_path(&r->paths, p);
    }
    std::istringstream iss(captured);
    std::string line;
    int listed = 0;
    while (std::getline(iss, line)) {
      line = trim_copy(line);
      if (line.empty() || line.rfind("total ", 0) == 0 || line.rfind("---", 0) == 0 ||
          line.rfind("[truncated", 0) == 0) {
        continue;
      }
      // `ls -l`: take last field as name
      std::string entry = line;
      if (verb == "ls" && (line[0] == '-' || line[0] == 'd' || line[0] == 'l') &&
          line.find(' ') != std::string::npos) {
        const auto sp = line.rfind(' ');
        if (sp != std::string::npos) {
          entry = line.substr(sp + 1);
        }
      }
      const std::string joined = join_dir_entry(list_dir, entry);
      if (verb == "find" || path_exists_under(cwd, joined) || looks_like_path_token(entry)) {
        push_unique_path(&r->paths, joined);
        ++listed;
      }
    }
    push_fact(&r->facts, "list:dir=" + list_dir + " n≈" + std::to_string(listed));
    // Keep a few entry names as facts for chaining (basename visibility).
    int shown = 0;
    for (const auto& p : r->paths) {
      if (p == list_dir) {
        continue;
      }
      push_fact(&r->facts, p);
      if (++shown >= 5) {
        break;
      }
    }
    return;
  }

  if (verb == "head" || verb == "tail" || verb == "cat") {
    for (const auto& p : argv_paths) {
      push_unique_path(&r->paths, p);
    }
    const std::string hint = argv_paths.empty() ? std::string{} : argv_paths.front();
    add_content_facts(verb.c_str(), hint);
    return;
  }

  if (verb == "wc") {
    for (const auto& p : argv_paths) {
      push_unique_path(&r->paths, p);
    }
    std::istringstream iss(captured);
    std::string line;
    while (std::getline(iss, line)) {
      line = trim_copy(line);
      if (line.empty()) {
        continue;
      }
      // "443 path" or "10 20 30 path"
      std::istringstream ls(line);
      std::vector<std::string> cols;
      std::string col;
      while (ls >> col) {
        cols.push_back(col);
      }
      if (cols.empty()) {
        continue;
      }
      std::string path;
      int lines_n = -1;
      if (cols.size() >= 2 && std::isdigit(static_cast<unsigned char>(cols[0][0]))) {
        try {
          lines_n = std::stoi(cols[0]);
        } catch (...) {
          lines_n = -1;
        }
        path = cols.back();
        if (path == "total") {
          path.clear();
        }
      }
      if (!path.empty()) {
        push_unique_path(&r->paths, path);
      }
      if (lines_n >= 0) {
        std::ostringstream f;
        f << "wc:path=" << (path.empty() ? (argv_paths.empty() ? "?" : argv_paths.front()) : path)
          << " lines=" << lines_n;
        push_fact(&r->facts, f.str());
      } else {
        push_fact(&r->facts, line);
      }
    }
    if (r->facts.empty() && !argv_paths.empty()) {
      push_fact(&r->facts, "wc:path=" + argv_paths.front());
    }
    return;
  }

  // Generic: argv paths + a few log lines as facts
  for (const auto& p : argv_paths) {
    push_unique_path(&r->paths, p);
  }
  if (!argv_paths.empty()) {
    push_fact(&r->facts, "shell:cmd=" + verb + " path=" + argv_paths.front());
  }
  add_content_facts(verb.c_str(), argv_paths.empty() ? std::string{} : argv_paths.front());
}

AdminJobResult admin_run_shell_safe(const std::string& cmd, const std::string& cwd) {
  AdminJobResult r;
  if (!admin_shell_cmd_allowed(cmd)) {
    r.error =
        "shell denegado (allowlist: ls/find/rg/…; un solo '| head -N'|'| tail -N' OK; "
        "sin ;>&` — para código usa search/explore, no asumas .ts/.js)";
    r.summary = r.error;
    return r;
  }
  std::string perimeter_err;
  if (!admin_shell_stays_in_workspace(cmd, cwd, &perimeter_err)) {
    r.error = perimeter_err.empty() ? "shell fuera del workspace root" : perimeter_err;
    r.summary = r.error;
    return r;
  }
  if (cmd == "launch" || cmd == "compile" || cmd == "test") {
    r.ok = true;
    r.summary = "shell named-task stub arg=" + cmd;
    return r;
  }
  std::string full = cmd;
  if (!cwd.empty()) {
    full = "cd " + shell_single_quote(cwd) + " && " + cmd;
  }
  full += " 2>&1";
  FILE* pipe = ::popen(full.c_str(), "r");
  if (pipe == nullptr) {
    r.error = "popen falló";
    r.summary = r.error;
    return r;
  }
  std::string captured;
  std::array<char, 512> buf{};
  while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
    captured += buf.data();
    if (captured.size() > static_cast<std::size_t>(kAdminSummaryChars) * 4) {
      captured += "\n…(captura cortada)…\n";
      break;
    }
  }
  const int code = ::pclose(pipe);
  bool trunc = false;
  int raw = 0;
  r.log_tail = admin_clip_output(captured, &trunc, &raw);
  r.truncated = trunc;
  r.raw_bytes = raw;
  r.ok = (code == 0);
  std::ostringstream sum;
  sum << "shell exit=" << code << " bytes=" << raw << (trunc ? " truncated=1" : "");
  r.summary = sum.str();
  if (!r.ok) {
    r.error = "exit_code!=0";
  }
  admin_shell_enrich_result(cmd, captured, cwd, &r);
  if (!r.paths.empty()) {
    r.summary += " paths=" + std::to_string(r.paths.size());
  }
  return r;
}

AdminJobResult admin_run_search_rg(const std::string& query, const std::string& cwd) {
  AdminJobResult r;
  const std::string q = trim_copy(query);
  if (q.empty()) {
    r.error = "search sin query";
    r.summary = r.error;
    return r;
  }
  // In-process (no depende de rg en PATH del GUI). OR con '|'; prioriza src/.
  std::vector<std::string> needles;
  {
    std::string cur;
    for (char ch : q) {
      if (ch == '|') {
        cur = trim_copy(cur);
        if (!cur.empty()) {
          needles.push_back(cur);
        }
        cur.clear();
      } else {
        cur.push_back(ch);
      }
    }
    cur = trim_copy(cur);
    if (!cur.empty()) {
      needles.push_back(cur);
    }
  }
  if (needles.empty()) {
    r.error = "search sin query";
    r.summary = r.error;
    return r;
  }

  auto is_skip_dir = [](const std::string& name) {
    return name == ".tuide" || name == "build" || name == ".git" || name == "node_modules" ||
           name == ".cache" || name == "CMakeFiles";
  };
  auto is_code_ext = [](const fs::path& p) {
    const std::string e = p.extension().string();
    return e == ".cpp" || e == ".hpp" || e == ".h" || e == ".cc" || e == ".c" || e == ".md" ||
           e == ".cmake" || e == ".txt" || e == ".json";
  };
  auto line_hits = [&](const std::string& line) -> bool {
    for (const auto& n : needles) {
      if (line.find(n) != std::string::npos) {
        return true;
      }
    }
    return false;
  };

  const fs::path root = cwd.empty() ? fs::current_path() : fs::path(cwd);
  std::ostringstream captured;
  int hit_lines = 0;
  constexpr int kMaxHitLines = 40;
  std::error_code ec;
  if (!fs::is_directory(root, ec)) {
    r.error = "search: cwd no es directorio";
    r.summary = r.error;
    return r;
  }

  auto scan_tree = [&](const fs::path& base, bool src_only) {
    std::error_code walk_ec;
    fs::recursive_directory_iterator it(
        base, fs::directory_options::skip_permission_denied, walk_ec);
    const fs::recursive_directory_iterator end;
    for (; it != end && hit_lines < kMaxHitLines; it.increment(walk_ec)) {
      if (walk_ec) {
        walk_ec.clear();
        continue;
      }
      const fs::directory_entry& ent = *it;
      if (ent.is_directory(walk_ec)) {
        if (is_skip_dir(ent.path().filename().string())) {
          it.disable_recursion_pending();
        }
        continue;
      }
      if (!ent.is_regular_file(walk_ec) || !is_code_ext(ent.path())) {
        continue;
      }
      const fs::path rel = fs::relative(ent.path(), root, walk_ec);
      if (walk_ec || rel.empty()) {
        continue;
      }
      const std::string rel_s = rel.generic_string();
      if (admin_path_is_noise(rel_s)) {
        continue;
      }
      if (src_only && rel_s.rfind("src/", 0) != 0) {
        continue;
      }
      std::ifstream in(ent.path());
      if (!in) {
        continue;
      }
      std::string line;
      int lineno = 0;
      while (std::getline(in, line) && hit_lines < kMaxHitLines) {
        ++lineno;
        if (line.size() > 4000) {
          continue;
        }
        if (!line_hits(line)) {
          continue;
        }
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
          line.pop_back();
        }
        captured << rel_s << ':' << lineno << ':' << line.substr(0, 200) << '\n';
        ++hit_lines;
      }
    }
  };

  // Primero src/ (donde vive el producto); luego el resto si faltan hits.
  const fs::path src = root / "src";
  if (fs::is_directory(src, ec)) {
    scan_tree(src, true);
  }
  if (hit_lines < 8) {
    scan_tree(root, false);
  }

  bool trunc = false;
  int raw = 0;
  const std::string text = captured.str();
  r.log_tail = admin_clip_output(text, &trunc, &raw);
  r.truncated = trunc;
  r.raw_bytes = raw;
  r.ok = true;
  admin_collect_search_hits(text, &r);
  r.summary = "search hits≈" + std::to_string(r.paths.size()) + " bytes=" + std::to_string(raw);
  if (r.paths.empty()) {
    r.summary += " (0 hits)";
  }
  return r;
}

void admin_collect_search_hits(const std::string& text, AdminJobResult* r) {
  if (r == nullptr || text.empty()) {
    return;
  }
  auto push_path = [&](std::string p) {
    p = trim_copy(std::move(p));
    while (!p.empty() && (p.front() == '.' || p.front() == '/')) {
      // keep relative "src/..." ; strip leading "./"
      if (p.rfind("./", 0) == 0) {
        p = p.substr(2);
        continue;
      }
      break;
    }
    if (p.empty() || admin_path_is_noise(p)) {
      return;
    }
    if (p.find('/') == std::string::npos && p.find('.') == std::string::npos) {
      return;
    }
    // Evita capturar "explorer: 195" u otras claves de resumen.
    if (p.find(' ') != std::string::npos) {
      p = trim_copy(p.substr(0, p.find(' ')));
    }
    if (std::find(r->paths.begin(), r->paths.end(), p) == r->paths.end()) {
      r->paths.push_back(p);
    }
  };

  bool in_top_files = false;
  std::istringstream iss(text);
  std::string line;
  int fact_n = 0;
  while (std::getline(iss, line)) {
    const std::string raw_line = line;
    line = trim_copy(line);
    if (line.empty()) {
      in_top_files = false;
      continue;
    }
    if (line.rfind("top_files:", 0) == 0) {
      in_top_files = true;
      continue;
    }
    if (line.rfind("needles", 0) == 0 || line.rfind("hint:", 0) == 0 ||
        line.rfind("needles_tried:", 0) == 0) {
      in_top_files = false;
      continue;
    }
    if (in_top_files) {
      // Sale de top_files al primer hit path:line:…
      const auto c_top = line.find(':');
      if (c_top != std::string::npos && c_top + 1 < line.size() &&
          line.find('/') != std::string::npos &&
          std::isdigit(static_cast<unsigned char>(line[c_top + 1]))) {
        in_top_files = false;
      } else if (line[0] == '-' ||
                 (line.find(':') != std::string::npos && line.find('/') == std::string::npos)) {
        in_top_files = false;
      } else {
        std::string p = line;
        const auto sp = p.find("  ");
        if (sp != std::string::npos) {
          p = p.substr(0, sp);
        }
        const auto br = p.find(" [");
        if (br != std::string::npos) {
          p = p.substr(0, br);
        }
        push_path(p);
        continue;
      }
    }
    // rg / tool hit: path:line:…  (path debe contener /)
    const auto c1 = line.find(':');
    if (c1 != std::string::npos && c1 > 0) {
      const std::string maybe = line.substr(0, c1);
      if (maybe.find('/') != std::string::npos &&
          c1 + 1 < line.size() &&
          std::isdigit(static_cast<unsigned char>(line[c1 + 1]))) {
        push_path(maybe);
        if (fact_n < 12) {
          std::string fact = raw_line;
          utf8_resize(&fact, static_cast<std::size_t>(kAdminNotebookFactChars));
          r->facts.push_back(std::move(fact));
          ++fact_n;
        }
      }
    }
  }
}

AdminJobResult admin_run_read_file(const std::string& target, const std::string& cwd) {
  AdminJobResult r;
  std::string path = trim_copy(target);
  std::string symbol;
  int line_start = 0;
  int line_end = 0;
  // Soporta path:Symbol | path:N | path:N:M (pelar sufijos de derecha a izquierda).
  while (path.find('/') != std::string::npos) {
    const auto col = path.rfind(':');
    if (col == std::string::npos || col == 0) {
      break;
    }
    const std::string maybe = path.substr(0, col);
    const std::string suf = path.substr(col + 1);
    if (maybe.find('.') == std::string::npos) {
      break;
    }
    bool all_digit = !suf.empty();
    for (char ch : suf) {
      if (!std::isdigit(static_cast<unsigned char>(ch))) {
        all_digit = false;
        break;
      }
    }
    if (all_digit) {
      const int n = std::atoi(suf.c_str());
      if (line_end == 0) {
        line_end = n;
      } else {
        line_start = n;
      }
      path = maybe;
      continue;
    }
    if (line_start == 0 && line_end == 0 && symbol.empty() && !suf.empty()) {
      symbol = suf;
      path = maybe;
    }
    break;
  }
  if (line_start == 0 && line_end > 0) {
    line_start = line_end;
    line_end = line_start + 80;
  }
  if (line_start > 0 && line_end < line_start) {
    std::swap(line_start, line_end);
  }
  std::string abs;
  std::string rel;
  std::string perr;
  if (!cwd.empty()) {
    if (!admin_resolve_in_workspace(cwd, path, &abs, &rel, &perr)) {
      r.error = perr.empty() ? ("read fuera del workspace: " + path) : perr;
      r.summary = r.error;
      return r;
    }
    path = rel.empty() ? path : rel;
  } else {
    abs = path;
  }
  std::string body = read_file(abs.empty() ? path : abs);
  if (body.empty()) {
    r.error = "read: no se pudo abrir " + path;
    r.summary = r.error;
    return r;
  }
  if (line_start > 0) {
    std::istringstream iss(body);
    std::ostringstream slice;
    std::string line;
    int ln = 0;
    int kept = 0;
    while (std::getline(iss, line)) {
      ++ln;
      if (ln < line_start) {
        continue;
      }
      if (line_end > 0 && ln > line_end) {
        break;
      }
      slice << ln << ':' << line << '\n';
      if (++kept >= 120) {
        break;
      }
    }
    body = slice.str();
    if (body.empty()) {
      r.facts.push_back("read:line fuera de rango " + std::to_string(line_start));
      body = read_file(abs.empty() ? path : abs);
      utf8_resize(&body, static_cast<std::size_t>(kAdminReadMaxChars));
    } else {
      r.facts.push_back("read:lines=" + std::to_string(line_start) + "-" +
                        std::to_string(line_end > 0 ? line_end : line_start));
    }
  } else if (!symbol.empty() && !std::isdigit(static_cast<unsigned char>(symbol[0]))) {
    const auto pos = body.find(symbol);
    if (pos != std::string::npos) {
      const std::size_t start = pos > 400 ? pos - 400 : 0;
      body = body.substr(start, static_cast<std::size_t>(kAdminReadMaxChars));
      r.simbolos.push_back(path + ":" + symbol);
    } else {
      utf8_resize(&body, static_cast<std::size_t>(kAdminReadMaxChars));
      r.facts.push_back("símbolo no encontrado en archivo; head clip");
    }
  } else {
    utf8_resize(&body, static_cast<std::size_t>(kAdminReadMaxChars));
  }
  utf8_sanitize_inplace(&body);
  bool trunc = false;
  int raw = 0;
  r.log_tail = admin_clip_output(body, &trunc, &raw);
  r.truncated = trunc;
  r.raw_bytes = raw;
  r.paths.push_back(path);
  r.ok = true;
  r.summary = "read " + path + " bytes=" + std::to_string(raw);
  r.facts.push_back("leído:" + path);
  return r;
}

AdminJobResult admin_run_diagnostics_stub(const AdminSpawn&) {
  AdminJobResult r;
  r.ok = true;
  r.summary = "diagnostics stub (sin LSP en CLI); en app usa ToolRegistry";
  r.facts.push_back("diagnostics:stub");
  return r;
}

AdminJobResult admin_run_test_stub(const AdminSpawn& spawn) {
  AdminJobResult r;
  r.ok = true;
  r.summary = "test stub arg=" + spawn.arg + " (app: TaskRunner)";
  r.facts.push_back("test:" + spawn.arg);
  return r;
}

AdminJobResult admin_run_edit_file(const AdminSpawn& spawn, const AdminState& st,
                                  const std::string& cwd) {
  AdminJobResult r;
  if (!admin_notebook_has_path(st, spawn.arg)) {
    r.error = "edit: path no anclado en notebook";
    r.summary = r.error;
    return r;
  }
  std::string abs;
  std::string rel;
  std::string perr;
  if (!cwd.empty() && !admin_resolve_in_workspace(cwd, spawn.arg, &abs, &rel, &perr)) {
    r.error = perr.empty() ? ("edit fuera del workspace: " + spawn.arg) : perr;
    r.summary = r.error;
    return r;
  }
  SearchReplaceHunk hunk;
  hunk.path = spawn.arg;
  hunk.search = spawn.search;
  hunk.replace = spawn.replace;
  const ApplyHunkResult fr = apply_hunk_to_workspace_file(cwd, hunk, true);
  if (!fr.ok) {
    r.error = fr.error.empty() ? "edit falló" : fr.error;
    r.summary = r.error;
    return r;
  }
  r.ok = true;
  r.paths.push_back(spawn.arg);
  r.summary = "edit ok " + spawn.arg;
  r.facts.push_back("edit:" + spawn.arg);
  return r;
}

namespace {

std::string shell_quote_simple(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

std::string url_encode_query(const std::string& q) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(q.size() * 2);
  for (unsigned char c : q) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else if (c == ' ') {
      out.push_back('+');
    } else {
      out.push_back('%');
      out.push_back(hex[(c >> 4) & 0xF]);
      out.push_back(hex[c & 0xF]);
    }
  }
  return out;
}

bool http_get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers,
              int max_secs, std::string* body, std::string* err) {
  if (body == nullptr) {
    return false;
  }
  std::ostringstream cmd;
  cmd << "curl -sS --max-time " << max_secs << " -L " << shell_quote_simple(url);
  for (const auto& h : headers) {
    cmd << " -H " << shell_quote_simple(h.first + ": " + h.second);
  }
  FILE* pipe = ::popen(cmd.str().c_str(), "r");
  if (pipe == nullptr) {
    if (err) {
      *err = "curl popen falló";
    }
    return false;
  }
  std::ostringstream raw;
  std::array<char, 1024> buf{};
  while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
    raw << buf.data();
    if (raw.str().size() > 400000) {
      break;
    }
  }
  const int status = ::pclose(pipe);
  *body = raw.str();
  if (status != 0) {
    if (err) {
      *err = "curl exit!=0";
    }
    return false;
  }
  return true;
}

std::string html_unescape_basic(std::string s) {
  auto repl = [&](const char* from, const char* to) {
    for (;;) {
      const auto p = s.find(from);
      if (p == std::string::npos) {
        break;
      }
      s.replace(p, std::strlen(from), to);
    }
  };
  repl("&amp;", "&");
  repl("&lt;", "<");
  repl("&gt;", ">");
  repl("&quot;", "\"");
  repl("&#x27;", "'");
  repl("&#39;", "'");
  return s;
}

void push_web_hit(AdminJobResult* r, int idx, const std::string& url, const std::string& title,
                  const std::string& snippet) {
  if (r == nullptr || url.empty()) {
    return;
  }
  if (std::find(r->paths.begin(), r->paths.end(), url) == r->paths.end()) {
    r->paths.push_back(url);
  }
  std::ostringstream f;
  f << "web:" << idx << " url=" << url;
  if (!title.empty()) {
    f << " title=" << title;
  }
  std::string fact = f.str();
  utf8_resize(&fact, static_cast<std::size_t>(kAdminNotebookFactChars));
  r->facts.push_back(fact);
  if (!snippet.empty() && static_cast<int>(r->facts.size()) < 10) {
    std::string sn = "web:" + std::to_string(idx) + " snip=" + snippet;
    utf8_resize(&sn, static_cast<std::size_t>(kAdminNotebookFactChars));
    r->facts.push_back(std::move(sn));
  }
}

}  // namespace

AdminJobResult admin_run_web_search_stub(const std::string& query) {
  AdminJobResult r;
  const std::string q = trim_copy(query);
  if (q.empty()) {
    r.error = "web sin query";
    r.summary = r.error;
    return r;
  }
  r.ok = true;
  r.summary = "web stub q=" + q;
  const std::string enc = url_encode_query(q);
  push_web_hit(&r, 1, "https://duckduckgo.com/?q=" + enc, "DuckDuckGo: " + q,
               "Resultado stub local (TUIDE_WEB_SEARCH_STUB). Sustituye por live con red/API key.");
  push_web_hit(&r, 2, "https://en.wikipedia.org/w/index.php?search=" + enc, "Wikipedia search: " + q,
               "Pista stub para encadenar investigación.");
  r.log_tail = r.summary + "\n" + (r.facts.empty() ? "" : r.facts.front());
  r.raw_bytes = static_cast<int>(r.log_tail.size());
  r.facts.insert(r.facts.begin(), "web:provider=stub");
  return r;
}

AdminJobResult admin_run_web_search(const std::string& query) {
  const std::string q = trim_copy(query);
  if (q.empty()) {
    AdminJobResult r;
    r.error = "web sin query";
    r.summary = r.error;
    return r;
  }

  const char* stub_env = std::getenv("TUIDE_WEB_SEARCH_STUB");
  if (stub_env != nullptr && stub_env[0] == '1') {
    return admin_run_web_search_stub(q);
  }

  const char* key_env = std::getenv("TUIDE_WEB_SEARCH_API_KEY");
  if (key_env == nullptr || key_env[0] == '\0') {
    key_env = std::getenv("BRAVE_API_KEY");
  }
  const std::string api_key = key_env != nullptr ? std::string(key_env) : std::string{};

  AdminJobResult r;
  std::string body;
  std::string herr;

  if (!api_key.empty()) {
    const std::string url =
        "https://api.search.brave.com/res/v1/web/search?q=" + url_encode_query(q) + "&count=5";
    if (http_get(url, {{"Accept", "application/json"}, {"X-Subscription-Token", api_key}}, 20, &body,
                 &herr)) {
      try {
        const auto j = nlohmann::json::parse(body);
        int idx = 0;
        if (j.contains("web") && j["web"].contains("results") && j["web"]["results"].is_array()) {
          for (const auto& hit : j["web"]["results"]) {
            if (idx >= 5) {
              break;
            }
            const std::string u = hit.value("url", "");
            const std::string title = hit.value("title", "");
            const std::string desc = hit.value("description", "");
            if (!u.empty()) {
              ++idx;
              push_web_hit(&r, idx, u, title, desc);
            }
          }
        }
        if (idx > 0) {
          r.ok = true;
          r.summary = "web brave hits=" + std::to_string(idx);
          r.facts.insert(r.facts.begin(), "web:provider=brave");
          bool trunc = false;
          int raw = 0;
          r.log_tail = admin_clip_output(body, &trunc, &raw);
          r.truncated = trunc;
          r.raw_bytes = raw;
          return r;
        }
      } catch (...) {
        // fall through to DDG / stub
      }
    }
  }

  // DuckDuckGo HTML (sin clave). Parseo mínimo de uddg= links.
  {
    const std::string url = "https://html.duckduckgo.com/html/?q=" + url_encode_query(q);
    body.clear();
    herr.clear();
    if (http_get(url, {{"User-Agent", "TUIDE-admin/1.0"}}, 20, &body, &herr)) {
      int idx = 0;
      std::size_t pos = 0;
      while (idx < 5 && pos < body.size()) {
        const auto a = body.find("uddg=", pos);
        if (a == std::string::npos) {
          break;
        }
        auto start = a + 5;
        auto end = body.find('&', start);
        auto end2 = body.find('"', start);
        if (end2 != std::string::npos && (end == std::string::npos || end2 < end)) {
          end = end2;
        }
        if (end == std::string::npos) {
          break;
        }
        std::string enc = body.substr(start, end - start);
        // crude decode %XX and +
        std::string decoded;
        for (std::size_t i = 0; i < enc.size(); ++i) {
          if (enc[i] == '+' ) {
            decoded.push_back(' ');
          } else if (enc[i] == '%' && i + 2 < enc.size()) {
            auto hex = enc.substr(i + 1, 2);
            char* ep = nullptr;
            const long v = std::strtol(hex.c_str(), &ep, 16);
            if (ep && *ep == '\0') {
              decoded.push_back(static_cast<char>(v));
              i += 2;
            } else {
              decoded.push_back(enc[i]);
            }
          } else {
            decoded.push_back(enc[i]);
          }
        }
        std::string title;
        const auto t0 = body.find("result__a", end);
        if (t0 != std::string::npos && t0 < end + 400) {
          const auto gt = body.find('>', t0);
          const auto lt = body.find("</a>", gt);
          if (gt != std::string::npos && lt != std::string::npos && lt > gt) {
            title = html_unescape_basic(body.substr(gt + 1, lt - gt - 1));
            // strip tags
            for (;;) {
              const auto s = title.find('<');
              if (s == std::string::npos) {
                break;
              }
              const auto e = title.find('>', s);
              if (e == std::string::npos) {
                break;
              }
              title.erase(s, e - s + 1);
            }
          }
        }
        if (!decoded.empty() && decoded.rfind("http", 0) == 0) {
          ++idx;
          push_web_hit(&r, idx, decoded, title, "");
        }
        pos = end + 1;
      }
      if (idx > 0) {
        r.ok = true;
        r.summary = "web ddg hits=" + std::to_string(idx);
        r.facts.insert(r.facts.begin(), "web:provider=ddg");
        bool trunc = false;
        int raw = 0;
        r.log_tail = admin_clip_output(body, &trunc, &raw);
        r.truncated = trunc;
        r.raw_bytes = raw;
        return r;
      }
    }
  }

  // Fallback determinista (offline / bloqueo red).
  r = admin_run_web_search_stub(q);
  r.summary += " (fallback stub; live vacío/falló)";
  return r;
}

bool admin_url_fetch_allowed(const std::string& url) {
  const std::string u = trim_copy(url);
  if (u.size() < 8 || u.size() > 2000) {
    return false;
  }
  const std::string low = ascii_lower(u);
  if (low.rfind("https://", 0) != 0 && low.rfind("http://", 0) != 0) {
    return false;
  }
  // Host between scheme and next / ? #
  std::size_t host_start = low.find("://");
  if (host_start == std::string::npos) {
    return false;
  }
  host_start += 3;
  std::size_t host_end = low.find_first_of("/?#", host_start);
  if (host_end == std::string::npos) {
    host_end = low.size();
  }
  std::string host = low.substr(host_start, host_end - host_start);
  // strip userinfo
  const auto at = host.rfind('@');
  if (at != std::string::npos) {
    host = host.substr(at + 1);
  }
  // strip port
  const auto col = host.find(':');
  if (col != std::string::npos) {
    host = host.substr(0, col);
  }
  if (host.empty() || host == "localhost" || host == "127.0.0.1" || host == "::1" ||
      host == "0.0.0.0" || host.rfind("127.", 0) == 0 || host.rfind("10.", 0) == 0 ||
      host.rfind("192.168.", 0) == 0 || host.rfind("169.254.", 0) == 0) {
    return false;
  }
  if (host.rfind("172.", 0) == 0) {
    // 172.16.0.0 – 172.31.255.255
    int second = -1;
    try {
      const auto dot = host.find('.', 4);
      if (dot != std::string::npos) {
        second = std::stoi(host.substr(4, dot - 4));
      }
    } catch (...) {
      second = -1;
    }
    if (second >= 16 && second <= 31) {
      return false;
    }
  }
  return true;
}

namespace {

std::string strip_html_basic(std::string html) {
  // Drop script/style blocks coarsely.
  auto drop_block = [&](const char* open, const char* close) {
    for (;;) {
      const auto a = ascii_lower(html).find(open);
      if (a == std::string::npos) {
        break;
      }
      const auto b = ascii_lower(html).find(close, a);
      if (b == std::string::npos) {
        html.erase(a);
        break;
      }
      html.erase(a, b + std::strlen(close) - a);
    }
  };
  drop_block("<script", "</script>");
  drop_block("<style", "</style>");
  std::string out;
  out.reserve(html.size());
  bool in_tag = false;
  for (char c : html) {
    if (c == '<') {
      in_tag = true;
      continue;
    }
    if (c == '>') {
      in_tag = false;
      out.push_back(' ');
      continue;
    }
    if (!in_tag) {
      out.push_back(c);
    }
  }
  // Collapse whitespace
  std::string flat;
  bool sp = false;
  for (char c : out) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!sp) {
        flat.push_back(' ');
        sp = true;
      }
    } else {
      flat.push_back(c);
      sp = false;
    }
  }
  return trim_copy(flat);
}

}  // namespace

AdminJobResult admin_run_web_fetch_stub(const std::string& url) {
  AdminJobResult r;
  const std::string u = trim_copy(url);
  r.ok = true;
  r.paths.push_back(u);
  r.summary = "web_fetch stub " + u;
  r.facts.push_back("fetch:provider=stub");
  r.facts.push_back("fetch:url=" + u);
  r.facts.push_back(
      "fetch:body Stub HTML body for anchored URL. Contiene marcador TUIDE_WEB_FETCH_STUB y texto "
      "útil para cerrar.");
  r.log_tail = "<html><body><h1>Stub fetch</h1><p>TUIDE_WEB_FETCH_STUB url=" + u +
               "</p><p>nlohmann json releases documentation sample.</p></body></html>";
  r.raw_bytes = static_cast<int>(r.log_tail.size());
  return r;
}

AdminJobResult admin_run_web_fetch(const std::string& url, const AdminState& st) {
  AdminJobResult r;
  const std::string u = trim_copy(url);
  if (!admin_url_fetch_allowed(u)) {
    r.error = "web_fetch: URL no permitida";
    r.summary = r.error;
    return r;
  }
  if (!admin_notebook_has_path(st, u)) {
    r.error = "web_fetch: URL no anclada en notebook";
    r.summary = r.error;
    return r;
  }

  const char* stub_env = std::getenv("TUIDE_WEB_FETCH_STUB");
  if (stub_env != nullptr && stub_env[0] == '1') {
    return admin_run_web_fetch_stub(u);
  }
  // Battery often stubs search; keep fetch deterministic if search was stubbed.
  const char* search_stub = std::getenv("TUIDE_WEB_SEARCH_STUB");
  if (search_stub != nullptr && search_stub[0] == '1') {
    return admin_run_web_fetch_stub(u);
  }

  std::string body;
  std::string herr;
  if (!http_get(u, {{"User-Agent", "TUIDE-admin/1.0"}, {"Accept", "text/html,text/plain,*/*"}}, 25,
                &body, &herr)) {
    r = admin_run_web_fetch_stub(u);
    r.summary += " (fallback stub; live falló)";
    return r;
  }

  bool trunc = false;
  int raw = 0;
  r.log_tail = admin_clip_output(body, &trunc, &raw);
  r.truncated = trunc;
  r.raw_bytes = raw;
  r.ok = true;
  r.paths.push_back(u);
  r.summary = "web_fetch ok bytes=" + std::to_string(raw) + (trunc ? " truncated=1" : "");
  r.facts.push_back("fetch:provider=live");
  r.facts.push_back("fetch:url=" + u);

  std::string plain = strip_html_basic(body);
  utf8_resize(&plain, static_cast<std::size_t>(kAdminReadMaxChars));
  if (!plain.empty()) {
    // Split into a few fact lines (~200 chars each already capped by notebook fact size)
    std::size_t off = 0;
    int n = 0;
    while (off < plain.size() && n < 4) {
      std::string chunk = plain.substr(off, 180);
      off += chunk.size();
      std::string fact = "fetch:text=" + chunk;
      utf8_resize(&fact, static_cast<std::size_t>(kAdminNotebookFactChars));
      r.facts.push_back(std::move(fact));
      ++n;
    }
  }
  return r;
}

AdminVerifyResult admin_run_verify(AdminState* st, L2Brain& brain, const std::string& thesis,
                                   const std::string& workspace_root, const AdminLoopOpts& opts) {
  AdminVerifyResult out;
  if (st == nullptr) {
    out.veredicto = "dudoso";
    out.blocks = true;
    out.why = "state null";
    out.report = out.why;
    return out;
  }
  if (st->notebook.empty() && st->jobs.empty()) {
    out.ok = true;
    out.veredicto = "sostiene";
    out.why = "sin evidencia; se omite verificador";
    return out;
  }
  // Tope: se OMITE el gate (ni absuelve ni bloquea). Si blocks=true aquí, el piloto
  // nunca puede cerrar — bug de producto (sesión atrapada en dudoso eterno).
  if (st->verify_passes >= kAdminMaxVerifyPasses) {
    out.ok = true;
    out.veredicto = "dudoso";
    out.blocks = false;
    out.why = "tope de verificadores; se omite el gate (cierre permitido)";
    out.report =
        "## Informe del VERIFICADOR\nomitido: tope global\nwhy: " + out.why + "\n";
    return out;
  }

  const auto paths = admin_notebook_paths(*st);
  std::ostringstream path_list;
  for (std::size_t i = 0; i < paths.size() && i < 40; ++i) {
    if (i) {
      path_list << ", ";
    }
    path_list << paths[i];
  }

  auto is_miss = [](const std::string& v) {
    const std::string x = ascii_lower(trim_copy(v));
    return x == "no_encontrado" || x == "parcial" || x == "no_hay" || x == "no_concluyente" ||
           x == "no";
  };

  std::ostringstream misses_ss;
  std::vector<std::pair<int, std::string>> miss_jobs;
  for (const auto& j : st->jobs) {
    if (is_miss(j.veredicto)) {
      miss_jobs.push_back({j.id, j.veredicto});
      misses_ss << "job" << j.id << "=" << j.veredicto << "; ";
    }
  }

  (void)thesis;  // sin narrativa del piloto; solo consulta + anclas + veredictos
  const std::string sys =
      "Eres VERIFICADOR. No explores features nuevas. Te dan la consulta, ANCLAS "
      "(símbolos/paths) y veredictos tipados del notebook. Refuta si no demuestran "
      "el arco A→B del pedido (co-ocurrencia ≠ puente). Un no_encontrado/parcial "
      "no se borra porque otro job encontró otra cosa. Tools: entre|read|cerrar. "
      "read solo paths anclados. Al cerrar: "
      "{\"do\":\"cerrar\",\"veredicto\":\"sostiene|refuta|dudoso\",\"ataques\":[],"
      "\"arco\":{\"de\":\"\",\"a\":\"\"},\"why\":\"…\"}";

  std::ostringstream anchors;
  int n_anch = 0;
  for (const auto& j : st->jobs) {
    for (const auto& s : j.simbolos) {
      if (s.empty() || n_anch >= 16) {
        continue;
      }
      anchors << "- `" << s << "`\n";
      ++n_anch;
    }
  }
  if (n_anch == 0) {
    for (const auto& e : st->notebook) {
      for (const auto& s : e.simbolos) {
        if (s.empty() || n_anch >= 16) {
          continue;
        }
        anchors << "- `" << s << "`\n";
        ++n_anch;
      }
      for (const auto& p : e.paths) {
        if (p.empty() || n_anch >= 16) {
          continue;
        }
        anchors << "- `" << p << "`\n";
        ++n_anch;
      }
    }
  }
  if (n_anch == 0) {
    anchors << "(sin anclas)\n";
  }

  std::ostringstream verdicts;
  verdicts << "## Veredictos del notebook (hechos del hijo; no narrativa)\n";
  verdicts << "Un no_encontrado/parcial en un polo NO se borra porque otro job haya "
              "encontrado otra cosa.\n";
  bool any_job = false;
  for (const auto& j : st->jobs) {
    if (j.tipo != "explore" && !j.tipo.empty()) {
      continue;
    }
    any_job = true;
    verdicts << "- job" << j.id << ": veredicto=" << (j.veredicto.empty() ? "-" : j.veredicto)
             << " | " << j.summary.substr(0, 140) << "\n";
  }
  if (!any_job) {
    for (const auto& j : st->jobs) {
      verdicts << "- job" << j.id << " (" << j.tipo
               << "): veredicto=" << (j.veredicto.empty() ? "-" : j.veredicto) << "\n";
    }
  }

  std::ostringstream user;
  user << "## Consulta del usuario\n" << st->consulta << "\n\n";
  user << verdicts.str() << "\n";
  user << "## Anclas (sin narrativa del explorador)\n" << anchors.str() << "\n";
  user << "## Paths legibles\n" << path_list.str() << "\n\n";
  user << "N=" << kAdminVerifyMaxSteps
       << ". Ataca si estas anclas demuestran el arco de la consulta; "
          "si solo co-ocurren, refuta/dudoso. Elige UNA acción JSON "
          "(entre|read|cerrar).\n";

  std::string conversation_user = user.str();
  int entres = 0;
  int reads = 0;
  bool counterask_used = false;
  const int budget = kAdminVerifyMaxSteps + (miss_jobs.empty() ? 0 : 1);

  auto path_ok = [&](const std::string& p) {
    if (p.empty()) {
      return false;
    }
    for (const auto& a : paths) {
      if (p == a || p.find(a) != std::string::npos || a.find(p) != std::string::npos) {
        return true;
      }
    }
    return false;
  };

  auto finish_with_optional_refute = [&](const std::string& verd, const std::string& why,
                                          const nlohmann::json& j) {
    out.ok = true;
    out.veredicto = verd;
    out.why = why;
    out.blocks = (verd == "refuta" || verd == "dudoso");

    // F: pase refutador LLM si aún sostiene (sin rewrite heurístico de dominio).
    if (verd == "sostiene") {
      ui_note(opts, "  · pasada adversaria: ¿confunde con un mecanismo vecino?");
      const std::string refute_sys =
          "Eres REFUTADOR adversarial. NO explores el repo. Te dan consulta, "
          "cobertura/veredicto del verificador y anclas. ÚNICA misión: tumbar "
          "si confunde un mecanismo VECINO con el pedido LITERAL. "
          "Responde UN JSON: "
          "{\"veredicto\":\"sostiene|refuta|dudoso\",\"ataques\":[{\"id\":\"e1\","
          "\"tumbado\":true|false,\"why\":\"…\"}],\"why\":\"…\"}";
      std::ostringstream ru;
      ru << "## Consulta del usuario\n" << st->consulta << "\n\n";
      ru << "## Veredicto del verificador\n" << verd << "\nwhy: " << why << "\n";
      if (j.contains("cobertura")) {
        ru << "cobertura: "
           << j["cobertura"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)
           << "\n";
      }
      ru << "\n## Anclas\n" << anchors.str() << "\nIntenta tumbar. UN JSON.\n";
      L2BrainRequest rreq;
      rreq.system_prompt = refute_sys;
      rreq.user_prompt = ru.str();
      rreq.phase = "admin_verify_refute";
      rreq.max_tokens = opts.settings.max_tokens > 0 ? opts.settings.max_tokens : 600;
      rreq.n_ctx =
          opts.settings.n_ctx_remote > 0 ? opts.settings.n_ctx_remote : opts.settings.n_ctx;
      rreq.temperature = 0.1f;
      rreq.enable_thinking = false;
      L2BrainResult rr = brain.propose(rreq, opts.cancel);
      if (rr.ok) {
        const std::string rraw = rr.text.empty() ? rr.raw : rr.text;
        std::string rblob = rraw;
        const auto rb = rblob.find('{');
        if (rb != std::string::npos) {
          rblob = rblob.substr(rb);
        }
        try {
          nlohmann::json rj = nlohmann::json::parse(rblob);
          std::string rv = ascii_lower(trim_copy(json_str(rj, "veredicto")));
          if (rv != "sostiene" && rv != "refuta" && rv != "dudoso") {
            rv = "dudoso";
          }
          bool any_tumbado = false;
          if (rj.contains("ataques") && rj["ataques"].is_array()) {
            for (const auto& a : rj["ataques"]) {
              if (a.is_object() && a.value("tumbado", false)) {
                any_tumbado = true;
              }
            }
          }
          if (any_tumbado && rv == "sostiene") {
            rv = "refuta";
          }
          if (rv == "refuta" || rv == "dudoso") {
            out.veredicto = rv;
            out.blocks = true;
            const std::string rwhy = trim_copy(json_str(rj, "why"));
            if (rwhy.size() >= 4) {
              out.why = "[refutador] " + rwhy;
            }
          }
        } catch (...) {
          // si el refutador falla al parsear, se mantiene el veredicto del primero
        }
      }
    }

    std::ostringstream rep;
    rep << "## Informe del VERIFICADOR\nveredicto=" << out.veredicto << "\nwhy: " << out.why
        << "\n";
    if (j.contains("ataques") && j["ataques"].is_array()) {
      rep << "ataques: "
          << j["ataques"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
    }
    if (j.contains("arco") && j["arco"].is_object()) {
      rep << "arco: "
          << j["arco"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
    }
    if (out.blocks) {
      rep << "Salida bloqueada — vuelves al menú del piloto. "
             "Decide: spawn explore / seguir_explorando (hueco del ataque), "
             "reintentar editar/cerrar solo si rebatiste, o ask_user.\n";
    }
    out.report = rep.str();
  };

  ui_note(opts, "→ Verificando la explicación…");

  for (int step = 0; step < budget; ++step) {
    const bool last_wave = (step >= budget - 1);
    if (last_wave) {
      conversation_user +=
          "\n\n## Usuario\nÚLTIMA OLA — cierra YA. Emite solo "
          "{\"do\":\"cerrar\",\"veredicto\":\"…\",\"why\":\"…\"}. "
          "Si falta puente: refuta o dudoso. Tools prohibidas.\n";
    }
    L2BrainRequest req;
    req.system_prompt = sys;
    req.user_prompt = conversation_user;
    req.phase = "admin_verify";
    req.max_tokens = opts.settings.max_tokens > 0 ? opts.settings.max_tokens : 700;
    req.n_ctx = opts.settings.n_ctx_remote > 0 ? opts.settings.n_ctx_remote : opts.settings.n_ctx;
    req.temperature = 0.1f;
    req.enable_thinking = false;
    L2BrainResult br = brain.propose(req, opts.cancel);
    if (!br.ok) {
      out.veredicto = "dudoso";
      out.blocks = true;
      out.why = br.error.empty() ? "verify brain falló" : br.error;
      out.report = out.why;
      return out;
    }
    const std::string raw = br.text.empty() ? br.raw : br.text;
    std::string blob = raw;
    const auto brace = blob.find('{');
    if (brace != std::string::npos) {
      blob = blob.substr(brace);
    }
    nlohmann::json j;
    try {
      j = nlohmann::json::parse(blob);
    } catch (...) {
      conversation_user +=
          "\n\n## Asistente\n" + raw.substr(0, 800) +
          "\n\n## Usuario\nJSON inválido. Emite entre|read|cerrar.\n";
      continue;
    }
    const std::string do_kind = ascii_lower(trim_copy(json_str(j, "do")));
    ++out.steps;
    if (do_kind == "cerrar") {
      std::string verd = ascii_lower(trim_copy(json_str(j, "veredicto")));
      if (verd != "sostiene" && verd != "refuta" && verd != "dudoso") {
        verd = "dudoso";
      }
      const std::string why = trim_copy(json_str(j, "why"));
      if (why.size() < 4) {
        conversation_user += "\n\n## Usuario\ncerrar exige why. Reemite.\n";
        continue;
      }
      // B: contra-pregunta si sostiene con miss tipados (LLM re-cierra; no rewrite).
      if (verd == "sostiene" && !miss_jobs.empty() && !counterask_used) {
        counterask_used = true;
        conversation_user +=
            "\n\n## Asistente\n" + raw.substr(0, 800) +
            "\n\n## Usuario\n## Contra-pregunta (hechos del notebook)\n"
            "Hay miss/parcial: " +
            misses_ss.str() +
            "\nO bien rebajas cobertura/veredicto (dudoso/refuta), "
            "o explicas en why por qué ese miss NO toca el pedido. "
            "Reemite SOLO un cerrar.\n";
        ui_note(opts, "  · el verificador pide aclarar hallazgos incompletos");
        continue;
      }
      finish_with_optional_refute(verd, why, j);
      return out;
    }
    if (do_kind == "entre") {
      if (entres >= 3) {
        conversation_user += "\n\n## Usuario\nTope de entre. Cierra.\n";
        continue;
      }
      ++entres;
      const std::string a = trim_copy(json_str(j, "from"));
      const std::string b = trim_copy(json_str(j, "to"));
      AdminJobResult ha = admin_run_search_rg(a, workspace_root);
      AdminJobResult hb = admin_run_search_rg(b, workspace_root);
      ui_note(opts, "  · cruzar «" + ui_clip(a, 40) + "» ↔ «" + ui_clip(b, 40) + "»");
      std::ostringstream tool;
      tool << "## Resultado entre `" << a << "` → `" << b << "`\n";
      tool << "A summary:\n" << ha.summary.substr(0, 1500) << "\n";
      tool << "B summary:\n" << hb.summary.substr(0, 1500) << "\n";
      conversation_user += "\n\n## Asistente\n" + raw.substr(0, 600) + "\n\n## Usuario\n" +
                           tool.str() + "\nSiguiente ola o cerrar.\n";
      continue;
    }
    if (do_kind == "read") {
      if (reads >= 2) {
        conversation_user += "\n\n## Usuario\nTope de read. Cierra.\n";
        continue;
      }
      std::string path = trim_copy(json_str(j, "path"));
      const auto colon = path.find(':');
      if (colon != std::string::npos) {
        path = path.substr(0, colon);
      }
      if (!path_ok(path)) {
        conversation_user +=
            "\n\n## Usuario\nread rechazado: path no anclado. Solo notebook paths.\n";
        continue;
      }
      ++reads;
      AdminJobResult rr = admin_run_read_file(path, workspace_root);
      ui_note(opts, "  · verificador lee `" + path + "`" +
                        (rr.ok ? "" : " — falló"));
      conversation_user += "\n\n## Asistente\n" + raw.substr(0, 400) +
                           "\n\n## Usuario\n## Resultado read\n" + rr.summary.substr(0, 2500) +
                           "\nSiguiente ola o cerrar.\n";
      continue;
    }
    conversation_user += "\n\n## Usuario\ndo inválido; usa entre|read|cerrar.\n";
  }

  out.ok = true;
  out.veredicto = "dudoso";
  out.blocks = true;
  out.why = "tope de pasos sin cerrar";
  out.report =
      "## Informe del VERIFICADOR\nveredicto=dudoso\nwhy: tope sin cerrar\nSalida bloqueada.\n";
  return out;
}


bool admin_apply(AdminState* st, const AdminOla& ola, const AdminOps& ops, std::string* err) {
  if (st == nullptr) {
    if (err) {
      *err = "state null";
    }
    return false;
  }
  if (ola.do_kind == AdminDo::Cerrar) {
    st->done = true;
    st->clarify = false;
    st->awaiting_edit_confirm = false;
    st->verify_reject_pending = false;
    st->reply = ola.reply;
    st->last_error.clear();
    // Archiva episodio para follow-ups (“el fix ahí”) sin perder notebook.
    AdminEpisode ep;
    ep.consulta = st->consulta;
    ep.reply = ola.reply;
    utf8_resize(&ep.reply, static_cast<std::size_t>(kAdminEpisodeReplyChars));
    if (!ep.consulta.empty() || !ep.reply.empty()) {
      st->episodes.push_back(std::move(ep));
      while (static_cast<int>(st->episodes.size()) > kAdminMaxEpisodes) {
        st->episodes.erase(st->episodes.begin());
      }
    }
    return true;
  }
  if (ola.do_kind == AdminDo::AskUser) {
    // Pausa el lazo; no cierra la sesión (el usuario responde en el panel AI).
    st->done = false;
    st->clarify = true;
    st->awaiting_edit_confirm = false;
    st->verify_reject_pending = false;
    st->reply = ola.reply;
    st->pending_question = ola.reply;
    st->last_error.clear();
    return true;
  }
  if (ola.do_kind == AdminDo::Editar) {
    st->awaiting_edit_confirm = true;
    st->verify_reject_pending = false;
    st->last_error.clear();
    return true;
  }
  if (ola.do_kind == AdminDo::ConfirmarEditar) {
    st->awaiting_edit_confirm = false;
    st->edit_confirmed = true;
    st->verify_reject_pending = false;
    st->edit_cubre = ola.cubre;
    st->edit_falta = ola.falta;
    st->last_error.clear();
    return true;
  }

  // SeguirExplorando se aplica como spawn explore con el brief del hueco.
  AdminOla effective = ola;
  if (ola.do_kind == AdminDo::SeguirExplorando) {
    st->awaiting_edit_confirm = false;
    st->verify_reject_pending = false;
    effective.do_kind = AdminDo::Spawn;
    effective.spawn.tipo = AdminSpawnTipo::Explore;
    if (effective.spawn.brief.empty()) {
      if (err) {
        *err = "seguir_explorando sin brief";
      }
      return false;
    }
  }

  if (effective.do_kind != AdminDo::Spawn) {
    if (err) {
      *err = "do no aplicable";
    }
    return false;
  }
  st->verify_reject_pending = false;

  AdminJobResult jr;
  const char* tipo = admin_spawn_tipo_name(effective.spawn.tipo);
  auto run_or = [&](const std::function<AdminJobResult(const AdminSpawn&)>& fn,
                    const std::function<AdminJobResult()>& fallback) {
    if (fn) {
      return fn(effective.spawn);
    }
    return fallback();
  };

  switch (effective.spawn.tipo) {
    case AdminSpawnTipo::Explore:
      jr = run_or(ops.run_explore, [&] { return admin_explore_stub(effective.spawn); });
      break;
    case AdminSpawnTipo::Build:
      if (!ops.run_build) {
        if (err) {
          *err = "run_build no cableado";
        }
        return false;
      }
      jr = ops.run_build(effective.spawn);
      break;
    case AdminSpawnTipo::Git:
      if (!ops.run_git) {
        if (err) {
          *err = "run_git no cableado";
        }
        return false;
      }
      jr = ops.run_git(effective.spawn);
      break;
    case AdminSpawnTipo::Shell:
      jr = run_or(ops.run_shell, [&] {
        return admin_run_shell_safe(effective.spawn.arg, "");
      });
      break;
    case AdminSpawnTipo::Search:
      jr = run_or(ops.run_search, [&] { return admin_run_search_rg(effective.spawn.arg, ""); });
      break;
    case AdminSpawnTipo::Read:
      jr = run_or(ops.run_read, [&] { return admin_run_read_file(effective.spawn.arg, ""); });
      break;
    case AdminSpawnTipo::Diagnostics:
      jr = run_or(ops.run_diagnostics, [&] { return admin_run_diagnostics_stub(effective.spawn); });
      break;
    case AdminSpawnTipo::Test:
      jr = run_or(ops.run_test, [&] { return admin_run_test_stub(effective.spawn); });
      break;
    case AdminSpawnTipo::Edit:
      jr = run_or(ops.run_edit, [&] {
        AdminJobResult fail;
        fail.error = "edit no cableado";
        fail.summary = fail.error;
        return fail;
      });
      break;
    case AdminSpawnTipo::Web:
      jr = run_or(ops.run_web, [&] { return admin_run_web_search(effective.spawn.arg); });
      break;
    case AdminSpawnTipo::WebFetch:
      jr = run_or(ops.run_web_fetch, [&] {
        return admin_run_web_fetch(effective.spawn.arg, *st);
      });
      break;
    default:
      if (err) {
        *err = "tipo de spawn inválido";
      }
      return false;
  }
  (void)tipo;

  AdminJob job;
  job.id = static_cast<int>(st->jobs.size()) + 1;
  job.tipo = admin_spawn_tipo_name(effective.spawn.tipo);
  job.ok = jr.ok;
  job.summary = jr.summary.empty() ? jr.error : jr.summary;
  utf8_resize(&job.summary, static_cast<std::size_t>(kAdminSummaryChars));
  job.veredicto = jr.veredicto;
  job.simbolos = jr.simbolos;
  job.evidencia = jr.evidencia;
  if (jr.raw_bytes > 0 || jr.truncated) {
    job.log_tail = jr.log_tail;
    job.truncated = jr.truncated;
    job.raw_bytes = jr.raw_bytes;
  } else if (!jr.log_tail.empty()) {
    bool trunc = false;
    int raw = 0;
    job.log_tail = admin_clip_output(jr.log_tail, &trunc, &raw);
    job.truncated = trunc;
    job.raw_bytes = raw;
  }
  if (job.truncated && job.summary.find("truncated") == std::string::npos) {
    job.summary += " truncated=1";
  }
  if (!jr.ok && !jr.error.empty() && job.summary.find(jr.error) == std::string::npos) {
    if (!job.summary.empty()) {
      job.summary += "; ";
    }
    job.summary += jr.error;
  }
  admin_notebook_append(st, job, jr);
  st->jobs.push_back(std::move(job));
  ++st->spawns;
  st->last_error.clear();
  return true;
}

std::string admin_system_prompt() {
  // Un solo piloto de producto (admin_v1). Wave/L2-auto son backends o legado;
  // explore = hijo de caza (grep+read), no peeks del piloto.
  return R"(Eres el PILOTO de TIDE (el IDE). El usuario tiene un WORKSPACE abierto (un proyecto).
Decides el siguiente gesto. NO lees código tú: para localizar mecanismos usas spawn explore
(un hijo); search/read son atajos. Acumulas evidencias en el NOTEBOOK.
Cada turno UN JSON (sin prosa fuera del JSON):
{"action":"admin_v1","do":"spawn|cerrar|editar|confirmar_editar|seguir_explorando|ask_user","why":"…",
 "spawn":{"tipo":"…","brief":"…","arg":"…","search":"…","replace":"…"},
 "cubre":"…","falta":"…","reply":"…"}
IMPORTANTE: "action" es SIEMPRE la literal "admin_v1". El gesto va en "do" (no en "action").
Perímetro: solo el WORKSPACE_ROOT del prompt (proyecto abierto). search/read/edit/shell/explore
no pueden salir de esa ruta; el runtime lo bloquea. Si ahora el root es el repo de TIDE,
analizas TIDE; si mañana es otro proyecto, analizas ese — no inventes otros IDE ni rutas.
Saludo sin tarea → do=cerrar con reply amable (preferido) o ask_user breve.
ask_user solo si falta UN dato concreto para una tarea de código ya planteada.
Si el diálogo ya trae la tarea (respuesta del usuario), actúa (explore/search) — no re-preguntes.

Tipos spawn:
- explore: brief = UN solo fenómeno (una pregunta que un hijo puede cerrar). Si el pedido
  del usuario mezcla varios (p.ej. consola y margen, o A y B), parte: un explore por polo.
  No metas "vincular/conectar/ambos" en el mismo brief. Respeta el tope explore del presupuesto.
- search: arg=query (rg en el repo)
- read: arg=path o path:Symbol (clip)
- shell: allowlist ls/find/rg/head… (opcional | head -N). Para localizar código usa search/explore,
  no inventes stack (.ts/.js) — mira WORKSPACE_ROOT (aquí suele ser C++ en src/).
- git: arg=status|log|diff|show|branches
- build: arg=compile
- test: arg=test (u otro target whitelist)
- diagnostics: arg=path o vacío
- web: arg=query de internet. web_fetch: URL ya en notebook.
- edit: arg=path ya en notebook/UI; search+replace únicos. Solo tras confirmar_editar.

Salidas:
- cerrar: reply grounded en notebook. Con notebook, el runtime puede lanzar un VERIFICADOR
  adversarial (refuta arcos A→B) antes de aceptar.
- editar: pides pasar a edición; runtime verifica y luego pide CONFIRMACIÓN (no edita aún).
- confirmar_editar: tras ese pedido; obliga cubre + falta (falta puede ser "nada").
- seguir_explorando: tras confirmación, si falta un polo; spawn.brief = hueco.
- ask_user: pregunta al usuario; el runtime pausa y muestra la pregunta en el panel AI.
  La siguiente línea del usuario responde y continúa el lazo (consulta original intacta).
Tras cerrar, el hilo sigue: notebook + episodios anteriores anclan “ahí”/“eso”.
Un spawn por turno.)";
}

std::string admin_user_prompt(const AdminState& st, int max_proposes, int max_spawns,
                              const std::string& workspace_root) {
  std::ostringstream out;
  if (!workspace_root.empty()) {
    out << "## WORKSPACE_ROOT (perímetro FS — único árbol accesible)\n`" << workspace_root
        << "`\nPaths relativos a este root. Prohibido salir con .. o absolutos ajenos.\n\n";
  }
  out << "## Consulta\n" << st.consulta << "\n\n";
  if (!st.episodes.empty()) {
    out << "## Episodios anteriores (mismo hilo; el notebook sigue valiendo)\n";
    const int from = std::max(0, static_cast<int>(st.episodes.size()) - kAdminMaxEpisodes);
    for (int i = from; i < static_cast<int>(st.episodes.size()); ++i) {
      const auto& ep = st.episodes[static_cast<std::size_t>(i)];
      out << "- consulta: " << ep.consulta << "\n";
      if (!ep.reply.empty()) {
        out << "  reply: " << ep.reply << "\n";
      }
    }
    out << "Si el usuario dice \"ahí\"/\"eso\"/\"ese sitio\", ancla a episodios + notebook.\n\n";
  }
  if (!st.clarifies.empty()) {
    out << "## Diálogo con el usuario (ya respondido)\n";
    for (const auto& c : st.clarifies) {
      out << "- Q: " << c.question << "\n  A: " << c.answer << "\n";
    }
    out << "\n";
  }
  // Sin pistas runtime: el piloto elige el gesto (mismo criterio que explorer bruto).
  out << "## Presupuesto\nproposes=" << st.proposes << "/" << max_proposes
      << " spawns=" << st.spawns << "/" << max_spawns << "\n";
  const auto legal = admin_legal_dos(st, max_proposes, max_spawns);
  out << "do legales:";
  for (const auto& d : legal) {
    out << " " << d;
  }
  out << "\n\n";

  if (st.awaiting_edit_confirm) {
    out << "## Confirmación de edición\n"
           "Has pedido editar. Declara cobertura del pedido del usuario vs el NOTEBOOK "
           "(no inventes paths no listados).\n"
           "{\"action\":\"admin_v1\",\"do\":\"confirmar_editar\",\"why\":\"…\","
           "\"cubre\":\"qué del pedido ya cubre el notebook\","
           "\"falta\":\"qué falta (o nada)\",\"reply\":\"…\"}\n"
           "{\"action\":\"admin_v1\",\"do\":\"seguir_explorando\",\"why\":\"…\","
           "\"spawn\":{\"tipo\":\"explore\",\"brief\":\"hueco concreto a cazar\"}}\n"
           "{\"action\":\"admin_v1\",\"do\":\"cerrar\",\"why\":\"…\",\"reply\":\"…\"}\n\n";
  }

  out << "## Sesion UI\n";
  if (st.ui.active_path.empty() && st.ui.git_branch.empty()) {
    out << "(sin UI)\n\n";
  } else {
    if (!st.ui.active_path.empty()) {
      out << "- active_path: `" << st.ui.active_path << "`";
      if (st.ui.cursor_line >= 0) {
        out << " line=" << (st.ui.cursor_line + 1);
      }
      out << "\n";
    }
    if (!st.ui.git_branch.empty()) {
      out << "- branch: `" << st.ui.git_branch << "`\n";
    }
    if (!st.ui.selection.empty()) {
      out << "- selection: " << st.ui.selection << "\n";
    }
    out << "\n";
  }

  out << "## Notebook (evidencias acumuladas — tu plan de facto)\n";
  out << admin_notebook_markdown(st) << "\n";

  out << "## Inbox jobs (reciente)\n";
  if (st.jobs.empty()) {
    out << "(vacío)\n";
  } else {
    const int from = std::max(0, static_cast<int>(st.jobs.size()) - 4);
    for (int i = from; i < static_cast<int>(st.jobs.size()); ++i) {
      const auto& j = st.jobs[static_cast<std::size_t>(i)];
      out << "- #" << j.id << " " << j.tipo << " ok=" << (j.ok ? "1" : "0");
      if (!j.veredicto.empty()) {
        out << " veredicto=" << j.veredicto;
      }
      out << "\n  summary: " << j.summary << "\n";
      if (j.truncated || j.raw_bytes > 0) {
        out << "  harness: bytes=" << j.raw_bytes
            << " truncated=" << (j.truncated ? "1" : "0") << "\n";
      }
      if (!j.log_tail.empty()) {
        out << "  log_tail:\n" << j.log_tail << "\n";
      }
    }
  }
  if (!st.last_error.empty()) {
    out << "\n## Ultimo rechazo\n" << st.last_error << "\n";
  }
  out << "\nResponde SOLO el JSON admin_v1.\n";
  return out.str();
}

AdminLoopResult run_admin_loop(AdminState* st, L2Brain& brain, const AdminOps& ops,
                               const AdminLoopOpts& opts) {
  AdminLoopResult result;
  if (st == nullptr) {
    result.error = "state null";
    return result;
  }
  if (!opts.ui.active_path.empty() || !opts.ui.git_branch.empty() ||
      !opts.ui.selection.empty()) {
    st->ui = opts.ui;
    utf8_resize(&st->ui.selection, static_cast<std::size_t>(kAdminUiSelectionChars));
  }
  const int max_p = opts.max_proposes > 0 ? opts.max_proposes : kAdminMaxProposes;
  const int max_s = opts.max_spawns > 0 ? opts.max_spawns : kAdminMaxSpawns;

  if (st->notebook.empty() && st->jobs.empty() && st->episodes.empty()) {
    ui_note(opts, "→ Investigando tu consulta…");
  } else {
    ui_note(opts, "→ Sigo con la evidencia ya reunida…");
  }
  while (!st->done && !st->clarify && !cancelled(opts)) {
    const std::string sys = admin_system_prompt();
    const std::string user = admin_user_prompt(*st, max_p, max_s, opts.workspace_root);
    L2BrainRequest req;
    req.system_prompt = sys;
    req.user_prompt = user;
    req.phase = "admin";
    req.max_tokens = opts.settings.max_tokens > 0 ? opts.settings.max_tokens : 1024;
    req.n_ctx = opts.settings.n_ctx_remote > 0 ? opts.settings.n_ctx_remote : opts.settings.n_ctx;
    req.temperature = opts.settings.temperature;
    req.enable_thinking = false;

    L2BrainResult br = brain.propose(req, opts.cancel);
    if (cancelled(opts)) {
      result.error = "cancelado";
      break;
    }
    if (!br.ok) {
      result.error = br.error.empty() ? "brain propose falló" : br.error;
      ui_note(opts, "→ Error del modelo: " + result.error);
      break;
    }

    AdminOla ola = admin_parse(br.text.empty() ? br.raw : br.text);
    std::string lerr;
    if (!admin_legal(*st, ola, max_p, max_s, &lerr)) {
      ++st->proposes;
      st->last_error = lerr;
      if (!ola.raw_json.empty()) {
        std::string clip = ola.raw_json;
        utf8_resize(&clip, 240);
        st->last_error += " | visto: " + clip;
      } else {
        std::string clip = br.text.empty() ? br.raw : br.text;
        utf8_resize(&clip, 240);
        if (!clip.empty()) {
          st->last_error += " | visto: " + clip;
        }
      }
      ui_note(opts, "→ " + humanize_admin_reject(lerr));
      if (!opts.workspace_root.empty()) {
        admin_save_state(opts.workspace_root, *st, nullptr);
      }
      if (st->proposes >= max_p) {
        result.error = "tope de proposes con turnos ilegales";
        break;
      }
      continue;
    }
    ++st->proposes;

    // Verificador adversarial antes de aceptar editar/cerrar (si hay notebook).
    // Saltar con brain scripted (baterías/unit) — no consumir el guion.
    const bool skip_verify = (brain.name() == "scripted");
    if (!skip_verify && (ola.do_kind == AdminDo::Editar || ola.do_kind == AdminDo::Cerrar) &&
        (!st->notebook.empty() || !st->jobs.empty()) && !st->awaiting_edit_confirm) {
      if (st->verify_passes >= kAdminMaxVerifyPasses) {
        ui_note(opts,
                "→ Tope de comprobaciones: dejo pasar el cierre sin nuevo veredicto");
        st->verify_reject_pending = false;
      } else {
        AdminVerifyResult vr =
            admin_run_verify(st, brain, ola.why, opts.workspace_root, opts);
        ++st->verify_passes;
        st->last_verify_verdict = vr.veredicto;
        st->last_verify_why = vr.why;
        {
          std::ostringstream line;
          line << "→ Veredicto: " << verdicto_verify_ui(vr.veredicto);
          if (!vr.why.empty()) {
            line << " — " << ui_clip(vr.why, 200);
          }
          if (vr.blocks) {
            line << " (hay que seguir investigando)";
          }
          ui_note(opts, line.str());
        }
        if (vr.blocks) {
          st->awaiting_edit_confirm = false;
          st->verify_reject_pending = true;
          st->last_error = vr.report.empty() ? ("verificador: " + vr.veredicto) : vr.report;
          if (!opts.workspace_root.empty()) {
            admin_save_state(opts.workspace_root, *st, nullptr);
          }
          continue;
        }
        st->verify_reject_pending = false;
      }
    }

    // Inject cwd into FS helpers via temporary ops wrap for search/read defaults.
    AdminOps ops_cwd = ops;
    if (!opts.workspace_root.empty()) {
      if (!ops_cwd.run_search) {
        ops_cwd.run_search = [root = opts.workspace_root](const AdminSpawn& s) {
          return admin_run_search_rg(s.arg, root);
        };
      }
      if (!ops_cwd.run_read) {
        ops_cwd.run_read = [root = opts.workspace_root](const AdminSpawn& s) {
          return admin_run_read_file(s.arg, root);
        };
      }
      if (!ops_cwd.run_shell) {
        ops_cwd.run_shell = [root = opts.workspace_root](const AdminSpawn& s) {
          return admin_run_shell_safe(s.arg, root);
        };
      }
      if (!ops_cwd.run_edit) {
        ops_cwd.run_edit = [root = opts.workspace_root, st](const AdminSpawn& s) {
          return admin_run_edit_file(s, *st, root);
        };
      }
      if (!ops_cwd.run_web) {
        ops_cwd.run_web = [](const AdminSpawn& s) { return admin_run_web_search(s.arg); };
      }
      if (!ops_cwd.run_web_fetch) {
        ops_cwd.run_web_fetch = [st](const AdminSpawn& s) {
          return admin_run_web_fetch(s.arg, *st);
        };
      }
    }

    if (ola.do_kind == AdminDo::Spawn || ola.do_kind == AdminDo::SeguirExplorando) {
      std::ostringstream line;
      if (ola.do_kind == AdminDo::SeguirExplorando) {
        line << "→ Siguiendo el hueco";
      } else {
        line << "→ Piloto: " << spawn_tipo_ui(ola.spawn.tipo);
      }
      if (!ola.spawn.arg.empty()) {
        line << " `" << ui_clip(ola.spawn.arg, 90) << "`";
      } else if (!ola.spawn.brief.empty()) {
        line << " — " << ui_clip(ola.spawn.brief, 140);
      }
      ui_note(opts, line.str());
    } else if (ola.do_kind == AdminDo::Editar) {
      ui_note(opts, "→ Proponiendo edición (pendiente de confirmación)…");
    } else if (ola.do_kind == AdminDo::ConfirmarEditar) {
      ui_note(opts, "→ Edición confirmada");
    }

    if (!admin_apply(st, ola, ops_cwd, &lerr)) {
      st->last_error = lerr;
      ui_note(opts, "→ No se pudo aplicar: " + lerr);
      if (!opts.workspace_root.empty()) {
        admin_save_state(opts.workspace_root, *st, nullptr);
      }
      continue;
    }

    if ((ola.do_kind == AdminDo::Spawn || ola.do_kind == AdminDo::SeguirExplorando) &&
        !st->jobs.empty()) {
      const auto& j = st->jobs.back();
      // Explore ya narra grep/read/conclusión; no repetir el summary técnico.
      if (j.tipo != "explore" && !j.summary.empty()) {
        ui_note(opts, "  · " + ui_clip(j.summary, 220));
      }
    }
    // cerrar / ask_user: el reply se muestra al salir del loop.

    if (!opts.workspace_root.empty()) {
      const fs::path turn_path = fs::path(admin_dir(opts.workspace_root)) / "turns" /
                                 (std::to_string(st->proposes) + ".json");
      write_file(turn_path,
                 nlohmann::json{{"propose", st->proposes},
                                {"do", admin_do_name(ola.do_kind)},
                                {"why", ola.why},
                                {"raw", ola.raw_json}}
                     .dump(2, ' ', false, nlohmann::json::error_handler_t::replace),
                 nullptr);
      admin_save_state(opts.workspace_root, *st, nullptr);
    }
  }

  result.proposes = st->proposes;
  result.spawns = st->spawns;
  result.clarify = st->clarify;
  result.reply = st->clarify && !st->pending_question.empty() ? st->pending_question : st->reply;
  if (st->done || st->clarify) {
    result.ok = true;
    if (st->clarify) {
      ui_note(opts, "→ Necesito una aclaración tuya:");
    } else {
      ui_note(opts, "→ Respuesta:");
    }
  } else if (result.error.empty()) {
    result.error = cancelled(opts) ? "cancelado" : "loop incompleto";
  }
  return result;
}

AdminScriptedBrain::AdminScriptedBrain(std::vector<std::string> script)
    : script_(std::move(script)) {}

std::string AdminScriptedBrain::name() const {
  return "scripted";
}

bool AdminScriptedBrain::ensure_ready(const AiSettings&,
                                      const std::function<void(const std::string&)>&,
                                      std::string*) {
  return true;
}

L2BrainResult AdminScriptedBrain::propose(const L2BrainRequest&, std::atomic<bool>*) {
  L2BrainResult out;
  out.backend = "scripted";
  if (idx_ >= script_.size()) {
    out.error = "script agotado";
    return out;
  }
  out.ok = true;
  out.text = script_[idx_++];
  out.raw = out.text;
  return out;
}

}  // namespace tuide
