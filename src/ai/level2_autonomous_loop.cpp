#include "ai/level2_autonomous_loop.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <vector>

#include "ai/ai_trace.hpp"
#include "ai/l2_action.hpp"

namespace tuide {
namespace {

constexpr std::size_t kMaxPromptCharsExplore = 10000;  // fits ai.level2.n_ctx=8192 with system
constexpr std::size_t kMaxPromptCharsEdit = 8000;
constexpr std::size_t kExploreObsBudget = 3500;
constexpr int kExploreMapDetailTop = 5;  // full snippet only for top-N ranked entries in prompt

bool is_map_entry_start_line(const std::string& line) {
  if (line.empty() || line[0] < '0' || line[0] > '9') {
    return false;
  }
  std::size_t i = 0;
  while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
    ++i;
  }
  return i < line.size() && line[i] == '.';
}

std::string first_line_only(const std::string& entry) {
  const auto nl = entry.find('\n');
  std::string line = nl == std::string::npos ? entry : entry.substr(0, nl);
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
    line.pop_back();
  }
  return line;
}

// Prompt-time slim: keep detail for first keep_detail entries; name line for the rest.
// Does not mutate session.md (disk compact still runs after tools).
std::string slim_ranked_map_for_prompt(const std::string& map_body, int keep_detail,
                                       std::size_t max_chars) {
  std::string body = map_body;
  const auto bodies = body.find("\n## Bodies");
  if (bodies != std::string::npos) {
    body = body.substr(0, bodies);
  }

  std::vector<std::string> entries;
  std::string preamble;
  {
    std::istringstream in(body);
    std::string line;
    std::ostringstream pre;
    std::ostringstream cur;
    bool in_entry = false;
    auto flush = [&]() {
      if (!in_entry) {
        return;
      }
      std::string e = cur.str();
      while (!e.empty() && (e.back() == '\n' || e.back() == '\r')) {
        e.pop_back();
      }
      if (!e.empty()) {
        entries.push_back(std::move(e));
      }
      cur.str("");
      cur.clear();
    };
    while (std::getline(in, line)) {
      if (is_map_entry_start_line(line)) {
        if (!in_entry) {
          preamble = pre.str();
        }
        flush();
        in_entry = true;
        cur << line << '\n';
      } else if (in_entry) {
        cur << line << '\n';
      } else {
        pre << line << '\n';
      }
    }
    flush();
    if (!in_entry) {
      preamble = pre.str();
    }
  }

  std::ostringstream out;
  out << preamble;
  if (preamble.find("## Ranked entries") == std::string::npos && !entries.empty()) {
    out << "## Ranked entries\n\n";
  }
  for (std::size_t i = 0; i < entries.size(); ++i) {
    if (static_cast<int>(i) < keep_detail) {
      out << entries[i] << "\n\n";
    } else {
      out << first_line_only(entries[i]) << "\n";
    }
  }
  std::string slim = out.str();
  if (slim.size() > max_chars) {
    slim = slim.substr(0, max_chars) +
           "\n…[map prompt truncado; prioriza score alto / usa tools]…\n";
  }
  return slim;
}

std::string session_prompt_header(const std::string& body, std::size_t map_pos) {
  // Prefer ## Instruction (skip legacy duplicated ## Tool guide in old sessions).
  const std::string instr_mark = "## Instruction";
  const auto instr_pos = body.find(instr_mark);
  if (instr_pos != std::string::npos && instr_pos < map_pos) {
    return body.substr(instr_pos, map_pos - instr_pos);
  }
  // Fallback: short slice before map.
  const std::size_t start = map_pos > 800 ? map_pos - 800 : 0;
  return body.substr(start, map_pos - start);
}

std::string read_file_tail(const std::string& path, std::size_t max_chars) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string body = ss.str();
  if (body.size() <= max_chars) {
    return body;
  }
  const std::string obs_mark = "## Observations";
  const auto obs_pos = body.find(obs_mark);
  if (obs_pos != std::string::npos && obs_pos < max_chars / 3) {
    const std::string head = body.substr(0, std::min(obs_pos + obs_mark.size() + 2, max_chars / 4));
    const std::size_t tail_budget =
        max_chars > head.size() + 40 ? max_chars - head.size() - 40 : max_chars / 2;
    const std::string tail = body.substr(body.size() - tail_budget);
    return head + "\n\n…[observations medias omitidas]…\n\n" + tail;
  }
  return "…[session head omitida]…\n\n" + body.substr(body.size() - max_chars);
}

std::string read_file_limited(const std::string& path, std::size_t max_chars) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string body = ss.str();
  if (body.size() <= max_chars) {
    return body;
  }
  const std::size_t head = max_chars / 3;
  const std::size_t tail = max_chars - head;
  return body.substr(0, head) + "\n\n…[session truncada]…\n\n" +
         body.substr(body.size() - tail);
}

// Explore: Instruction + slim map (detail only top-N) + short Observations tail.
std::string read_session_for_explore(const std::string& path, std::size_t max_chars) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string body = ss.str();

  const std::string map_mark = "## Ranked map";
  const std::string obs_mark = "## Observations";
  const auto map_pos = body.find(map_mark);
  const auto obs_pos = body.find(obs_mark);

  if (map_pos == std::string::npos) {
    if (body.size() <= max_chars) {
      return body;
    }
    return read_file_limited(path, max_chars);
  }

  std::string header = session_prompt_header(body, map_pos);
  header += map_mark;

  std::string map_body;
  std::string obs_body;
  if (obs_pos != std::string::npos && obs_pos > map_pos) {
    map_body = body.substr(map_pos + map_mark.size(), obs_pos - (map_pos + map_mark.size()));
    obs_body = body.substr(obs_pos);
  } else {
    map_body = body.substr(map_pos + map_mark.size());
  }

  if (obs_body.size() > kExploreObsBudget) {
    const std::string obs_head = obs_mark + "\n\n";
    const std::size_t keep = kExploreObsBudget > 100 ? kExploreObsBudget - 80 : kExploreObsBudget / 2;
    obs_body = obs_head + "…[observations medias omitidas]…\n\n" +
               obs_body.substr(obs_body.size() - std::min(keep, obs_body.size()));
  }

  const std::size_t reserved = header.size() + obs_body.size() + 100;
  const std::size_t map_budget =
      max_chars > reserved ? max_chars - reserved : max_chars / 3;
  map_body = slim_ranked_map_for_prompt(map_body, kExploreMapDetailTop, map_budget);

  std::string out = header + "\n" + map_body;
  if (!obs_body.empty()) {
    if (out.empty() || out.back() != '\n') {
      out.push_back('\n');
    }
    out += obs_body;
  }
  if (out.size() > max_chars) {
    out = out.substr(0, max_chars) + "\n…[explore prompt truncado]…\n";
  }
  return out;
}

// Edit (sin pack): instruction + compact map slice + Observations tail (feedback).
std::string read_session_for_edit(const std::string& path, std::size_t max_chars) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string body = ss.str();
  if (body.size() <= max_chars) {
    // Still drop legacy tool guide if present.
    const std::string instr_mark = "## Instruction";
    const auto instr_pos = body.find(instr_mark);
    if (instr_pos != std::string::npos && instr_pos > 0) {
      return body.substr(instr_pos);
    }
    return body;
  }

  const std::string map_mark = "## Ranked map";
  const std::string obs_mark = "## Observations";
  const auto map_pos = body.find(map_mark);
  const auto obs_pos = body.find(obs_mark);
  if (map_pos == std::string::npos || obs_pos == std::string::npos || obs_pos <= map_pos) {
    return read_file_tail(path, max_chars);
  }

  std::string head = session_prompt_header(body, map_pos);
  head += map_mark;
  std::string map_body =
      body.substr(map_pos + map_mark.size(), obs_pos - (map_pos + map_mark.size()));
  constexpr std::size_t kMapCap = 4000;
  map_body = slim_ranked_map_for_prompt(map_body, 3, kMapCap);
  head += "\n" + map_body;

  const std::size_t tail_budget =
      max_chars > head.size() + 60 ? max_chars - head.size() - 60 : max_chars / 2;
  std::string tail = body.substr(body.size() - std::min(tail_budget, body.size()));
  if (tail.find(obs_mark) == std::string::npos) {
    tail = std::string(obs_mark) + "\n\n…\n\n" + tail;
  }
  return head + "\n…\n\n" + tail;
}

std::string read_instruction_only(const std::string& session_body) {
  const std::string instr_mark = "## Instruction";
  const std::string map_mark = "## Ranked map";
  const auto instr_pos = session_body.find(instr_mark);
  if (instr_pos == std::string::npos) {
    return {};
  }
  const auto map_pos = session_body.find(map_mark, instr_pos);
  if (map_pos != std::string::npos && map_pos > instr_pos) {
    return session_body.substr(instr_pos, map_pos - instr_pos);
  }
  const auto obs_pos = session_body.find("## Observations", instr_pos);
  if (obs_pos != std::string::npos && obs_pos > instr_pos) {
    return session_body.substr(instr_pos, obs_pos - instr_pos);
  }
  return session_body.substr(instr_pos, std::min<std::size_t>(session_body.size() - instr_pos, 1200));
}

// Instruction + code pack (+ short Observations tail for edit_feedback / compile).
std::string read_session_for_pack(const std::string& workspace_root, std::size_t max_chars) {
  const std::string sess = [&]() {
    std::ifstream in(Level2Session::session_path(workspace_root));
    if (!in) {
      return std::string{};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }();
  std::string head = read_instruction_only(sess);
  if (head.empty()) {
    head = "## Instruction\n\n(sin instruction)\n\n";
  }

  std::string pack;
  {
    std::ifstream in(Level2Session::pack_path(workspace_root));
    if (in) {
      std::ostringstream ss;
      ss << in.rdbuf();
      pack = ss.str();
    }
  }
  if (pack.empty()) {
    pack = "(pack.md vacío — emite action=plan)\n";
  }

  std::string obs_tail;
  const auto obs_pos = sess.find("## Observations");
  if (obs_pos != std::string::npos) {
    constexpr std::size_t kObsTail = 1800;
    const std::string obs = sess.substr(obs_pos);
    if (obs.size() > kObsTail) {
      obs_tail = "## Observations\n\n…[cola]…\n\n" +
                 obs.substr(obs.size() - (kObsTail - 40));
    } else {
      obs_tail = obs;
    }
  }

  std::string out = head;
  if (!out.empty() && out.back() != '\n') {
    out.push_back('\n');
  }
  out += "## Code pack\n\n";
  const std::size_t reserved = out.size() + obs_tail.size() + 80;
  const std::size_t pack_budget = max_chars > reserved ? max_chars - reserved : max_chars / 2;
  if (pack.size() > pack_budget) {
    pack = pack.substr(0, pack_budget) + "\n…[pack truncado en prompt]…\n";
  }
  out += pack;
  if (!obs_tail.empty()) {
    if (out.back() != '\n') {
      out.push_back('\n');
    }
    out += obs_tail;
  }
  if (out.size() > max_chars) {
    out = out.substr(0, max_chars) + "\n…[pack prompt truncado]…\n";
  }
  return out;
}

// After compile_ok: full initial map + short obs + ask ¿algo más?
std::string read_session_for_map_review(const std::string& workspace_root, std::size_t max_chars) {
  const std::string sess = [&]() {
    std::ifstream in(Level2Session::session_path(workspace_root));
    if (!in) {
      return std::string{};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }();
  std::string head = read_instruction_only(sess);
  if (head.empty()) {
    head = "## Instruction\n\n";
  }

  std::string map_body = [&]() {
    std::ifstream in(Level2Session::map_initial_path(workspace_root));
    if (in) {
      std::ostringstream ss;
      ss << in.rdbuf();
      return ss.str();
    }
    return std::string{};
  }();
  if (map_body.empty()) {
    const auto map_pos = sess.find("## Ranked map");
    const auto obs_pos = sess.find("## Observations");
    if (map_pos != std::string::npos) {
      if (obs_pos != std::string::npos && obs_pos > map_pos) {
        map_body = sess.substr(map_pos + 13, obs_pos - (map_pos + 13));
      } else {
        map_body = sess.substr(map_pos + 13);
      }
    }
  }

  std::string obs_tail;
  const auto obs_pos = sess.find("## Observations");
  if (obs_pos != std::string::npos) {
    constexpr std::size_t kObsTail = 1600;
    const std::string obs = sess.substr(obs_pos);
    if (obs.size() > kObsTail) {
      obs_tail = "## Observations\n\n…[cola]…\n\n" +
                 obs.substr(obs.size() - (kObsTail - 40));
    } else {
      obs_tail = obs;
    }
  }

  std::ostringstream out;
  out << head;
  if (head.empty() || head.back() != '\n') {
    out << '\n';
  }
  out << "## Ranked map (inicial completo)\n\n";
  const std::size_t reserved = out.str().size() + obs_tail.size() + 120;
  const std::size_t map_budget = max_chars > reserved ? max_chars - reserved : max_chars / 2;
  // Prefer full map; only slim if over budget (keep more detail than explore).
  if (map_body.size() > map_budget) {
    map_body = slim_ranked_map_for_prompt(map_body, 12, map_budget);
  }
  out << map_body;
  if (!obs_tail.empty()) {
    out << '\n' << obs_tail;
  }
  std::string s = out.str();
  if (s.size() > max_chars) {
    s = s.substr(0, max_chars) + "\n…[map_review truncado]…\n";
  }
  return s;
}

std::string phase_banner(const std::string& phase, int step, int max_steps) {
  return "L2 ▸ fase=" + phase + " paso=" + std::to_string(step) + "/" +
         std::to_string(max_steps);
}

std::string build_system_prompt(const std::string& extra) {
  std::ostringstream out;
  out << "Eres el Nivel 2 (coder) de tuide. Exploras el repo y emites ediciones "
         "Search/Replace de match único.\n"
         "Responde SIEMPRE con UN solo objeto JSON. PROHIBIDO markdown/prosa fuera del JSON.\n"
         "Formatos:\n"
         "{\"action\":\"plan\",\"targets\":[\"src/a.cpp:Foo\",\"src/b.cpp:42\"],\"summary\":\"…\"}\n"
         "{\"action\":\"tools\",\"calls\":[{\"name\":\"get_code_of\",\"arg\":\"src/a.cpp:Foo\"},"
         "{\"name\":\"search\",\"arg\":\"wake\"}]}  // máx 4; extras tras el pack\n"
         "{\"action\":\"tool\",\"name\":\"get_code_of\",\"arg\":\"src/foo.cpp:Symbol\"}\n"
         "{\"action\":\"done\",\"summary\":\"evidencia paths:líneas\",\"next\":\"edit\"}\n"
         "{\"action\":\"done\",\"summary\":\"no encontré X; ¿concretas?\",\"next\":\"clarify\"}\n"
         "{\"action\":\"done\",\"summary\":\"cambios listos en paths…\"}  // fin real (sin next)\n"
         "{\"action\":\"edit\",\"hunks\":[{\"path\":\"src/foo.cpp\",\"search\":\"exacto único\","
         "\"replace\":\"nuevo\"}]}\n"
         "Fases: en explore la PRIMERA mirada es action=plan (watchlist path:Symbol). "
         "El runtime baja todos los targets + 1 outline/archivo a un pack bajo presupuesto. "
         "Tras el pack el prompt es Instruction+pack (sin mapa). "
         "Extras: tools máx 4. Si [TRUNCATED], usa refetch path:A-B o path:Symbol#mid|#tail "
         "(default de get_code_of largo = head+tail). "
         "action=edit es para phase=edit; si emites edit en explore el runtime auto-promueve. "
         "Tras edit el runtime compila: compile OK restaura el mapa inicial y pregunta "
         "«¿algo más?» (plan / edit / done). Clarify prematuro: pushback "
         "(ai.level2.clarify_pushback_max). "
         "Reglas: next=edit solo con evidencia en pack/Observations; search único en el "
         "archivo; no inventes paths; tras edit_feedback corrige el search; "
         "tras compile fail reemite edit.\n";
  out << Level2Session::tool_guide_markdown();
  if (!extra.empty()) {
    out << "\n" << extra << "\n";
  }
  return out.str();
}

std::string build_user_prompt(const std::string& workspace_root, const std::string& phase,
                              int step, bool has_pack, bool map_review) {
  std::ostringstream out;
  out << "phase=" << phase << " step=" << step;
  if (map_review) {
    out << " map_review=1";
  }
  if (has_pack) {
    out << " has_pack=1";
  }
  out << "\n\n";

  if (map_review) {
    out << "Compile OK. Aquí tienes el **mapa inicial completo** otra vez.\n"
           "¿Algo más?\n"
           "- Más código → {\"action\":\"plan\",\"targets\":[…]}\n"
           "- Más edits → {\"action\":\"edit\",\"hunks\":[…]}\n"
           "- Fin → {\"action\":\"done\",\"summary\":\"…\"} sin next.\n\n";
    out << read_session_for_map_review(workspace_root, kMaxPromptCharsEdit);
  } else if (has_pack) {
    if (phase == "edit") {
      out << "phase=edit. Opciones:\n"
             "- edit_feedback / compile_feedback → corrige con action=edit.\n"
             "- Instruction cubierta → done sin next.\n"
             "- Falta contexto → plan o tools.\n"
             "Contexto: Instruction + Code pack (sin mapa rankeado completo).\n\n";
    } else {
      out << "Ya hay Code pack. Decide: done next=edit, edit, ampliar plan, o tools extras.\n"
             "Contexto: Instruction + pack (mapa omitido).\n\n";
    }
    out << read_session_for_pack(workspace_root, kMaxPromptCharsEdit);
  } else if (phase == "edit") {
    out << "phase=edit (sin pack aún). Corrige con edit o arma plan/tools.\n\n";
    out << read_session_for_edit(Level2Session::session_path(workspace_root),
                                 kMaxPromptCharsEdit);
  } else {
    out << "El ## Ranked map es tu base. Primera acción preferida: "
           "{\"action\":\"plan\",\"targets\":[\"path:Symbol\",…]} (máx 16). "
           "El runtime arma el pack. Tools ad-hoc solo si hace falta buscar.\n\n";
    out << read_session_for_explore(Level2Session::session_path(workspace_root),
                                    kMaxPromptCharsExplore);
  }
  return out.str();
}

}  // namespace

Level2AutonomousLoopResult run_level2_autonomous(Level2Session& session, L2Brain& brain,
                                                 const Level2AutonomousLoopOpts& opts,
                                                 const Level2PhaseLogFn& log,
                                                 std::atomic<bool>* cancel) {
  using clock = std::chrono::steady_clock;
  Level2AutonomousLoopResult result;
  const int max_steps = opts.settings.max_steps > 0 ? opts.settings.max_steps : 32;
  const std::string system = build_system_prompt(opts.system_prompt_extra);
  const auto run_t0 = clock::now();

  auto emit = [&](const std::string& line) {
    if (log) {
      log(line);
    }
  };

  auto elapsed_ms = [](clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
  };

  ai_trace(AiTraceChannel::L2, "l2_run_begin",
           "{\"backend\":\"" + ai_trace_escape(brain.name()) + "\",\"max_steps\":" +
               std::to_string(max_steps) + ",\"n_ctx\":" + std::to_string(opts.settings.n_ctx) +
               "}");

  for (int step = 1; step <= max_steps; ++step) {
    if (cancel != nullptr && cancel->load()) {
      result.error = "cancelado";
      result.phase = "cancelled";
      result.steps = step - 1;
      ai_trace(AiTraceChannel::L2, "l2_run_end",
               "{\"ok\":0,\"phase\":\"cancelled\",\"steps\":" + std::to_string(result.steps) +
                   ",\"total_ms\":" + std::to_string(elapsed_ms(run_t0)) + "}");
      emit("L2 ▸ cancelado");
      return result;
    }

    // Peek phase from status file via a lightweight tool-less path: apply nothing.
    // status_text includes phase=; also process after each action updates state.
    const std::string status = session.status_text(opts.workspace_root);
    std::string phase = "explore";
    bool has_pack = false;
    bool map_review = false;
    {
      const auto p = status.find("phase: ");
      if (p != std::string::npos) {
        const auto end = status.find_first_of(" \n", p + 7);
        phase = status.substr(p + 7, end == std::string::npos ? std::string::npos : end - (p + 7));
      }
      has_pack = status.find("has_pack: yes") != std::string::npos;
      map_review = status.find("map_review: yes") != std::string::npos;
    }
    if (status.find("done: yes") != std::string::npos || phase == "done" || phase == "clarify") {
      result.ok = phase == "done" || phase == "clarify";
      result.phase = phase;
      result.steps = step - 1;
      result.summary = phase == "clarify" ? "clarify (hace falta más detalle)" : "sesión terminada";
      ai_trace(AiTraceChannel::L2, "l2_run_end",
               std::string("{\"ok\":") + (result.ok ? "1" : "0") + ",\"phase\":\"" + phase +
                   "\",\"steps\":" + std::to_string(result.steps) +
                   ",\"total_ms\":" + std::to_string(elapsed_ms(run_t0)) + "}");
      emit(phase_banner(phase, step - 1, max_steps) + " — fin (" + result.summary + ")");
      return result;
    }
    // compile is runtime-owned after edit; if stuck in compile, poke run_compile.
    if (phase == "compile") {
      emit(phase_banner(phase, step, max_steps) + " — compilando…");
      const auto t_comp = clock::now();
      const auto tr = session.run_compile(opts.workspace_root);
      ai_trace(AiTraceChannel::L2, "l2_compile_step",
               "{\"step\":" + std::to_string(step) + ",\"ok\":" + (tr.ok ? "1" : "0") +
                   ",\"duration_ms\":" + std::to_string(elapsed_ms(t_comp)) + ",\"phase\":\"" +
                   tr.phase + "\"}");
      emit(std::string("L2 ▸ compile ") + (tr.ok ? "OK" : "FAIL") + " — " + tr.summary);
      result.steps = step;
      continue;
    }

    const auto step_t0 = clock::now();
    emit(phase_banner(phase, step, max_steps) + " — pidiendo acción al modelo (" + brain.name() +
         ")…");

    L2BrainRequest breq;
    breq.system_prompt = system;
    breq.user_prompt = build_user_prompt(opts.workspace_root, phase, step, has_pack, map_review);
    breq.phase = phase;
    breq.max_tokens = opts.settings.max_tokens;
    breq.n_ctx = opts.settings.n_ctx;
    breq.temperature = opts.settings.temperature;

    const auto propose_t0 = clock::now();
    const L2BrainResult br = brain.propose(breq, cancel);
    const auto propose_ms = elapsed_ms(propose_t0);
    ai_trace(AiTraceChannel::L2, "l2_propose",
             "{\"step\":" + std::to_string(step) + ",\"phase\":\"" + phase + "\",\"backend\":\"" +
                 ai_trace_escape(brain.name()) + "\",\"ok\":" + (br.ok ? "1" : "0") +
                 ",\"duration_ms\":" + std::to_string(propose_ms) + ",\"prompt_chars\":" +
                 std::to_string(breq.system_prompt.size() + breq.user_prompt.size()) +
                 ",\"reply_chars\":" + std::to_string(br.text.size()) +
                 (br.ok ? "" : (",\"error\":\"" + ai_trace_escape(br.error) + "\"")) + "}");
    if (!br.ok) {
      emit("L2 ▸ modelo error: " + br.error);
      result.error = br.error;
      result.phase = phase;
      result.steps = step;
      // Soft retry next step unless cancel.
      continue;
    }

    const L2Action action = parse_l2_action(br.text);
    emit(std::string("L2 ▸ acción=") + l2_action_kind_name(action.kind) +
         (action.name.empty() ? "" : (" name=" + action.name)) +
         (action.error.empty() ? "" : (" err=" + action.error)));

    Level2TurnResult tr;
    const auto action_t0 = clock::now();
    if (action.kind == L2ActionKind::Tool || action.kind == L2ActionKind::Tools) {
      const std::vector<L2ToolCall>& calls =
          !action.calls.empty()
              ? action.calls
              : std::vector<L2ToolCall>{L2ToolCall{action.name, action.arg}};
      if (calls.size() == 1) {
        emit("L2 ▸ tool " + calls[0].name +
             (calls[0].arg.empty() ? "" : ("(" + calls[0].arg.substr(0, 80) + ")")));
        tr = session.apply_tool(opts.workspace_root, calls[0].name, calls[0].arg);
      } else {
        emit("L2 ▸ tools batch n=" + std::to_string(calls.size()));
        for (const auto& c : calls) {
          emit("  · " + c.name + (c.arg.empty() ? "" : ("(" + c.arg.substr(0, 60) + ")")));
        }
        tr = session.apply_tools(opts.workspace_root, calls);
      }
    } else if (action.kind == L2ActionKind::Plan) {
      emit("L2 ▸ plan targets=" + std::to_string(action.targets.size()) +
           (action.summary.empty() ? "" : (" — " + action.summary.substr(0, 80))));
      for (const auto& t : action.targets) {
        emit("  · " + t.substr(0, 100));
      }
      tr = session.apply_plan(opts.workspace_root, action.targets, action.summary);
      emit(std::string("L2 ▸ pack ") + (tr.ok ? "OK" : "FAIL") + " — " + tr.summary.substr(0, 160));
    } else if (action.kind == L2ActionKind::Done) {
      emit("L2 ▸ done next=" + (action.next.empty() ? "(none)" : action.next) + " — " +
           action.summary.substr(0, 120));
      tr = session.mark_done(opts.workspace_root, action.summary, action.next);
      if (tr.summary.find("clarify_pushback") != std::string::npos) {
        emit("L2 ▸ clarify prematuro → " + tr.summary + " (pide más código)");
      }
    } else if (action.kind == L2ActionKind::Edit) {
      // Opción B: si el modelo salta done next=edit estando en explore, auto-promover.
      if (phase == "explore") {
        emit("L2 ▸ edit en explore → auto phase=edit (skip done next=edit)");
        ai_trace(AiTraceChannel::L2, "l2_auto_phase_edit",
                 "{\"step\":" + std::to_string(step) + ",\"reason\":\"edit_while_explore\"}");
        const auto promo = session.mark_done(
            opts.workspace_root,
            "auto: modelo emitió edit en explore; promoviendo a phase=edit", "edit");
        if (!promo.ok) {
          emit("L2 ▸ auto phase=edit falló: " + promo.error);
          result.steps = step;
          result.phase = promo.phase.empty() ? phase : promo.phase;
          result.error = promo.error;
          continue;
        }
        phase = "edit";
      }
      emit("L2 ▸ edit hunks=" + std::to_string(action.hunks.size()) + " (apply+compile)…");
      tr = session.apply_edit(opts.workspace_root, action.hunks);
      emit(std::string("L2 ▸ tras edit: phase=") + tr.phase + " ok=" + (tr.ok ? "1" : "0") +
           " — " +
           (tr.ok ? tr.summary.substr(0, 160)
                  : (tr.error.empty() ? tr.summary : tr.error).substr(0, 200)));
    } else {
      emit("L2 ▸ acción inválida; reintentando. " + action.error);
      result.steps = step;
      continue;
    }
    const auto action_ms = elapsed_ms(action_t0);
    const auto step_ms = elapsed_ms(step_t0);

    ai_trace(AiTraceChannel::L2, "l2_action",
             "{\"step\":" + std::to_string(step) + ",\"phase\":\"" + phase + "\",\"kind\":\"" +
                 l2_action_kind_name(action.kind) + "\",\"name\":\"" +
                 ai_trace_escape(action.name) + "\",\"ok\":" + (tr.ok ? "1" : "0") +
                 ",\"result_phase\":\"" + tr.phase + "\",\"duration_ms\":" +
                 std::to_string(action_ms) +
                 (tr.error.empty() ? "" : (",\"error\":\"" + ai_trace_escape(tr.error) + "\"")) +
                 "}");
    ai_trace(AiTraceChannel::L2, "l2_step",
             "{\"step\":" + std::to_string(step) + ",\"phase\":\"" + phase + "\",\"backend\":\"" +
                 ai_trace_escape(brain.name()) + "\",\"kind\":\"" +
                 l2_action_kind_name(action.kind) + "\",\"propose_ms\":" +
                 std::to_string(propose_ms) + ",\"action_ms\":" + std::to_string(action_ms) +
                 ",\"total_ms\":" + std::to_string(step_ms) + "}");

    result.steps = step;
    result.phase = tr.phase;
    if (!tr.ok && !tr.error.empty()) {
      emit("L2 ▸ turn error: " + tr.error);
    }
    if (tr.phase == "done" || tr.phase == "clarify") {
      result.ok = true;
      result.summary = tr.summary.empty() ? action.summary : tr.summary;
      ai_trace(AiTraceChannel::L2, "l2_run_end",
               std::string("{\"ok\":1,\"phase\":\"") + tr.phase + "\",\"steps\":" +
                   std::to_string(result.steps) +
                   ",\"total_ms\":" + std::to_string(elapsed_ms(run_t0)) + "}");
      emit(phase_banner(tr.phase, step, max_steps) + " — " +
           (tr.phase == "clarify" ? "clarify" : "éxito"));
      return result;
    }
  }

  result.error = "max_steps agotados (" + std::to_string(max_steps) + ")";
  result.phase = "timeout";
  ai_trace(AiTraceChannel::L2, "l2_run_end",
           "{\"ok\":0,\"phase\":\"timeout\",\"steps\":" + std::to_string(max_steps) +
               ",\"total_ms\":" + std::to_string(elapsed_ms(run_t0)) + "}");
  emit("L2 ▸ " + result.error);
  return result;
}

}  // namespace tuide
