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

std::string ascii_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

void utf8_resize(std::string* s, std::size_t max_bytes) {
  if (s == nullptr || s->size() <= max_bytes) {
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
  return (st.clarify && !st.done) || (!st.notebook.empty() && !st.done);
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
  if (!write_file(admin_state_path(workspace_root), admin_state_to_json(st).dump(2), err)) {
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
    out << "- #" << e.job_id << " `" << e.tipo << "` " << e.summary << "\n";
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
  const std::string action = j.value("action", "");
  if (!action.empty() && action != "admin_v1") {
    out.error = "contrato admin_v1 inválido";
    return out;
  }
  const std::string d = ascii_lower(trim_copy(json_str(j, "do")));
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

bool admin_shell_cmd_allowed(const std::string& cmd) {
  const std::string c = trim_copy(cmd);
  if (c.empty() || c.size() > 500) {
    return false;
  }
  if (c == "launch" || c == "compile" || c == "test") {
    return true;
  }
  static const char* kMeta[] = {"`", "$(", "${", ">", "<", "|", ";", "&", "\n", "\r"};
  for (const char* m : kMeta) {
    if (c.find(m) != std::string::npos) {
      return false;
    }
  }
  const std::string low = ascii_lower(c);
  static const char* kBan[] = {"sudo", " rm", "rm ", "\trm", "mv ", "chmod", "chown",
                               "curl", "wget", "ssh ", "scp ", "dd ", "mkfs"};
  for (const char* b : kBan) {
    if (low.find(b) != std::string::npos) {
      return false;
    }
  }
  if (low.rfind("rm ", 0) == 0 || low == "rm") {
    return false;
  }
  std::string first;
  for (char ch : c) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      break;
    }
    first.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  static const char* kOk[] = {"ls",    "find", "pwd",      "echo",     "wc",       "head",
                              "tail",  "cat",  "file",     "du",       "which",    "printf",
                              "basename", "dirname", "realpath", "tree", "test",  "true",
                              "false", "stat", "readlink", "env",      "printenv", "id",
                              "uname", "date", "seq",      "rg",       "grep"};
  for (const char* ok : kOk) {
    if (first == ok) {
      return true;
    }
  }
  return false;
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
    r.error = "shell denegado (allowlist lectura; sin pipes/redirects)";
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
    full = "cd '" + cwd + "' && " + cmd;
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
  // Prefer rg; fallback to grep -R.
  std::string cmd = "rg -n --no-heading -S -m 40 -- " + q;
  // Escape is minimal: reject meta already via shell allow if we route through it.
  // Run via popen with quoted query.
  std::ostringstream oss;
  if (!cwd.empty()) {
    oss << "cd '" << cwd << "' && ";
  }
  oss << "rg -n --no-heading -S -m 40 -- '" << q << "' . 2>/dev/null || "
      << "grep -RIn --exclude-dir=.git --exclude-dir=build -m 40 -e '" << q << "' . 2>/dev/null";
  FILE* pipe = ::popen(oss.str().c_str(), "r");
  if (pipe == nullptr) {
    r.error = "search popen falló";
    r.summary = r.error;
    return r;
  }
  std::string captured;
  std::array<char, 512> buf{};
  while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
    captured += buf.data();
  }
  ::pclose(pipe);
  bool trunc = false;
  int raw = 0;
  r.log_tail = admin_clip_output(captured, &trunc, &raw);
  r.truncated = trunc;
  r.raw_bytes = raw;
  r.ok = true;
  int hits = 0;
  std::istringstream iss(captured);
  std::string line;
  while (std::getline(iss, line) && hits < 12) {
    const auto c1 = line.find(':');
    if (c1 == std::string::npos) {
      continue;
    }
    const std::string path = line.substr(0, c1);
    if (!path.empty() && std::find(r.paths.begin(), r.paths.end(), path) == r.paths.end()) {
      r.paths.push_back(path);
    }
    r.facts.push_back(line.substr(0, std::min<std::size_t>(line.size(), 200)));
    ++hits;
  }
  r.summary = "search hits≈" + std::to_string(hits) + " bytes=" + std::to_string(raw);
  if (hits == 0) {
    r.summary += " (0 hits)";
  }
  return r;
}

AdminJobResult admin_run_read_file(const std::string& target, const std::string& cwd) {
  AdminJobResult r;
  std::string path = trim_copy(target);
  std::string symbol;
  const auto col = path.rfind(':');
  if (col != std::string::npos && col > 0 && path.find('/') != std::string::npos) {
    // path:Symbol or path:line — keep path before last colon if looks like file
    const std::string maybe = path.substr(0, col);
    if (maybe.find('.') != std::string::npos) {
      symbol = path.substr(col + 1);
      path = maybe;
    }
  }
  fs::path fp = path;
  if (!fp.is_absolute() && !cwd.empty()) {
    fp = fs::path(cwd) / path;
  }
  std::string body = read_file(fp);
  if (body.empty()) {
    r.error = "read: no se pudo abrir " + path;
    r.summary = r.error;
    return r;
  }
  if (!symbol.empty() && !std::isdigit(static_cast<unsigned char>(symbol[0]))) {
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
  if (st->verify_passes >= kAdminMaxVerifyPasses) {
    out.ok = true;
    out.veredicto = "sostiene";
    out.why = "tope global de verificadores; se omite";
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

  (void)thesis;  // sin narrativa del piloto; solo consulta + anclas
  const std::string sys =
      "Eres VERIFICADOR. No explores features nuevas. Te dan la consulta y ANCLAS "
      "(símbolos/paths). Refuta si no demuestran el arco A→B del pedido "
      "(co-ocurrencia ≠ puente). Tools: entre|read|cerrar. read solo paths anclados. "
      "Al cerrar: "
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

  std::ostringstream user;
  user << "## Consulta del usuario\n" << st->consulta << "\n\n";
  user << "## Anclas (sin narrativa del explorador)\n" << anchors.str() << "\n";
  user << "## Paths legibles\n" << path_list.str() << "\n\n";
  user << "N=" << kAdminVerifyMaxSteps
       << ". Ataca si estas anclas demuestran el arco de la consulta; "
          "si solo co-ocurren, refuta/dudoso. Elige UNA acción JSON "
          "(entre|read|cerrar).\n";

  std::string conversation_user = user.str();
  int entres = 0;
  int reads = 0;

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

  for (int step = 0; step < kAdminVerifyMaxSteps; ++step) {
    const bool last_wave = (step >= kAdminVerifyMaxSteps - 1);
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
    append_line(opts, "Admin ▸ verify ola " + std::to_string(step + 1));
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
      out.veredicto = verd;
      out.why = trim_copy(json_str(j, "why"));
      if (out.why.size() < 4) {
        conversation_user += "\n\n## Usuario\ncerrar exige why. Reemite.\n";
        continue;
      }
      out.ok = true;
      out.blocks = (verd == "refuta" || verd == "dudoso");
      std::ostringstream rep;
      rep << "## Informe del VERIFICADOR\nveredicto=" << verd << "\nwhy: " << out.why << "\n";
      if (j.contains("ataques") && j["ataques"].is_array()) {
        rep << "ataques: " << j["ataques"].dump() << "\n";
      }
      if (j.contains("arco") && j["arco"].is_object()) {
        rep << "arco: " << j["arco"].dump() << "\n";
      }
      if (out.blocks) {
        rep << "Salida bloqueada — vuelves al menú del piloto. "
               "Decide: spawn explore / seguir_explorando (hueco del ataque), "
               "reintentar editar/cerrar solo si rebatiste, o ask_user.\n";
      }
      out.report = rep.str();
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
  out.report = "## Informe del VERIFICADOR\nveredicto=dudoso\nwhy: tope sin cerrar\nSalida bloqueada.\n";
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
    return true;
  }
  if (ola.do_kind == AdminDo::AskUser) {
    st->done = true;
    st->clarify = true;
    st->awaiting_edit_confirm = false;
    st->verify_reject_pending = false;
    st->reply = ola.reply;
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
  return R"(Eres el PILOTO de TIDE. Has recibido la orden del usuario. Decides el siguiente gesto.
NO lees código tú: para localizar mecanismos usas spawn explore (un hijo); search/read son atajos.
Acumulas evidencias en el NOTEBOOK. Cada turno UN JSON:
{"action":"admin_v1","do":"spawn|cerrar|editar|confirmar_editar|seguir_explorando|ask_user","why":"…",
 "spawn":{"tipo":"…","brief":"…","arg":"…","search":"…","replace":"…"},
 "cubre":"…","falta":"…","reply":"…"}

Tipos spawn:
- explore: brief = UN solo fenómeno (una pregunta que un hijo puede cerrar). Si el pedido
  del usuario mezcla varios (p.ej. consola y margen, o A y B), parte: un explore por polo.
  No metas "vincular/conectar/ambos" en el mismo brief. Respeta el tope explore del presupuesto.
- search: arg=query (rg en el repo)
- read: arg=path o path:Symbol (clip)
- shell: allowlist ls/find/rg/… o launch
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
- ask_user: si la orden es vaga.
Un spawn por turno.)";
}

std::string admin_user_prompt(const AdminState& st, int max_proposes, int max_spawns) {
  std::ostringstream out;
  out << "## Consulta\n" << st.consulta << "\n\n";
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

  append_line(opts, "Admin ▸ arranque (notebook=" + std::to_string(st->notebook.size()) + ")");
  while (!st->done && !cancelled(opts)) {
    const std::string sys = admin_system_prompt();
    const std::string user = admin_user_prompt(*st, max_p, max_s);
    L2BrainRequest req;
    req.system_prompt = sys;
    req.user_prompt = user;
    req.phase = "admin";
    req.max_tokens = opts.settings.max_tokens > 0 ? opts.settings.max_tokens : 1024;
    req.n_ctx = opts.settings.n_ctx_remote > 0 ? opts.settings.n_ctx_remote : opts.settings.n_ctx;
    req.temperature = opts.settings.temperature;
    req.enable_thinking = false;

    append_line(opts, "Admin ▸ propose #" + std::to_string(st->proposes + 1));
    L2BrainResult br = brain.propose(req, opts.cancel);
    if (cancelled(opts)) {
      result.error = "cancelado";
      break;
    }
    if (!br.ok) {
      result.error = br.error.empty() ? "brain propose falló" : br.error;
      append_line(opts, "Admin ▸ brain ✗ " + result.error);
      break;
    }

    AdminOla ola = admin_parse(br.text.empty() ? br.raw : br.text);
    ++st->proposes;
    std::string lerr;
    if (!admin_legal(*st, ola, max_p, max_s, &lerr)) {
      st->last_error = lerr;
      append_line(opts, "Admin ▸ rechazo: " + lerr);
      if (!opts.workspace_root.empty()) {
        admin_save_state(opts.workspace_root, *st, nullptr);
      }
      if (st->proposes >= max_p) {
        result.error = "tope de proposes con turnos ilegales";
        break;
      }
      continue;
    }

    // Verificador adversarial antes de aceptar editar/cerrar (si hay notebook).
    // Saltar con brain scripted (baterías/unit) — no consumir el guion.
    const bool skip_verify = (brain.name() == "scripted");
    if (!skip_verify && (ola.do_kind == AdminDo::Editar || ola.do_kind == AdminDo::Cerrar) &&
        (!st->notebook.empty() || !st->jobs.empty()) && !st->awaiting_edit_confirm) {
      AdminVerifyResult vr =
          admin_run_verify(st, brain, ola.why, opts.workspace_root, opts);
      ++st->verify_passes;
      st->last_verify_verdict = vr.veredicto;
      st->last_verify_why = vr.why;
      append_line(opts, "Admin ▸ verify " + vr.veredicto +
                            (vr.blocks ? " (bloquea)" : " (ok)"));
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

    if (!admin_apply(st, ola, ops_cwd, &lerr)) {
      st->last_error = lerr;
      append_line(opts, "Admin ▸ apply ✗ " + lerr);
      if (!opts.workspace_root.empty()) {
        admin_save_state(opts.workspace_root, *st, nullptr);
      }
      continue;
    }

    append_line(opts, std::string("Admin ▸ ") + admin_do_name(ola.do_kind) +
                          (ola.do_kind == AdminDo::Spawn
                               ? (std::string(" ") + admin_spawn_tipo_name(ola.spawn.tipo))
                               : ""));
    if (ola.do_kind == AdminDo::Spawn && !st->jobs.empty()) {
      const auto& j = st->jobs.back();
      append_line(opts, "Admin ▸ job #" + std::to_string(j.id) + " " + j.summary);
      append_line(opts, "Admin ▸ notebook items=" + std::to_string(st->notebook.size()));
    }

    if (!opts.workspace_root.empty()) {
      const fs::path turn_path = fs::path(admin_dir(opts.workspace_root)) / "turns" /
                                 (std::to_string(st->proposes) + ".json");
      write_file(turn_path,
                 nlohmann::json{{"propose", st->proposes},
                                {"do", admin_do_name(ola.do_kind)},
                                {"why", ola.why},
                                {"raw", ola.raw_json}}
                     .dump(2),
                 nullptr);
      admin_save_state(opts.workspace_root, *st, nullptr);
    }
  }

  result.proposes = st->proposes;
  result.spawns = st->spawns;
  result.clarify = st->clarify;
  result.reply = st->reply;
  if (st->done) {
    result.ok = true;
    append_line(opts, st->clarify ? ("Admin ▸ ask_user: " + st->reply)
                                  : ("Admin ▸ cerrar: " + st->reply));
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
