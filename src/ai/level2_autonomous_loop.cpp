#include "ai/level2_autonomous_loop.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai/ai_trace.hpp"
#include "ai/ai_types.hpp"
#include "ai/l2_action.hpp"
#include "ai/l2_explore_a.hpp"
#include "ai/l2_feat.hpp"
#include "ai/l2_grammar.hpp"
#include "ai/l2_pack_review.hpp"

namespace tuide {
namespace {

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
std::string read_session_for_explore(const std::string& path, std::size_t max_chars,
                                     std::size_t obs_budget) {
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

  if (obs_budget > 0 && obs_body.size() > obs_budget) {
    const std::string obs_head = obs_mark + "\n\n";
    const std::size_t keep = obs_budget > 100 ? obs_budget - 80 : obs_budget / 2;
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
std::string read_session_for_pack(const std::string& workspace_root, std::size_t max_chars,
                                  std::size_t obs_tail_budget) {
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
  const std::size_t kObsCap = obs_tail_budget > 0 ? obs_tail_budget : 1800;
  if (l2_feat::enabled("EDIT_LEAN_PROMPT")) {
    obs_tail = Level2Session::last_edit_relevant_observation(sess, kObsCap);
  } else {
    const auto obs_pos = sess.find("## Observations");
    if (obs_pos != std::string::npos) {
      const std::string obs = sess.substr(obs_pos);
      if (obs.size() > kObsCap) {
        obs_tail = "## Observations\n\n…[cola]…\n\n" +
                   obs.substr(obs.size() - (kObsCap - 40));
      } else {
        obs_tail = obs;
      }
    }
  }

  const bool coverage_obs =
      obs_tail.find("post_edit_coverage") != std::string::npos ||
      obs_tail.find("edit_covered_path") != std::string::npos ||
      obs_tail.find("covered_path_limit") != std::string::npos;

  std::string out = head;
  if (!out.empty() && out.back() != '\n') {
    out.push_back('\n');
  }
  out += "## Code pack\n\n";
  if (coverage_obs) {
    pack = "_(pack original omitido: SEARCH = bloque **fresh** de Observations. "
           "La firma del path cubierto es referencia, no la edites.)_\n";
  }
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

std::string build_system_prompt(const Level2AutonomousLoopOpts& opts,
                                const std::string& phase = {}, bool map_review = false) {
  const AiWorkflowKind workflow = parse_ai_workflow_kind(opts.workflow);
  const bool lean_edit = l2_feat::enabled("EDIT_LEAN_PROMPT") && phase == "edit" &&
                         !ai_workflow_is_readonly(workflow);
  std::ostringstream out;
  if (workflow == AiWorkflowKind::Ask) {
    out << "Eres el Nivel 2 (explicador) de tuide. Exploras el repo y respondes en "
           "lenguaje natural.\n"
           "Responde SIEMPRE con UN solo objeto JSON. PROHIBIDO markdown/prosa fuera del JSON.\n"
           "Formatos:\n"
           "{\"action\":\"plan\",\"targets\":[\"src/a.cpp:Foo\"],\"summary\":\"…\"}\n"
           "{\"action\":\"tools\",\"calls\":[{\"name\":\"get_code_of\",\"arg\":\"…\"}]}\n"
           "{\"action\":\"tool\",\"name\":\"get_code_of\",\"arg\":\"…\"}\n"
           "{\"action\":\"synthesize\",\"summary\":\"explicación clara al usuario\"}\n"
           "{\"action\":\"done\",\"summary\":\"…\",\"next\":\"clarify\"}\n"
           "Fases: explore → plan/pack → synthesize. PROHIBIDO action=edit y next=edit.\n"
           "Tras el pack (o con suficiente evidencia) emite synthesize con la respuesta completa.\n";
  } else if (workflow == AiWorkflowKind::Plan) {
    out << "Eres el Nivel 2 (planificador) de tuide. Exploras el repo y propones un plan "
           "de cambios SIN editar.\n"
           "Responde SIEMPRE con UN solo objeto JSON. PROHIBIDO markdown/prosa fuera del JSON.\n"
           "Formatos:\n"
           "{\"action\":\"plan\",\"targets\":[\"src/a.cpp:Foo\"],\"summary\":\"…\"}\n"
           "{\"action\":\"tools\",\"calls\":[{\"name\":\"get_code_of\",\"arg\":\"…\"}]}\n"
           "{\"action\":\"tool\",\"name\":\"get_code_of\",\"arg\":\"…\"}\n"
           "{\"action\":\"synthesize\",\"summary\":\"plan: archivos, pasos, riesgos\"}\n"
           "{\"action\":\"done\",\"summary\":\"…\",\"next\":\"clarify\"}\n"
           "Fases: explore → plan/pack → synthesize(plan). PROHIBIDO edit/compile.\n"
           "El summary de synthesize debe listar paths concretos y pasos ordenados.\n";
  } else if (workflow == AiWorkflowKind::Git) {
    out << "Eres el Nivel 2 (analista git) de tuide. Usas ## Git context y, si hace falta, "
           "código actual para explicar cambios del historial.\n"
           "Responde SIEMPRE con UN solo objeto JSON. PROHIBIDO markdown/prosa fuera del JSON.\n"
           "Formatos:\n"
           "{\"action\":\"plan\",\"targets\":[\"src/a.cpp:Foo\"],\"summary\":\"…\"}\n"
           "{\"action\":\"tools\",\"calls\":[{\"name\":\"get_code_of\",\"arg\":\"…\"}]}\n"
           "{\"action\":\"tool\",\"name\":\"get_code_of\",\"arg\":\"…\"}\n"
           "{\"action\":\"synthesize\",\"summary\":\"qué cambió, impacto, riesgos\"}\n"
           "{\"action\":\"done\",\"summary\":\"…\",\"next\":\"clarify\"}\n"
           "Puedes synthesize directo si el Git context basta. PROHIBIDO edit/compile.\n";
  } else if (lean_edit && map_review) {
    out << "Eres el Nivel 2 (coder) de tuide. Instruction cubierta y compile OK.\n"
           "Emite {\"action\":\"done\",\"summary\":\"…qué cambiaste…\"} sin next.\n"
           "PROHIBIDO action=plan, action=tool y más hunks. Un edit extra cierra sin aplicar.\n";
  } else if (lean_edit) {
    out << "Eres el Nivel 2 (coder) de tuide en phase=edit.\n"
           "Emite hunks Search/Replace en texto plano (estilo Aider). "
           "NO pongas código C++ dentro de JSON. PROHIBIDO action=plan.\n"
           "Formato:\n"
           "src/foo.cpp\n"
           "<<<<<<< SEARCH\n"
           "bloque exacto único del pack\n"
           "=======\n"
           "bloque nuevo\n"
           ">>>>>>> REPLACE\n"
           "También válido: {\"action\":\"done\",\"summary\":\"cambios listos\"}\n"
           "PROHIBIDO action=plan y action=tool. Un hunk, un path.\n"
           "Tras compile_fail: corrige el SEARCH (span del pack).\n";
  } else if (l2_feat::enabled("EDIT_LEAN_PROMPT") && phase == "explore") {
    out << "Eres el Nivel 2 (coder) de tuide.\n"
           "Responde SIEMPRE con UN solo objeto JSON. PROHIBIDO markdown/prosa fuera del JSON.\n"
           "Primer paso: {\"action\":\"plan\",\"targets\":[\"src/a.cpp:Foo\",\"src/b.hpp:Bar\"]}\n"
           "Luego {\"action\":\"done\",\"summary\":\"…\",\"next\":\"edit\"} o action=edit. "
           "No repitas el mismo get_code_of. Si no cabe JSON: emite SOLO el objeto, sin texto.\n";
  } else {
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
           "Fases: en explore la PRIMERA mirada es action=plan (watchlist path:Symbol|path:A-B; "
           "evitar path bare). "
           "Orden de `targets` = prioridad: primero los must (control/estado del request); "
           "el runtime empaqueta en ese orden y omite por la cola si no cabe. "
           "El runtime normaliza bare→símbolo por needles, merge de packs, prioriza fragmentos "
           "pequeños y auto-refetch de truncados. "
           "pack_incomplete = gaps Instruction↔pack (o cero fragmentos), no meros Truncated. "
           "Si map_stale=1 no confíes en el top del mapa. "
           "Tras el pack el prompt es Instruction+pack (sin mapa). "
           "Extras: tools máx 4. Si [TRUNCATED], refetch tip path:A-B / path:Symbol#mid|#tail "
           "solo si editas esa ventana; Truncated no implica pack incompleto ni bloquea next=edit "
           "si el locus de control/estado ya está en el pack. "
           "done next=edit con gaps Instruction reales puede rechazarse (pushback). "
           "action=edit es para phase=edit; si emites edit en explore el runtime auto-promueve. "
           "Tras edit el runtime compila: compile OK restaura el mapa inicial y pregunta "
           "«¿algo más?» (plan / edit / done). Clarify prematuro: pushback "
           "(ai.level2.clarify_pushback_max). "
           "Reglas: next=edit solo con evidencia en pack/Observations; search único en el "
           "archivo; no inventes paths; tras edit_feedback corrige el search; "
           "tras compile fail reemite edit.\n";
  }
  if (!opts.tool_guide_override.empty()) {
    out << opts.tool_guide_override;
  } else if (lean_edit) {
    out << Level2Session::tool_guide_edit_markdown();
  } else if (phase == "explore_a") {
    out << Level2Session::tool_guide_explore_a_markdown();
  } else if (phase == "explore_b" && l2_feat::enabled("L2_EXPLORE_PHASE_A")) {
    out << Level2Session::tool_guide_explore_b_markdown();
  } else if (l2_feat::enabled("EDIT_LEAN_PROMPT") &&
             (phase == "explore" || phase == "explore_b")) {
    out << Level2Session::tool_guide_explore_markdown();
  } else {
    out << Level2Session::tool_guide_markdown();
  }
  if (!opts.system_prompt_extra.empty()) {
    out << "\n" << opts.system_prompt_extra << "\n";
  }
  out << "\nSi el user prompt marca resume=1, hay historial de un turno previo (Follow-ups / "
         "Prior answer / pack). Prefiere atajar con synthesize o edit si basta; solo "
         "explore/plan si falta evidencia.\n";
  return out.str();
}

std::string build_user_prompt(const std::string& workspace_root, const std::string& phase,
                              int step, bool has_pack, bool map_review, bool map_stale,
                              bool pack_incomplete, bool resume, AiWorkflowKind workflow,
                              const L2ContextBudget& budget, const Level2AutonomousLoopOpts& opts,
                              const std::string& recover_note = {},
                              bool pack_review_pending = false) {
  std::ostringstream out;
  out << "phase=" << phase << " step=" << step << " workflow=" << ai_workflow_kind_name(workflow);
  if (resume) {
    out << " resume=1";
  }
  if (map_review) {
    out << " map_review=1";
  }
  if (has_pack) {
    out << " has_pack=1";
  }
  if (map_stale) {
    out << " map_stale=1";
  }
  if (pack_incomplete) {
    out << " pack_incomplete=1";
  }
  out << "\n\n";
  if (!recover_note.empty() && l2_feat::enabled("EDIT_LEAN_PROMPT") &&
      !(has_pack && pack_review_pending)) {
    out << recover_note << "\n\n";
  }

  const bool readonly = ai_workflow_is_readonly(workflow);
  const std::size_t prompt_explore =
      budget.prompt_explore > 0 ? budget.prompt_explore : 10000;
  const std::size_t prompt_edit = budget.prompt_edit > 0 ? budget.prompt_edit : 8000;
  const std::size_t resume_chars = budget.resume_chars > 0 ? budget.resume_chars : 5500;
  const std::size_t obs_tail = budget.obs_tail > 0 ? budget.obs_tail : 1800;
  // Explore observations slice ~35% of explore prompt at baseline (3500/10000).
  const std::size_t explore_obs =
      std::max<std::size_t>(1200, static_cast<std::size_t>(prompt_explore * 35 / 100));

  if (resume) {
    out << "Follow-up del usuario (resume=1). Lee ## Follow-ups + Prior answer/pack.\n";
    if (readonly) {
      out << "Si basta el historial → action=synthesize. Si falta evidencia → plan/tools.\n"
             "PROHIBIDO edit.\n\n";
    } else {
      out << "Dos vías:\n"
             "- Historial/pack suficiente → action=edit (luego compile) o done.\n"
             "- Falta contexto / otro módulo → plan/tools (explore normal).\n\n";
    }
    const std::string prior =
        Level2Session::resume_context_markdown(workspace_root, resume_chars);
    if (!prior.empty()) {
      out << prior;
      if (prior.back() != '\n') {
        out << '\n';
      }
      out << '\n';
    }
  }

  if (map_review && !readonly) {
    if (l2_feat::enabled("EDIT_LEAN_PROMPT")) {
      out << "Instruction cubierta y compile OK.\n"
             "Emite {\"action\":\"done\",\"summary\":\"…qué cambiaste…\"} sin next.\n"
             "PROHIBIDO plan/tool JSON, mapa rankeado y hunks extra.\n\n";
      if (!opts.user_overlay_map_review.empty()) {
        out << opts.user_overlay_map_review << "\n\n";
      }
      out << read_session_for_pack(workspace_root, prompt_edit, obs_tail);
    } else {
      out << "Compile OK. Aquí tienes el **mapa inicial completo** otra vez.\n"
             "¿Algo más?\n"
             "- Más código → {\"action\":\"plan\",\"targets\":[…]}\n"
             "- Más edits → {\"action\":\"edit\",\"hunks\":[…]}\n"
             "- Fin → {\"action\":\"done\",\"summary\":\"…\"} sin next.\n\n";
      if (!opts.user_overlay_map_review.empty()) {
        out << opts.user_overlay_map_review << "\n\n";
      }
      out << read_session_for_map_review(workspace_root, prompt_edit);
    }
  } else if (has_pack) {
    if (phase == "edit" && !readonly) {
      out << "phase=edit. Opciones:\n"
             "- Prioridad: hunk Aider (path + SEARCH/REPLACE) con search = span del pack.\n"
             "- PROHIBIDO action=plan y action=tool (pack ya cubre Instruction).\n"
             "- edit_feedback / compile_feedback → corrige el SEARCH (no tools en bucle).\n"
             "- Instruction cubierta → {\"action\":\"done\",\"summary\":\"…\"} sin next.\n"
             "Contexto: Instruction + Code pack (sin mapa rankeado completo).\n\n";
      if (!opts.user_overlay_edit.empty()) {
        out << opts.user_overlay_edit << "\n\n";
      }
      out << "Empieza la respuesta con un path del pack y la marca SEARCH de Aider.\n\n";
    } else if (readonly) {
      out << "Ya hay Code pack"
          << (pack_incomplete ? " (**pack_incomplete**: gaps Instruction↔pack)" : "")
          << ". Emite action=synthesize con la respuesta"
          << (workflow == AiWorkflowKind::Plan ? " (plan de cambios)" : "")
          << ", o amplía plan/tools si falta evidencia.\n"
             "PROHIBIDO edit. Contexto: Instruction + pack.\n\n";
      if (!opts.user_overlay_pack.empty()) {
        out << opts.user_overlay_pack << "\n\n";
      }
    } else {
      out << "Ya hay Code pack"
          << (pack_incomplete ? " (**pack_incomplete**: gaps Instruction↔pack)" : "")
          << ". Decide: done next=edit, edit, ampliar plan, o tools extras.\n"
             "Preferir path:Symbol / path:A-B. Contexto: Instruction + pack.\n"
             "Truncated en el pack no basta para ampliar: si el locus está cubierto, "
             "preferir done next=edit.\n";
      if (pack_review_pending) {
        out << "Pack review ABIERTA: PROHIBIDO repetir targets ya en watchlist/pack.\n"
               "Siguiente acción: `action=plan` con paths NUEVOS de MAP HITS (prioriza src/ai).\n\n";
        if (!recover_note.empty()) {
          out << recover_note << "\n\n";
        }
      } else {
        out << "Pack cubierto: un segundo `plan` igual fuerza phase=edit.\n\n";
      }
      if (!opts.user_overlay_pack.empty()) {
        out << opts.user_overlay_pack << "\n\n";
      }
    }
    out << read_session_for_pack(workspace_root, prompt_edit, obs_tail);
  } else if (phase == "edit" && !readonly) {
    if (l2_feat::enabled("EDIT_LEAN_PROMPT")) {
      out << "phase=edit. Opciones:\n"
             "- Prioridad: hunk Aider (path + SEARCH/REPLACE) con search = span fresco / pack.\n"
             "- PROHIBIDO action=plan y action=tool.\n"
             "- Instruction cubierta → {\"action\":\"done\",\"summary\":\"…\"} sin next.\n\n";
      if (!opts.user_overlay_edit.empty()) {
        out << opts.user_overlay_edit << "\n\n";
      }
      out << "Empieza la respuesta con un path y la marca SEARCH de Aider.\n\n";
      out << read_session_for_pack(workspace_root, prompt_edit, obs_tail);
    } else {
      out << "phase=edit (sin pack aún). Corrige con edit o arma plan/tools.\n\n";
      if (!opts.user_overlay_edit.empty()) {
        out << opts.user_overlay_edit << "\n\n";
      }
      out << read_session_for_edit(Level2Session::session_path(workspace_root), prompt_edit);
    }
  } else {
    if (workflow == AiWorkflowKind::Git) {
      out << "Prioriza ## Git context. Si basta para responder: "
             "{\"action\":\"synthesize\",\"summary\":\"…\"}. "
             "Si necesitas código actual: plan/tools sobre el ## Ranked map"
          << (map_stale ? " (**map_stale**)" : "") << ".\n\n";
    } else if (resume) {
      out << "Sin pack aún en este follow-up. Si Prior answer/targets bastan para "
          << (readonly ? "synthesize" : "edit")
          << ", hazlo; si no, plan/tools sobre el ## Ranked map"
          << (map_stale ? " (**map_stale**)" : "") << ".\n\n";
    } else if (phase == "explore_b" && l2_feat::enabled("L2_EXPLORE_PHASE_A")) {
      out << "Phase B: materializa pack desde ## Loci (plan vacío o targets de loci). "
             "PROHIBIDO multi-stem fuera de loci.\n\n";
    } else {
      out << "El ## Ranked map es tu base"
          << (map_stale ? " (**map_stale**: poco alineado a la Instruction; prioriza search/plan)"
                        : "")
          << ". Primera acción preferida: "
             "{\"action\":\"plan\",\"targets\":[\"path:Symbol\",…]} (máx 16; no path bare). "
             "El runtime arma el pack (merge + auto-refetch truncados).\n\n";
    }
    if (!opts.user_overlay_explore.empty()) {
      out << opts.user_overlay_explore << "\n\n";
    }
    std::size_t explore_cap = prompt_explore;
    if (!recover_note.empty() && l2_feat::enabled("EDIT_LEAN_PROMPT")) {
      explore_cap = std::max<std::size_t>(3500, prompt_explore / 2);
    }
    out << read_session_for_explore(Level2Session::session_path(workspace_root), explore_cap,
                                    explore_obs);
  }
  return out.str();
}

constexpr int kMaxPackReviewCycles = 3;
constexpr int kPushbackEscalateAfter = 2;

bool pack_review_enabled() { return l2_feat::enabled("PACK_REVIEW"); }

bool status_has_pack_review_ok(const std::string& flags) {
  return flags.find("pack_review_ok=yes") != std::string::npos;
}

std::string read_path_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

void load_watchlist_rejected_from_state(const std::string& workspace_root,
                                        std::vector<std::string>* watchlist,
                                        std::vector<std::string>* rejected) {
  if (watchlist == nullptr && rejected == nullptr) {
    return;
  }
  const std::string st_raw =
      read_path_file(Level2Session::state_path(workspace_root));
  if (st_raw.empty()) {
    return;
  }
  try {
    const auto j = nlohmann::json::parse(st_raw);
    if (watchlist != nullptr && j.contains("watchlist") && j["watchlist"].is_array()) {
      for (const auto& t : j["watchlist"]) {
        if (t.is_string()) {
          watchlist->push_back(t.get<std::string>());
        }
      }
    }
    if (rejected != nullptr && j.contains("rejected_targets") &&
        j["rejected_targets"].is_array()) {
      for (const auto& t : j["rejected_targets"]) {
        if (t.is_string()) {
          rejected->push_back(t.get<std::string>());
        }
      }
    }
  } catch (...) {
  }
}

std::string build_pushback_replan_note(const Level2AutonomousLoopOpts& opts) {
  const std::string session_md =
      read_path_file(Level2Session::session_path(opts.workspace_root));
  const std::string map_last =
      read_path_file(opts.workspace_root + "/.tuide/ai/map_last.md");
  const std::string distilled = extract_distilled_intent_block(session_md);
  std::vector<std::string> watchlist;
  std::vector<std::string> rejected;
  load_watchlist_rejected_from_state(opts.workspace_root, &watchlist, &rejected);
  PackReviewVerdict stub;
  stub.ok = true;
  stub.verdict = "partial";
  return build_pack_replan_menu(session_md, map_last, watchlist, rejected, stub, distilled,
                                nullptr, nullptr, true);
}

// Returns true when explore should continue (expand after miss); false when covered or skipped.
bool maybe_run_pack_review_after_plan(Level2Session& session, L2Brain& brain,
                                      const Level2AutonomousLoopOpts& opts,
                                      const Level2PhaseLogFn& log, std::atomic<bool>* cancel,
                                      std::string* recover_note) {
  if (!pack_review_enabled()) {
    return false;
  }
  const std::string flags0 = session.status_flags(opts.workspace_root);
  if (flags0.find("has_pack=yes") == std::string::npos) {
    return false;
  }
  if (status_has_pack_review_ok(flags0)) {
    return false;
  }
  int cycles = 0;

  std::string session_md =
      read_path_file(Level2Session::session_path(opts.workspace_root));
  std::string pack_md = read_path_file(Level2Session::pack_path(opts.workspace_root));
  if (pack_md.empty()) {
    return false;
  }
  const std::string instruction = extract_session_instruction_block(session_md);
  const std::string distilled = extract_distilled_intent_block(session_md);

  std::vector<std::string> watchlist;
  std::vector<std::string> rejected;
  std::vector<std::string> used_review_terms;
  const std::string map_last_path = opts.workspace_root + "/.tuide/ai/map_last.md";
  const std::string map_last = read_path_file(map_last_path);
  {
    const std::string st_raw = read_path_file(Level2Session::state_path(opts.workspace_root));
    if (!st_raw.empty()) {
      try {
        const auto j = nlohmann::json::parse(st_raw);
        if (j.contains("watchlist") && j["watchlist"].is_array()) {
          for (const auto& t : j["watchlist"]) {
            if (t.is_string()) {
              watchlist.push_back(t.get<std::string>());
            }
          }
        }
        if (j.contains("rejected_targets") && j["rejected_targets"].is_array()) {
          for (const auto& t : j["rejected_targets"]) {
            if (t.is_string()) {
              rejected.push_back(t.get<std::string>());
            }
          }
        }
        if (j.contains("review_search_terms") && j["review_search_terms"].is_array()) {
          for (const auto& t : j["review_search_terms"]) {
            if (t.is_string()) {
              used_review_terms.push_back(t.get<std::string>());
            }
          }
        }
        if (j.contains("pack_review_cycles")) {
          cycles = j["pack_review_cycles"].get<int>();
        }
      } catch (...) {
      }
    }
  }

  auto build_replan_menu = [&](const PackReviewVerdict& verdict,
                               const std::vector<std::string>& terms,
                               const std::vector<std::string>* hits_override) -> std::string {
    return build_pack_replan_menu(session_md, map_last, watchlist, rejected, verdict, distilled,
                                  hits_override, terms.empty() ? nullptr : &terms, false);
  };

  auto log_priority = [&](const char* label, const std::vector<std::string>& targets) {
    if (!log || targets.empty()) {
      return;
    }
    log(std::string("L2 ▸ prioridad ") + label + " (1=must):");
    const int show = std::min(static_cast<int>(targets.size()), 12);
    for (int i = 0; i < show; ++i) {
      const char* tier = i < kL2MustPlanTargets ? "must" : "tail";
      log("  " + std::to_string(i + 1) + ". [" + tier + "] " +
          targets[static_cast<std::size_t>(i)]);
    }
  };

  const std::vector<std::string> anchors =
      retrieval_anchor_targets(map_last, session_md, 12);
  // Prefer path-bearing watchlist head + map anchors for body checks.
  std::vector<std::string> must_check;
  for (const auto& w : watchlist) {
    if (w.find('/') != std::string::npos) {
      must_check.push_back(w);
    }
    if (must_check.size() >= static_cast<std::size_t>(kL2MustPlanTargets)) {
      break;
    }
  }
  for (const auto& a : anchors) {
    if (a.find('/') != std::string::npos) {
      must_check.push_back(a);
    }
  }
  {
    std::vector<std::string> uniq;
    std::unordered_set<std::string> seen;
    for (const auto& t : must_check) {
      if (seen.insert(t).second) {
        uniq.push_back(t);
      }
    }
    must_check = std::move(uniq);
  }

  const auto missing_bodies = pack_targets_missing_bodies(pack_md, must_check);
  const bool needs_pair = std::any_of(must_check.begin(), must_check.end(),
                                      [](const std::string& t) { return target_is_lifecycle_set(t); });
  const bool pair_ok = !needs_pair || pack_has_lifecycle_pair(pack_md);
  // With a real set+clear pair, 2 must fences are enough; otherwise require 3.
  const int min_ok = (pair_ok && needs_pair) ? 2 : 3;
  const bool anchors_covered = pack_must_anchors_covered(pack_md, must_check, min_ok) && pair_ok;

  // Fence evidence beats 7B undercoverage: skip LLM when must bodies + set/clear pair are present.
  if (anchors_covered) {
    if (log) {
      log("L2 ▸ pack review auto-covered — anclas must con cuerpo en fences");
    }
    session.mark_pack_review(opts.workspace_root, true,
                             "runtime: must anchors have symbol bodies in pack fences");
    return false;
  }

  if (!missing_bodies.empty() || (needs_pair && !pair_ok)) {
    std::vector<std::string> priority = missing_bodies;
    for (const auto& w : watchlist) {
      if (w.find('/') != std::string::npos) {
        priority.push_back(w);
      }
    }
    for (const auto& a : anchors) {
      if (a.find('/') != std::string::npos) {
        priority.push_back(a);
      }
    }
    {
      const auto sibs =
          expand_anchor_api_siblings(priority, map_last, 8, opts.workspace_root);
      // Clear/cancel siblings first so pre-review pack budget prefers them.
      std::vector<std::string> clears;
      std::vector<std::string> other;
      for (const auto& s : sibs) {
        if (target_is_lifecycle_clear(s)) {
          clears.push_back(s);
        } else {
          other.push_back(s);
        }
      }
      priority.insert(priority.begin(), other.begin(), other.end());
      priority.insert(priority.begin(), clears.begin(), clears.end());
    }
    // Prefer clear/cancel locus, then path:line, then src/.
    std::stable_sort(priority.begin(), priority.end(),
                     [](const std::string& a, const std::string& b) {
                       auto score = [](const std::string& t) {
                         int s = 0;
                         if (target_is_lifecycle_clear(t)) {
                           s += 50;
                         }
                         if (target_is_lifecycle_set(t)) {
                           s += 40;
                         }
                         const auto colon = t.rfind(':');
                         if (colon != std::string::npos && colon + 1 < t.size()) {
                           bool digits = true;
                           for (std::size_t i = colon + 1; i < t.size(); ++i) {
                             if (!std::isdigit(static_cast<unsigned char>(t[i]))) {
                               digits = false;
                               break;
                             }
                           }
                           if (digits) {
                             s += 20;
                           }
                         }
                         if (t.find("src/") != std::string::npos) {
                           s += 5;
                         }
                         return s;
                       };
                       return score(a) > score(b);
                     });
    {
      std::vector<std::string> uniq;
      std::unordered_set<std::string> seen;
      for (const auto& t : priority) {
        if (t.empty() || !seen.insert(t).second) {
          continue;
        }
        uniq.push_back(t);
      }
      priority = std::move(uniq);
    }
    if (priority.size() > 10) {
      priority.resize(10);  // keep clear/set head; avoid budget thrash on noise tails
    }
    if (!priority.empty()) {
      if (log) {
        log("L2 ▸ pack pre-review: refetch anclas sin cuerpo en fences");
      }
      session.unreject_matching(opts.workspace_root, anchors);
      log_priority("pre-review anclas", priority);
      const auto tr = session.apply_plan(opts.workspace_root, priority,
                                         "runtime: pre-review anclas path:line");
      if (log) {
        log(std::string("L2 ▸ pack pre-review ") + (tr.ok ? "OK" : "FAIL") + " — " +
            tr.summary.substr(0, 160));
      }
      pack_md = read_path_file(Level2Session::pack_path(opts.workspace_root));
      session_md = read_path_file(Level2Session::session_path(opts.workspace_root));
      watchlist.clear();
      rejected.clear();
      load_watchlist_rejected_from_state(opts.workspace_root, &watchlist, &rejected);
      const bool pair_ok2 = !needs_pair || pack_has_lifecycle_pair(pack_md);
      const int min_ok2 = (pair_ok2 && needs_pair) ? 2 : 3;
      if (pack_must_anchors_covered(pack_md, must_check, min_ok2) && pair_ok2) {
        if (log) {
          log("L2 ▸ pack review auto-covered — anclas must con cuerpo tras refetch");
        }
        session.mark_pack_review(opts.workspace_root, true,
                                 "runtime: must anchors covered after pre-review refetch");
        return false;
      }
    }
  }

  const std::string digest = build_pack_digest(pack_md);

  if (cycles >= kMaxPackReviewCycles) {
    if (!pack_has_anchor_fragment(pack_md, anchors)) {
      std::vector<std::string> priority = plan_targets_from_map_hits(
          ranked_map_unseen_hits(map_last, {}, {}, 8), 6);
      if (priority.empty()) {
        for (const auto& a : anchors) {
          if (a.find('/') != std::string::npos) {
            priority.push_back(a);
          }
          if (priority.size() >= 4) {
            break;
          }
        }
      }
      if (!priority.empty()) {
        if (log) {
          log("L2 ▸ pack review: max ciclos sin anclas en pack — rescue refetch");
        }
        session.unreject_matching(opts.workspace_root, anchors);
        session.reset_watchlist_priority(opts.workspace_root, priority, true);
        log_priority("rescue mapa", priority);
        const auto tr = session.apply_plan(opts.workspace_root, priority,
                                           "runtime: rescue anclas mapa L1");
        if (log) {
          log(std::string("L2 ▸ pack rescue ") + (tr.ok ? "OK" : "FAIL") + " — " +
              tr.summary.substr(0, 160));
        }
        load_watchlist_rejected_from_state(opts.workspace_root, &watchlist, &rejected);
        if (recover_note) {
          PackReviewVerdict stub;
          stub.ok = true;
          stub.verdict = "partial";
          stub.reason = "rescue anchors after max pack_review_cycles";
          *recover_note = build_replan_menu(stub, {}, nullptr);
        }
        return true;
      }
    }
    if (log) {
      log("L2 ▸ pack review: max ciclos sin covered — replan desde mapa");
    }
    PackReviewVerdict stub;
    stub.ok = true;
    stub.verdict = "partial";
    stub.reason = "max pack_review_cycles";
    if (recover_note) {
      *recover_note = build_replan_menu(stub, {}, nullptr);
    }
    return true;
  }

  if (log) {
    log("L2 ▸ pack review — L2 juzga cobertura semántica ES/EN…");
  }
  L2BrainRequest breq;
  breq.system_prompt = pack_review_system_prompt();
  breq.user_prompt =
      pack_review_user_prompt(instruction, distilled, digest, watchlist);
  breq.phase = "explore";
  breq.max_tokens = 420;
  breq.n_ctx = std::min(opts.budget.n_ctx, 4096);
  breq.temperature = 0.05f;
  const L2BrainResult br = brain.propose(breq, cancel);
  if (!br.ok) {
    if (log) {
      log("L2 ▸ pack review error: " + br.error);
    }
    return false;
  }
  const PackReviewVerdict verdict = parse_pack_review_json(br.text);
  if (log) {
    log("L2 ▸ pack review verdict=" + verdict.verdict +
        (verdict.reason.empty() ? "" : (" — " + verdict.reason.substr(0, 120))));
  }
  if (!verdict.ok) {
    if (log) {
      log("L2 ▸ pack review parse fail");
    }
    return false;
  }
  {
    // Don't accept LLM covered without fence evidence; do accept covered when fences OK
    // even if the 7B stays on partial (undercoverage thrash in decond runs).
    const bool pair_ok_gate = !needs_pair || pack_has_lifecycle_pair(pack_md);
    const int min_ok_gate = (pair_ok_gate && needs_pair) ? 2 : 3;
    const bool fences_ok =
        pack_must_anchors_covered(pack_md, must_check, min_ok_gate) && pair_ok_gate;
    if (verdict.verdict == "covered") {
      if (!fences_ok) {
        if (log) {
          log("L2 ▸ pack review covered rechazado — anclas must sin cuerpo o sin set/clear");
        }
      } else {
        session.mark_pack_review(opts.workspace_root, true, verdict.reason);
        return false;
      }
    } else if (fences_ok) {
      if (log) {
        log("L2 ▸ pack review " + verdict.verdict +
            " → covered por anclas must (override undercoverage)");
      }
      session.mark_pack_review(opts.workspace_root, true,
                               "runtime: fence override after LLM " + verdict.verdict);
      return false;
    }
  }

  std::ostringstream summary;
  summary << "verdict=" << verdict.verdict;
  if (!verdict.reason.empty()) {
    summary << " reason=" << verdict.reason;
  }
  if (!verdict.present.empty()) {
    summary << "\npresent:";
    for (const auto& p : verdict.present) {
      summary << ' ' << p;
    }
  }
  if (!verdict.missing.empty()) {
    summary << "\nmissing:";
    for (const auto& m : verdict.missing) {
      summary << ' ' << m;
    }
  }
  session.mark_pack_review(opts.workspace_root, false, summary.str());

  // P4 capa 4: pack miss/partial may name paths outside loci → micro-A allowlist.
  if (l2_feat::enabled("L2_EXPLORE_PHASE_A")) {
    std::vector<std::string> allow;
    for (const auto& m : verdict.missing) {
      if (m.find('/') != std::string::npos || m.find(".hpp") != std::string::npos ||
          m.find(".cpp") != std::string::npos || m.find(".h") != std::string::npos) {
        allow.push_back(m);
      }
    }
    for (const auto& m : verdict.present) {
      (void)m;
    }
    if (!allow.empty()) {
      const auto ma = session.allow_micro_a_paths(opts.workspace_root, allow);
      if (log && ma.ok) {
        log("L2 ▸ micro-A allow — " + ma.summary);
      }
    }
  }

  std::vector<std::string> reject_extra = verdict.reject;
  {
    const auto invented = infer_invented_rejects(verdict, map_last);
    reject_extra.insert(reject_extra.end(), invented.begin(), invented.end());
  }
  reject_extra = expand_review_rejects_for_watchlist(reject_extra, watchlist);
  // Never denylist L1 map/seed anchors (ai_controller, set_busy_*, …) nor must-tier head.
  {
    std::vector<std::string> protected_targets = anchors;
    if (!watchlist.empty()) {
      const std::size_t n =
          std::min(watchlist.size(), static_cast<std::size_t>(kL2MustPlanTargets));
      protected_targets.insert(protected_targets.end(), watchlist.begin(),
                               watchlist.begin() + static_cast<std::ptrdiff_t>(n));
    }
    reject_extra = filter_rejects_excluding_anchors(reject_extra, protected_targets);
    // decond lesson: 7B prune often drops clear_/cancel_ locus as "noise".
    {
      std::vector<std::string> kept;
      for (const auto& r : reject_extra) {
        if (target_is_lifecycle_clear(r) || target_is_lifecycle_set(r)) {
          continue;
        }
        kept.push_back(r);
      }
      reject_extra = std::move(kept);
    }
  }
  if (!reject_extra.empty()) {
    const auto pr = session.prune_watchlist_after_review(opts.workspace_root, reject_extra);
    if (log && pr.ok) {
      log("L2 ▸ pack prune — " + pr.summary);
    }
    const std::string st_raw = read_path_file(Level2Session::state_path(opts.workspace_root));
    if (!st_raw.empty()) {
      try {
        const auto j = nlohmann::json::parse(st_raw);
        watchlist.clear();
        rejected.clear();
        if (j.contains("watchlist") && j["watchlist"].is_array()) {
          for (const auto& t : j["watchlist"]) {
            if (t.is_string()) {
              watchlist.push_back(t.get<std::string>());
            }
          }
        }
        if (j.contains("rejected_targets") && j["rejected_targets"].is_array()) {
          for (const auto& t : j["rejected_targets"]) {
            if (t.is_string()) {
              rejected.push_back(t.get<std::string>());
            }
          }
        }
      } catch (...) {
      }
    }
  }

  if (!watchlist.empty()) {
    log_priority("watchlist post-review", watchlist);
  }

  const std::vector<std::string> raw_terms = review_search_terms(verdict, distilled, 3);
  const std::vector<std::string> terms =
      filter_unused_review_search_terms(raw_terms, used_review_terms);
  std::vector<L2ToolCall> searches;
  for (const auto& t : terms) {
    searches.push_back(L2ToolCall{"search", t});
  }
  std::string search_blob;
  if (!searches.empty()) {
    if (log) {
      log("L2 ▸ review expand — search EN " + std::to_string(searches.size()) + " términos");
    }
    const auto tr_search = session.apply_tools(opts.workspace_root, searches);
    search_blob = tr_search.summary;
    session.add_review_search_terms(opts.workspace_root, terms);
    const std::string session_after = read_path_file(Level2Session::session_path(opts.workspace_root));
    const auto obs = session_after.find("## Observations");
    if (obs != std::string::npos) {
      search_blob += "\n" + session_after.substr(obs);
    }
  } else if (log && !raw_terms.empty()) {
    log("L2 ▸ review expand — términos ya buscados, omitiendo grep repetido");
  }

  std::vector<std::string> hit_menu = parse_search_hits_menu(search_blob, 12);
  {
    std::vector<std::string> blocklist = watchlist;
    blocklist.insert(blocklist.end(), rejected.begin(), rejected.end());
    hit_menu = filter_search_hits_excluding_watchlist(hit_menu, blocklist);
  }
  if (hit_menu.empty()) {
    if (!map_last.empty()) {
      hit_menu = ranked_map_replan_hits(map_last, watchlist, rejected, verdict, distilled, 12);
    }
    if (hit_menu.empty()) {
      std::vector<std::string> blocklist = watchlist;
      blocklist.insert(blocklist.end(), rejected.begin(), rejected.end());
      hit_menu = ranked_map_fallback_hits(session_md, blocklist, verdict, distilled, 10);
    }
    if (log && !hit_menu.empty()) {
      log("L2 ▸ review expand — map replan " + std::to_string(hit_menu.size()) + " entradas");
    }
  }
  std::stable_sort(hit_menu.begin(), hit_menu.end(), [](const std::string& a, const std::string& b) {
    const bool aa = a.find("src/ai/") != std::string::npos;
    const bool ba = b.find("src/ai/") != std::string::npos;
    if (aa != ba) {
      return aa > ba;
    }
    return a < b;
  });

  {
    const auto suggested = plan_targets_from_map_hits(hit_menu, 8);
    if (!suggested.empty()) {
      log_priority("sugerida post-review (MAP/SEARCH)", suggested);
    }
  }

  if (recover_note) {
    *recover_note = build_replan_menu(verdict, terms, &hit_menu);
  }
  return true;
}

}  // namespace

Level2AutonomousLoopResult run_level2_autonomous(Level2Session& session, L2Brain& brain,
                                                 const Level2AutonomousLoopOpts& opts,
                                                 const Level2PhaseLogFn& log,
                                                 std::atomic<bool>* cancel) {
  using clock = std::chrono::steady_clock;
  Level2AutonomousLoopResult result;
  const int max_steps = opts.settings.max_steps > 0 ? opts.settings.max_steps : 32;
  const AiWorkflowKind workflow = parse_ai_workflow_kind(opts.workflow);
  const L2ContextBudget& budget = opts.budget;
  const auto run_t0 = clock::now();

  session.set_context_budget(budget);
  {
    std::string trim_note;
    if (session.maybe_trim_pack_to_budget(opts.workspace_root, &trim_note) && !trim_note.empty()) {
      if (log) {
        log("L2 ▸ " + trim_note);
      }
    }
  }

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
               std::to_string(max_steps) + ",\"n_ctx\":" + std::to_string(budget.n_ctx) +
               ",\"pack_chars\":" + std::to_string(budget.pack_chars) +
               ",\"workflow\":\"" + ai_workflow_kind_name(workflow) + "\"}");

  int consecutive_invalid = 0;
  int consecutive_turn_errors = 0;
  int consecutive_plan_target_pushbacks = 0;
  std::string recover_note;

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
    bool map_stale = false;
    bool pack_incomplete = false;
    bool resume = false;
    bool pack_review_pending = false;
    {
      const auto p = status.find("phase: ");
      if (p != std::string::npos) {
        const auto end = status.find_first_of(" \n", p + 7);
        phase = status.substr(p + 7, end == std::string::npos ? std::string::npos : end - (p + 7));
      }
      has_pack = status.find("has_pack: yes") != std::string::npos;
      map_review = status.find("map_review: yes") != std::string::npos;
      map_stale = status.find("map_stale: yes") != std::string::npos;
      pack_incomplete = status.find("pack_incomplete: yes") != std::string::npos;
      resume = status.find("resume: yes") != std::string::npos;
    }
    if (pack_review_enabled()) {
      const std::string flags = session.status_flags(opts.workspace_root);
      pack_review_pending =
          flags.find("has_pack=yes") != std::string::npos &&
          flags.find("pack_review_ok=yes") == std::string::npos &&
          flags.find("pack_review_cycles=") != std::string::npos &&
          flags.find("pack_review_cycles=0") == std::string::npos;
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
      if (ai_workflow_is_readonly(workflow)) {
        emit("L2 ▸ compile ignorado (workflow=" + std::string(ai_workflow_kind_name(workflow)) +
             ")");
        result.steps = step;
        continue;
      }
      emit(phase_banner(phase, step, max_steps) + " — compilando…");
      const auto t_comp = clock::now();
      const auto tr = session.run_compile(opts.workspace_root);
      ai_trace(AiTraceChannel::L2, "l2_compile_step",
               "{\"step\":" + std::to_string(step) + ",\"ok\":" + (tr.ok ? "1" : "0") +
                   ",\"duration_ms\":" + std::to_string(elapsed_ms(t_comp)) + ",\"phase\":\"" +
                   tr.phase + "\"}");
      emit(std::string("L2 ▸ compile ") + (tr.ok ? "OK" : "FAIL") + " — " + tr.summary);
      result.steps = step;
      result.phase = tr.phase.empty() ? phase : tr.phase;
      if (tr.phase == "done" || tr.phase == "clarify") {
        result.ok = true;
        result.summary = tr.summary;
        return result;
      }
      if (!tr.ok && tr.phase == "compile") {
        emit("L2 ▸ compile stuck → force phase=edit");
        const auto promo = session.force_phase_edit(
            opts.workspace_root, "compile rollback; reemite hunk Aider sobre el baseline");
        if (promo.ok && promo.phase == "edit") {
          result.phase = "edit";
        }
      }
      continue;
    }

    const auto step_t0 = clock::now();
    emit(phase_banner(phase, step, max_steps) + " — pidiendo acción al modelo (" + brain.name() +
         ")…");

    L2BrainRequest breq;
    if (phase == "explore_a") {
      if (tuide::a_effect_summary_enabled()) {
        const auto ast = Level2Session::load_a_state(opts.workspace_root);
        if (ast.a_subphase == "a1_trail") {
          breq.system_prompt =
              "Eres el Nivel 2 en fase explore_a — subfase A1 trail (call-stacks desde L0).\n"
              "Responde SIEMPRE con UN solo objeto JSON. PROHIBIDO markdown/prosa fuera del JSON.\n"
              "\n"
              "## Trail → a_trail_judge\n"
              "interesting = rama cond (`ON`|`CXL`|`OFF`|`LINK`) y/o pila S* que "
              "explique el síntoma de Instruction. reject = falso positivo / ruido.\n"
              "{\"action\":\"a_trail_judge\",\"verdicts\":["
              "{\"target\":\"ON\",\"verdict\":\"interesting\",\"why\":\"caller del L0\"},"
              "{\"target\":\"S1\",\"verdict\":\"reject\",\"why\":\"no cuadra con el síntoma\"}]}\n"
              "Tras interesting el runtime pedirá suspect vars → dataflow.\n";
        } else if (ast.a_subphase == "a1_suspect_vars") {
          breq.system_prompt =
              "Eres el Nivel 2 en fase explore_a — subfase A1 suspect vars (post-trail).\n"
              "Responde SIEMPRE con UN solo objeto JSON.\n"
              "\n"
              "## Pilas interesting → a_judge phase=a1_suspect_vars\n"
              "¿Variable/campo C++ en el snippet que controla el síntoma de Instruction? Máx 2.\n"
              "{\"action\":\"a_judge\",\"phase\":\"a1_suspect_vars\",\"verdicts\":["
              "{\"target\":\"path:Symbol\",\"verdict\":\"expand\","
              "\"expand_with\":\"dataflow\",\"suspect_var\":\"campo_\","
              "\"why\":\"estado que explica el síntoma\"}],\"done\":false}\n"
              "Si ninguna clara → verdicts:[]. Solo vars reales del snippet trail.\n";
        } else if (ast.a_subphase == "a1_dataflow") {
          breq.system_prompt =
              "Eres el Nivel 2 en fase explore_a — subfase A1 dataflow (scoped + trail recap).\n"
              "Responde SIEMPRE con UN solo objeto JSON.\n"
              "\n"
              "## Dataflow + trail → a_judge\n"
              "El prompt incluye pilas interesting Y reporte rg scoped al caller.\n"
              "useful solo si hits explican el síntoma EN ESA RAMA (coherente con trail).\n"
              "reject si la var no cuadra o hits irrelevantes → runtime reabre trail.\n"
              "Máx 1 useful/vuelta.\n";
        } else if (ast.a_subphase.rfind("a1_", 0) == 0) {
          breq.system_prompt =
              "Eres el Nivel 2 en fase explore_a — subfase A1 confirmación.\n"
              "Responde SIEMPRE con UN solo objeto JSON.\n"
              "a_judge: useful|reject|uncertain (máx 1 useful). a_done solo con loci "
              "confirmados.\n";
        } else {
          breq.system_prompt =
              "Eres el Nivel 2 en fase explore_a — subfase A0 (Effect Summary / olfateo).\n"
              "Responde SIEMPRE con UN solo objeto JSON. PROHIBIDO markdown/prosa fuera del JSON.\n"
              "PROHIBIDO action=plan, tool, edit, done next=edit, useful en A0.\n"
              "\n"
              "## Fichas → a_judge phase=a0_sniff\n"
              "Juzga por seeds/nudge/hot/writes/calls; stem/map dan contexto L1.\n"
              "nudge = sugerencia determinista (expand:*|likely_glue|likely_noise|weak_seed), "
              "no veredicto.\n"
              "expand = merece peek|trail|dataflow (expand_with). reject = fuera. uncertain = "
              "duda.\n"
              "COBERTURA: el user prompt lista N cards — verdicts[] debe tener EXACTAMENTE N "
              "objetos (un target por card, mismo string).\n"
              "Máximo " +
              std::to_string(tuide::kA0MaxExpandPerTurn) +
              " expand/vuelta; resto reject|uncertain.\n"
              "Respeta nudge/hot/seeds de cada ficha; likely_* → reject salvo seeds fuertes.\n"
              "expand_with según nudge (expand:trail|peek|dataflow). NO dataflow en A0 salvo "
              "nudge explícito.\n"
              "Dataflow solo tras trail + suspect vars en A1.\n"
              "Ejemplo genérico (2 cards; en runtime N puede ser distinto):\n"
              "{\"action\":\"a_judge\",\"phase\":\"a0_sniff\",\"verdicts\":["
              "{\"target\":\"src/foo/module.cpp:sym_a#tail\",\"verdict\":\"expand\","
              "\"expand_with\":\"trail\",\"why\":\"nudge expand:trail + seeds\"},"
              "{\"target\":\"src/lsp/lsp_client.cpp:cancel#tail\",\"verdict\":\"reject\","
              "\"why\":\"likely_lsp_trap, sin seeds\"}],\"done\":false}\n"
              "\n"
              "Tras expand el runtime muestra A1 (una modalidad). Ahí sí useful|reject.\n"
              "a_done solo con loci confirmados post-A1.\n";
        }
      } else {
        breq.system_prompt =
            "Eres el Nivel 2 en fase explore_a (localización + trail).\n"
            "Responde SIEMPRE con UN solo objeto JSON. PROHIBIDO markdown/prosa fuera del JSON.\n"
            "PROHIBIDO action=plan, tool, edit, done next=edit.\n"
            "Objetivo: localizar el síntoma; el EDIT SITE puede salir del trail.\n"
            "\n"
            "## Peeks → a_judge\n"
            "useful = hipótesis débil ligada al síntoma del prompt (estado, flag, API L0). "
            "Getters/flags OK. Máx 1 useful/vuelta; resto reject|uncertain.\n"
            "NO copies textos de ejemplos de trail. why = 1 frase propia del peek.\n"
            "Ejemplo a_judge (targets ficticios):\n"
            "{\"action\":\"a_judge\",\"verdicts\":["
            "{\"target\":\"src/foo/module.cpp:sym_a\",\"verdict\":\"useful\","
            "\"why\":\"muta estado del síntoma según peek\"},"
            "{\"target\":\"src/lsp/lsp_client.cpp:cancel_inflight_completion\",\"verdict\":"
            "\"reject\",\"why\":\"cancel LSP, no el síntoma\"}],"
            "\"done\":false}\n"
            "Reject traps claros (completion LSP, frames cosméticos, glue sin seeds).\n"
            "\n"
            "## Trail (solo si el runtime mostró call-stacks) → a_trail_judge\n"
            "{\"action\":\"a_trail_judge\",\"verdicts\":["
            "{\"target\":\"S2\",\"verdict\":\"interesting\",\"why\":\"caller pone AiThinking\"},"
            "{\"target\":\"S1\",\"verdict\":\"reject\",\"why\":\"reindex no es síntoma IA\"}]}\n"
            "verdict EXACTAMENTE interesting|reject (una palabra). interesting ≤3.\n"
            "a_done cuando un hop del trail es el edit site (≤2 primary).\n"
            "{\"action\":\"a_done\",\"loci\":[{\"stem\":\"…\",\"anchor\":\"path:Symbol\","
            "\"role\":\"primary\",\"why\":\"…\"}],\"summary\":\"…\"}\n";
      }
    } else {
      breq.system_prompt = build_system_prompt(opts, phase, map_review);
    }
    breq.user_prompt =
        build_user_prompt(opts.workspace_root, phase, step, has_pack, map_review, map_stale,
                          pack_incomplete, resume, workflow, budget, opts, recover_note,
                          pack_review_pending);
    if (phase == "explore_a") {
      if (!recover_note.empty()) {
        breq.user_prompt += "\n\n## Recover\n" + recover_note + "\n";
      }
      std::ifstream nin(Level2Session::a_notes_path(opts.workspace_root));
      if (nin) {
        std::ostringstream nss;
        nss << nin.rdbuf();
        const std::string notes = nss.str();
        if (!notes.empty()) {
          breq.user_prompt += "\n\n" + notes;
        }
      }
      {
        tuide::AState ast_snap = Level2Session::load_a_state(opts.workspace_root);
        if (tuide::a_effect_summary_enabled() && tuide::a_in_a0_sniff(ast_snap)) {
          const tuide::A0TrancheShown shown = tuide::a_build_a0_tranche_shown(
              opts.workspace_root, ast_snap, tuide::kA0MaxCardsPerTurn);
          ast_snap.a0_shown_targets.clear();
          for (const auto& item : shown.items) {
            ast_snap.a0_shown_targets.push_back(item.target);
          }
          Level2Session::save_a_state(opts.workspace_root, ast_snap, nullptr);
        }
      }
      breq.user_prompt += "\n" + session.build_a_peek_tranche_markdown(opts.workspace_root);
    } else if (phase == "explore_b" && l2_feat::enabled("L2_EXPLORE_PHASE_A")) {
      const auto ast = Level2Session::load_a_state(opts.workspace_root);
      std::ostringstream brief;
      brief << "\n## Loci (Phase B — pack solo desde aquí)\n";
      for (const auto& loc : ast.loci_draft) {
        brief << "- [" << a_locus_role_name(loc.role) << "] `" << loc.anchor << "`";
        if (!loc.why.empty()) {
          brief << " — " << loc.why;
        }
        brief << "\n";
      }
      if (!ast.b_allow_paths.empty()) {
        brief << "micro-A allow: ";
        for (std::size_t i = 0; i < ast.b_allow_paths.size(); ++i) {
          if (i) {
            brief << ", ";
          }
          brief << "`" << ast.b_allow_paths[i] << "`";
        }
        brief << "\n";
      }
      brief << "Preferir plan vacío (runtime usa watchlist) o targets de loci. "
               "PROHIBIDO multi-stem fuera de loci.\n";
      breq.user_prompt += brief.str();
    }
    breq.phase = phase;
    breq.max_tokens = opts.settings.max_tokens;
    breq.n_ctx = budget.n_ctx;
    breq.temperature = opts.settings.temperature;
    // Lean edit uses Aider fences; GBNF JSON de hunks se atasca en prompts reales ("hunks":[�).
    const bool skip_grammar =
        (phase == "edit" && l2_feat::enabled("EDIT_LEAN_PROMPT")) || consecutive_invalid >= 2;
    if (!skip_grammar) {
      breq.grammar_file = l2_grammar::resolve_for_phase(opts.workspace_root, phase);
    }
    if (!breq.grammar_file.empty()) {
      emit("L2 ▸ grammar=" + breq.grammar_file);
    }

    const auto propose_t0 = clock::now();
    const L2BrainResult br = brain.propose(breq, cancel);
    const auto propose_ms = elapsed_ms(propose_t0);
    ai_trace(AiTraceChannel::L2, "l2_propose",
             "{\"step\":" + std::to_string(step) + ",\"phase\":\"" + phase + "\",\"backend\":\"" +
                 ai_trace_escape(brain.name()) + "\",\"ok\":" + (br.ok ? "1" : "0") +
                 ",\"duration_ms\":" + std::to_string(propose_ms) + ",\"prompt_chars\":" +
                 std::to_string(breq.system_prompt.size() + breq.user_prompt.size()) +
                 ",\"reply_chars\":" + std::to_string(br.text.size()) +
                 ",\"grammar\":" + (breq.grammar_file.empty() ? "0" : "1") +
                 (br.ok ? "" : (",\"error\":\"" + ai_trace_escape(br.error) + "\"")) + "}");
    if (!br.ok) {
      emit("L2 ▸ modelo error: " + br.error);
      result.error = br.error;
      result.phase = phase;
      result.steps = step;
      // Soft retry next step unless cancel.
      continue;
    }

    L2Action action = parse_l2_action(br.text);
    // explore_a: empty/unknown top-level actions (seeds, blank) → empty a_judge (fail-soft advance).
    if (phase == "explore_a" &&
        (action.kind == L2ActionKind::Unknown || action.kind == L2ActionKind::Error)) {
      const std::string raw = action.raw;
      const bool looks_seeds =
          raw.find("\"action\":\"seeds\"") != std::string::npos ||
          raw.find("\"seeds\"") != std::string::npos;
      const bool blank_action =
          action.error.find("action desconocida:") != std::string::npos ||
          action.error.find("sin objeto JSON") != std::string::npos;
      if (looks_seeds || blank_action) {
        emit("L2 ▸ coerce " + action.error.substr(0, 40) + " → empty a_judge");
        action = L2Action{};
        action.kind = L2ActionKind::AJudge;
        action.a_verdicts.clear();
      }
    }
    emit(std::string("L2 ▸ acción=") + l2_action_kind_name(action.kind) +
         (action.name.empty() ? "" : (" name=" + action.name)) +
         (action.error.empty() ? "" : (" err=" + action.error)));

    Level2TurnResult tr;
    const auto action_t0 = clock::now();
    const bool lean_closeout = l2_feat::enabled("EDIT_LEAN_PROMPT") && map_review &&
                               !ai_workflow_is_readonly(workflow);
    auto finish_auto_done = [&](const std::string& why) {
      emit("L2 ▸ closeout auto-done — " + why);
      const auto fin = session.mark_done(opts.workspace_root,
                                         "Instruction cubierta; compile OK", "");
      Level2AutonomousLoopResult r;
      r.ok = true;
      r.phase = fin.phase.empty() ? "done" : fin.phase;
      r.summary = fin.summary.empty() ? why : fin.summary;
      r.steps = step;
      return r;
    };
    if (action.kind == L2ActionKind::Error || action.kind == L2ActionKind::Unknown) {
      emit("L2 ▸ acción inválida; reintentando. " + action.error);
      emit("L2 ▸ dump /tmp/tuide-llama-last.out");
      result.steps = step;
      ++consecutive_invalid;
      consecutive_turn_errors = 0;
      if (lean_closeout) {
        return finish_auto_done("respuesta inválida tras Instruction cubierta");
      }
      if (phase == "explore_a") {
        const tuide::AState ast_inv = Level2Session::load_a_state(opts.workspace_root);
        std::ostringstream rec;
        rec << "**JSON/acción inválida** (intento " << consecutive_invalid
            << "/6): " << action.error.substr(0, 200) << "\n";
        if (tuide::a_in_a0_sniff(ast_inv)) {
          rec << "Emite SOLO {\"action\":\"a_judge\",\"phase\":\"a0_sniff\",\"verdicts\":[…N "
                 "cards…]}.\n";
        } else if (ast_inv.trail.active && ast_inv.trail.awaiting_judge) {
          rec << "Emite SOLO {\"action\":\"a_trail_judge\",\"verdicts\":["
                 "{\"target\":\"S1\",\"verdict\":\"interesting|reject\",\"why\":\"…\"}]}.\n";
        } else if (ast_inv.a_subphase == "a1_dataflow") {
          rec << "Emite SOLO {\"action\":\"a_judge\",\"verdicts\":[{\"target\":\""
              << (ast_inv.a1_active.target.empty() ? "path:Symbol" : ast_inv.a1_active.target)
              << "\",\"verdict\":\"useful|reject\",\"why\":\"…\"}]} o a_done con loci.\n";
        } else {
          rec << "Emite SOLO a_judge / a_trail_judge / a_done (PROHIBIDO seeds/plan/tool/"
                 "reject suelto).\n";
        }
        recover_note = rec.str();
      } else if (l2_feat::enabled("EDIT_LEAN_PROMPT")) {
        std::ostringstream rec;
        rec << "**JSON inválido** (intento " << consecutive_invalid
            << "/6). El parser falló:\n```\n"
            << action.error.substr(0, 280) << "\n```\n";
        if (phase == "edit") {
          rec << "Reemite un hunk Aider, NO JSON:\n"
                 "src/foo.cpp\n<<<<<<< SEARCH\nspan del pack\n=======\nnuevo\n"
                 ">>>>>>> REPLACE\nUn path, search corto. No copies observations.\n";
        } else {
          rec << "Reemite UN objeto JSON válido. En explore: "
                 "{\"action\":\"plan\",\"targets\":[\"path:Symbol\",…]} o "
                 "{\"action\":\"done\",\"summary\":\"…\",\"next\":\"edit\"}. "
                 "PROHIBIDO markdown/prosa.\n";
        }
        recover_note = rec.str();
      }
      if (consecutive_invalid >= 6) {
        emit("L2 ▸ demasiadas acciones inválidas seguidas — cerrando en clarify");
        const auto fin = session.mark_done(
            opts.workspace_root,
            "loop: demasiadas respuestas JSON inválidas; ¿reformulas el cambio?", "clarify");
        result.ok = true;
        result.phase = fin.phase.empty() ? "clarify" : fin.phase;
        result.summary = fin.summary;
        result.steps = step;
        return result;
      }
      continue;
    }
    if (action.kind == L2ActionKind::Tool || action.kind == L2ActionKind::Tools) {
      if (phase == "explore_a") {
        emit("L2 ▸ tool ignorado en explore_a — emite a_judge/a_done");
        recover_note =
            "Fase explore_a: PROHIBIDO tool/plan. Juzga los peeks con "
            "{\"action\":\"a_judge\",\"verdicts\":[…]} o cierra con a_done.\n";
        ++consecutive_invalid;
        result.steps = step;
        continue;
      }
      if (lean_closeout) {
        return finish_auto_done("tool tras Instruction cubierta");
      }
      if (phase == "edit" && has_pack && !pack_incomplete && !map_review &&
          !ai_workflow_is_readonly(workflow)) {
        emit("L2 ▸ tool ignorado en phase=edit — pide hunk Aider");
        ++consecutive_invalid;
        recover_note =
            "Pack cubierto. Emite un hunk Aider ahora (path + SEARCH/REPLACE).\n"
            "PROHIBIDO JSON: ni plan, ni tool, ni sibling_of.\n"
            "Empieza con un path del pack y la marca SEARCH de Aider.\n";
        if (consecutive_invalid >= 6) {
          emit("L2 ▸ demasiados tools en phase=edit — cerrando en clarify");
          const auto fin = session.mark_done(
              opts.workspace_root,
              "loop: phase=edit sin hunk; ¿reformulas el cambio?", "clarify");
          result.ok = true;
          result.phase = fin.phase.empty() ? "clarify" : fin.phase;
          result.summary = fin.summary;
          result.steps = step;
          return result;
        }
        result.steps = step;
        continue;
      }
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
      if ((tr.error == "post_pack_tool_pushback" || tr.error == "repeated_tool") &&
          phase == "explore" && has_pack && !pack_incomplete &&
          !ai_workflow_is_readonly(workflow)) {
        const std::string rv_flags = session.status_flags(opts.workspace_root);
        if (pack_review_enabled() && !status_has_pack_review_ok(rv_flags)) {
          emit("L2 ▸ tools tras pack — esperando pack review");
          result.steps = step;
          continue;
        }
        emit("L2 ▸ tool loop tras pack → force phase=edit (" + tr.error + ")");
        const auto promo = session.force_phase_edit(
            opts.workspace_root,
            "anti-loop: mismo get_code_of / extras tras pack cubierto; emite action=edit");
        if (promo.ok && promo.phase == "edit") {
          tr.phase = "edit";
          phase = "edit";
          recover_note =
              "Pack cubierto. Emite un hunk Aider ahora (path + SEARCH/REPLACE).\n"
              "PROHIBIDO JSON: ni plan, ni tool, ni sibling_of.\n"
              "Empieza con un path del pack y la marca SEARCH de Aider.\n";
        }
      }
    } else if (action.kind == L2ActionKind::Plan) {
      if (phase == "explore_a") {
        emit("L2 ▸ plan rechazado en explore_a — usa a_judge/a_done");
        recover_note =
            "Fase explore_a: no hay pack todavía. Emite a_judge o a_done "
            "(no action=plan).\n";
        ++consecutive_invalid;
        result.steps = step;
        continue;
      }
      emit("L2 ▸ plan targets=" + std::to_string(action.targets.size()) +
           (action.summary.empty() ? "" : (" — " + action.summary.substr(0, 80))));
      for (const auto& t : action.targets) {
        emit("  · " + t.substr(0, 100));
      }
      if (lean_closeout) {
        return finish_auto_done("plan tras Instruction cubierta");
      }
      if (phase == "edit" && has_pack && !pack_incomplete && !map_review &&
          !ai_workflow_is_readonly(workflow)) {
        emit("L2 ▸ plan ignorado en phase=edit — pide hunk Aider");
        ++consecutive_invalid;
        recover_note =
            "Pack cubierto. Emite un hunk Aider ahora (path + SEARCH/REPLACE).\n"
            "PROHIBIDO JSON: ni plan, ni tool, ni sibling_of.\n"
            "Empieza con un path del pack y la marca SEARCH de Aider.\n";
        if (consecutive_invalid >= 6) {
          emit("L2 ▸ demasiados plan en phase=edit — cerrando en clarify");
          const auto fin = session.mark_done(
              opts.workspace_root,
              "loop: phase=edit sin hunk; ¿reformulas el cambio?", "clarify");
          result.ok = true;
          result.phase = fin.phase.empty() ? "clarify" : fin.phase;
          result.summary = fin.summary;
          result.steps = step;
          return result;
        }
        result.steps = step;
        continue;
      }
      tr = session.apply_plan(opts.workspace_root, action.targets, action.summary);
      emit(std::string("L2 ▸ pack ") + (tr.ok ? "OK" : "FAIL") + " — " + tr.summary.substr(0, 160));
      if (tr.error == "repeated_plan_targets_pushback" && phase == "explore") {
        emit("L2 ▸ plan repetido sin targets nuevos — pushback");
        ++consecutive_plan_target_pushbacks;
        if (pack_review_enabled()) {
          recover_note = build_pushback_replan_note(opts);
          if (recover_note.find("MAP HITS") != std::string::npos ||
              recover_note.find("SEARCH HITS") != std::string::npos) {
            emit("L2 ▸ pushback replan — menú MAP/SEARCH HITS re-inyectado");
          }
          if (consecutive_plan_target_pushbacks >= kPushbackEscalateAfter) {
            const std::string map_last =
                read_path_file(opts.workspace_root + "/.tuide/ai/map_last.md");
            std::vector<std::string> watchlist;
            std::vector<std::string> rejected;
            load_watchlist_rejected_from_state(opts.workspace_root, &watchlist, &rejected);
            const auto hits = ranked_map_unseen_hits(map_last, watchlist, rejected, 8);
            const auto auto_targets = plan_targets_from_map_hits(hits, 3);
            if (!auto_targets.empty()) {
              emit("L2 ▸ pushback escalación — auto-plan runtime targets=" +
                   std::to_string(auto_targets.size()));
              for (const auto& t : auto_targets) {
                emit("  · " + t);
              }
              tr = session.apply_plan(opts.workspace_root, auto_targets,
                                      "runtime: pushback escalación MAP HITS");
              consecutive_plan_target_pushbacks = 0;
              emit(std::string("L2 ▸ pack ") + (tr.ok ? "OK" : "FAIL") + " — " +
                   tr.summary.substr(0, 160));
              if (tr.error == "repeated_plan_targets_pushback") {
                result.steps = step;
                continue;
              }
              if (tr.ok) {
                const std::string st_flags = session.status_flags(opts.workspace_root);
                if (st_flags.find("has_pack=yes") != std::string::npos) {
                  if (maybe_run_pack_review_after_plan(session, brain, opts, log, cancel,
                                                       &recover_note)) {
                    phase = "explore";
                    tr.phase = "explore";
                    result.steps = step;
                    continue;
                  }
                  // Battery / stop_at_explore: control pack + review ok → promote to edit.
                  const std::string flags_after =
                      session.status_flags(opts.workspace_root);
                  if (opts.stop_at_explore && status_has_pack_review_ok(flags_after)) {
                    emit("L2 ▸ pack review OK → force phase=edit (stop_at_explore)");
                    const auto promo = session.force_phase_edit(
                        opts.workspace_root,
                        "pack review OK; explore complete — emit action=edit");
                    if (promo.ok) {
                      tr.phase = "edit";
                      phase = "edit";
                    }
                  }
                }
              }
              result.steps = step;
              continue;
            }
          }
        } else {
          recover_note =
              "Todos los targets ya están en el pack. Emite `action=plan` con paths NUEVOS "
              "de SEARCH HITS (src/ai/…). Prioriza control de carga/cancelación del chat IA.\n";
        }
        result.steps = step;
        continue;
      }
      consecutive_plan_target_pushbacks = 0;
      if (tr.ok) {
        const std::string st_flags = session.status_flags(opts.workspace_root);
        if (st_flags.find("has_pack=yes") != std::string::npos) {
          if (maybe_run_pack_review_after_plan(session, brain, opts, log, cancel, &recover_note)) {
            phase = "explore";
            tr.phase = "explore";
            result.steps = step;
            continue;
          }
          const std::string flags_after = session.status_flags(opts.workspace_root);
          if (opts.stop_at_explore && status_has_pack_review_ok(flags_after)) {
            emit("L2 ▸ pack review OK → force phase=edit (stop_at_explore)");
            const auto promo = session.force_phase_edit(
                opts.workspace_root, "pack review OK; explore complete — emit action=edit");
            if (promo.ok) {
              tr.phase = "edit";
              phase = "edit";
            }
          }
        }
      }
      if (tr.error == "repeated_plan_pushback" && phase == "explore" &&
          !ai_workflow_is_readonly(workflow)) {
        emit("L2 ▸ plan loop tras pack → force phase=edit (" + tr.error + ")");
        const auto promo = session.force_phase_edit(
            opts.workspace_root,
            "anti-loop: plan repetido con pack cubierto; emite action=edit");
        if (promo.ok && promo.phase == "edit") {
          tr.phase = "edit";
          phase = "edit";
          recover_note =
              "Pack cubierto. Emite un hunk Aider ahora (path + SEARCH/REPLACE).\n"
              "PROHIBIDO JSON: ni plan, ni tool, ni sibling_of.\n"
              "Empieza con un path del pack y la marca SEARCH de Aider.\n";
        }
      }
    } else if (action.kind == L2ActionKind::AJudge ||
               action.kind == L2ActionKind::ATrailJudge) {
      // Route by live subphase: coerced reject/interesting may land on the wrong kind.
      if (phase == "explore_a") {
        const tuide::AState ast_route = Level2Session::load_a_state(opts.workspace_root);
        // Only awaiting_judge counts — leftover pending_stacks after interesting must NOT
        // coerce a_judge (dataflow/peek) into a_trail_judge.
        const bool trail_waiting =
            ast_route.trail.active && ast_route.trail.awaiting_judge;
        if (trail_waiting && action.kind == L2ActionKind::AJudge) {
          bool any_trail_v = false;
          for (const auto& v : action.a_verdicts) {
            if (v.verdict == tuide::AVerdictKind::Interesting ||
                v.verdict == tuide::AVerdictKind::Reject ||
                v.verdict == tuide::AVerdictKind::Useful) {
              any_trail_v = true;
              break;
            }
          }
          if (any_trail_v) {
            action.kind = L2ActionKind::ATrailJudge;
            emit("L2 ▸ coerce a_judge→a_trail_judge (trail awaiting)");
          }
        } else if (!trail_waiting && action.kind == L2ActionKind::ATrailJudge) {
          action.kind = L2ActionKind::AJudge;
          emit("L2 ▸ coerce a_trail_judge→a_judge (no trail awaiting)");
        }
        // Drop stale trail ids (ON/CXL/S*) when not awaiting — they must not become useful peeks.
        if (!trail_waiting && action.kind == L2ActionKind::AJudge) {
          std::vector<tuide::AVerdict> kept;
          int dropped = 0;
          for (const auto& v : action.a_verdicts) {
            if (tuide::a_is_trail_judge_target_id(v.target)) {
              ++dropped;
              continue;
            }
            kept.push_back(v);
          }
          if (dropped > 0) {
            emit("L2 ▸ drop " + std::to_string(dropped) +
                 " stale trail-id verdict(s) outside awaiting");
            action.a_verdicts = std::move(kept);
          }
        }
      }
      if (action.kind == L2ActionKind::AJudge) {
        if (action.a_verdicts.empty()) {
          const tuide::AState ast_empty = Level2Session::load_a_state(opts.workspace_root);
          if (!ast_empty.loci_draft.empty()) {
            recover_note =
                "No hay trail awaiting. Si ya tienes edit site, emite a_done con primary "
                "path:Symbol (elige entre expands/useful de A0/A1; no copies un ejemplo). "
                "Si no, a_judge useful|reject sobre el target A1 activo.\n";
            tr.ok = true;
            tr.phase = phase;
            tr.summary = "nudge a_done from loci_draft";
          }
        }
        if (tr.summary != "nudge a_done from loci_draft") {
          emit("L2 ▸ a_judge verdicts=" + std::to_string(action.a_verdicts.size()));
          for (const auto& v : action.a_verdicts) {
            emit(std::string("  · [") + tuide::a_verdict_kind_name(v.verdict) + "] " +
                 v.target.substr(0, 80));
          }
          tr = session.apply_a_judge(opts.workspace_root, action.a_verdicts, action.a_turn_done);
          emit(std::string("L2 ▸ a_judge ") + (tr.ok ? "OK" : "FAIL") + " — " +
               (tr.ok ? tr.summary : tr.error).substr(0, 200));
          if (!tr.ok && !tr.error.empty()) {
            const tuide::AState ast_rec = Level2Session::load_a_state(opts.workspace_root);
            if (tuide::a_effect_summary_enabled() && tuide::a_in_a0_sniff(ast_rec)) {
              std::ostringstream rec;
              rec << "a_judge A0 rechazado: " << tr.error << "\n";
              rec << "Reemite phase=a0_sniff con EXACTAMENTE "
                  << ast_rec.a0_shown_targets.size()
                  << " veredictos (expand|reject|uncertain; PROHIBIDO useful).\n";
              rec << "Máximo " << tuide::kA0MaxExpandPerTurn
                  << " expand; resto reject|uncertain.\n";
              rec << "Targets que FALTAN en tu JSON anterior:\n";
              for (const auto& t : ast_rec.a0_shown_targets) {
                bool hit = false;
                for (const auto& v : action.a_verdicts) {
                  if (tuide::a_target_matches_verdict_anchor(t, v.target)) {
                    hit = true;
                    break;
                  }
                }
                if (!hit) {
                  rec << "- `" << t << "`\n";
                }
              }
              recover_note = rec.str();
            } else if (ast_rec.a_subphase == "a1_dataflow") {
              recover_note =
                  "a_judge dataflow rechazado: " + tr.error +
                  "\nEmite {\"action\":\"a_judge\",\"verdicts\":[{\"target\":\"" +
                  (ast_rec.a1_active.target.empty() ? "path:Symbol"
                                                    : ast_rec.a1_active.target) +
                  "\",\"verdict\":\"useful|reject\",\"why\":\"…\"}]}.\n"
                  "Si la var explica el síntoma → useful; si no → reject (reabre trail).\n"
                  "Cuando tengas locus: {\"action\":\"a_done\",\"loci\":[{\"stem\":\"…\","
                  "\"anchor\":\"path:Symbol\",\"role\":\"primary\",\"why\":\"…\"}]}.\n";
            } else {
              recover_note =
                  "a_judge rechazado: " + tr.error +
                  "\nReemite a_judge: máx 1 useful (hipótesis→trail), resto reject|uncertain.\n";
            }
          } else if (tr.ok) {
            const tuide::AState ast_ok = Level2Session::load_a_state(opts.workspace_root);
            if (!ast_ok.loci_draft.empty() &&
                (ast_ok.a_subphase == "a1_dataflow" ||
                 tr.summary.find("useful=") != std::string::npos)) {
              recover_note =
                  "Hay candidatos en draft. Si uno explica Instruction, emite a_done "
                  "(primary path:Symbol). Si no, sigue a_judge; no copies un ejemplo.\n";
            } else {
              recover_note.clear();
            }
          }
        }
      } else {
        emit("L2 ▸ a_trail_judge verdicts=" + std::to_string(action.a_verdicts.size()));
        tr = session.apply_a_trail_judge(opts.workspace_root, action.a_verdicts);
        emit(std::string("L2 ▸ a_trail_judge ") + (tr.ok ? "OK" : "FAIL") + " — " +
             (tr.ok ? tr.summary : tr.error).substr(0, 200));
        if (!tr.ok && !tr.error.empty()) {
          if (tr.error.find("no hay trail activa") != std::string::npos ||
              tr.error.find("esperando juicio") != std::string::npos) {
            // Stale a_trail_judge after suspect/dataflow — soft recover, do not burn fusible.
            const tuide::AState ast_stale = Level2Session::load_a_state(opts.workspace_root);
            recover_note = "Trail no está awaiting. NO emitas a_trail_judge ahora.\n";
            if (ast_stale.a_subphase == "a1_dataflow" ||
                (ast_stale.a1_active_set &&
                 ast_stale.a1_active.modality == tuide::AExpandModality::Dataflow)) {
              recover_note +=
                  "Estás en A1 dataflow: emite a_judge useful|reject sobre `" +
                  (ast_stale.a1_active.target.empty() ? "la var activa"
                                                      : ast_stale.a1_active.target) +
                  "`.\n"
                  "Si ya tienes edit site: a_done con loci (primary anchor path:Symbol).\n";
            } else if (ast_stale.a_subphase == "a1_suspect_vars") {
              recover_note +=
                  "Emite a_judge phase=a1_suspect_vars (expand+dataflow) o verdicts:[].\n";
            } else {
              recover_note +=
                  "Emite a_judge / a_done según la modalidad A1 actual del prompt.\n";
            }
            tr.ok = true;
            tr.error.clear();
            emit("L2 ▸ a_trail_judge stale → soft recover (no fusible)");
          } else {
            recover_note = "a_trail_judge rechazado: " + tr.error +
                           "\nEmite interesting|reject SOLO sobre ids del prompt "
                           "(ON|CXL|OFF|LINK|S1…); PROHIBIDO nombres de símbolo A0.\n";
          }
        } else if (tr.ok) {
          const tuide::AState ast_tr = Level2Session::load_a_state(opts.workspace_root);
          if (!ast_tr.loci_draft.empty()) {
            recover_note =
                "Hay candidatos en draft. Si uno es el edit site de Instruction, a_done; "
                "si no, a_judge (no trail_judge).\n";
          } else {
            recover_note.clear();
          }
        }
      }
    } else if (action.kind == L2ActionKind::ADone) {
      emit("L2 ▸ a_done loci=" + std::to_string(action.a_loci.size()) + " — " +
           action.summary.substr(0, 100));
      tr = session.apply_a_done(opts.workspace_root, action.a_loci, action.summary);
      emit(std::string("L2 ▸ a_done ") + (tr.ok ? "OK → " : "FAIL — ") +
           (tr.ok ? tr.phase : tr.error).substr(0, 160));
      if (!tr.ok && !tr.error.empty()) {
        recover_note = "a_done rechazado: " + tr.error + "\n";
      }
      if (tr.ok && opts.stop_at_phase_a) {
        result.ok = true;
        result.phase = "explore_a_ok";
        result.summary = tr.summary.empty() ? action.summary : tr.summary;
        result.steps = step;
        ai_trace(AiTraceChannel::L2, "l2_run_end",
                 std::string("{\"ok\":1,\"phase\":\"explore_a_ok\",\"steps\":") +
                     std::to_string(result.steps) +
                     ",\"total_ms\":" + std::to_string(elapsed_ms(run_t0)) + "}");
        emit(phase_banner("explore_a", step, max_steps) +
             " — Phase A OK (a_done; stop_at_phase_a, sin pack B)");
        return result;
      }
      if (tr.ok && l2_feat::enabled("L2_EXPLORE_PHASE_A")) {
        // P4: auto-plan from loci watchlist (must-tier) → pack.
        emit("L2 ▸ explore_b auto-plan desde loci…");
        const auto plan_tr =
            session.apply_plan(opts.workspace_root, {}, "auto from loci (phase B)");
        emit(std::string("L2 ▸ auto-plan ") + (plan_tr.ok ? "OK" : "FAIL") + " — " +
             (plan_tr.ok ? plan_tr.summary : plan_tr.error).substr(0, 160));
        if (plan_tr.ok) {
          tr = plan_tr;
        }
      }
    } else if (action.kind == L2ActionKind::Done) {
      emit("L2 ▸ done next=" + (action.next.empty() ? "(none)" : action.next) + " — " +
           action.summary.substr(0, 120));
      std::string next = action.next;
      if (lean_closeout && (next == "clarify" || next == "abort" || next == "need_info")) {
        next.clear();
      }
      tr = session.mark_done(opts.workspace_root, action.summary, next);
      if (tr.summary.find("clarify_pushback") != std::string::npos) {
        emit("L2 ▸ clarify prematuro → " + tr.summary + " (pide más código)");
      }
      if (tr.summary.find("pack_incomplete_pushback") != std::string::npos) {
        emit("L2 ▸ pack incompleto → " + tr.summary + " (refetch truncados)");
      }
    } else if (action.kind == L2ActionKind::Synthesize) {
      emit("L2 ▸ synthesize — " + action.summary.substr(0, 120));
      tr = session.apply_synthesize(opts.workspace_root, action.summary);
      if (tr.ok && !tr.summary.empty()) {
        emit("L2 ▸ respuesta:");
        std::istringstream iss(tr.summary);
        std::string line;
        int lines = 0;
        while (std::getline(iss, line) && lines < 80) {
          emit(line);
          ++lines;
        }
        if (lines >= 80) {
          emit("…(respuesta truncada en consola; ver .tuide/ai/l2/answer.md)");
        }
      }
    } else if (action.kind == L2ActionKind::Edit) {
      if (lean_closeout) {
        return finish_auto_done("edit extra tras Instruction cubierta");
      }
      if (ai_workflow_is_readonly(workflow)) {
        emit("L2 ▸ edit rechazado (workflow=" + std::string(ai_workflow_kind_name(workflow)) +
             "); usa synthesize");
        result.steps = step;
        continue;
      }
      // Opción B: si el modelo salta done next=edit estando en explore, auto-promover.
      // Skip pack_incomplete pushback: the model already emitted hunks.
      if (phase == "explore") {
        emit("L2 ▸ edit en explore → auto phase=edit (skip done next=edit)");
        ai_trace(AiTraceChannel::L2, "l2_auto_phase_edit",
                 "{\"step\":" + std::to_string(step) + ",\"reason\":\"edit_while_explore\"}");
        const auto promo = session.force_phase_edit(
            opts.workspace_root,
            "auto: modelo emitió edit en explore; promoviendo a phase=edit");
        if (!promo.ok || promo.phase != "edit") {
          emit("L2 ▸ auto phase=edit falló: " +
               (promo.error.empty() ? promo.summary : promo.error));
          result.steps = step;
          result.phase = promo.phase.empty() ? phase : promo.phase;
          result.error = promo.error.empty() ? promo.summary : promo.error;
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
      if (!tr.ok && tr.phase == "compile") {
        emit("L2 ▸ compile leftover tras edit → force phase=edit");
        const auto promo = session.force_phase_edit(
            opts.workspace_root, "compile rollback; reemite hunk Aider sobre el baseline");
        if (promo.ok && promo.phase == "edit") {
          tr.phase = "edit";
          phase = "edit";
        }
      }
      if (!tr.ok && l2_feat::enabled("EDIT_LEAN_PROMPT") &&
          (tr.error.find("compile") != std::string::npos ||
           tr.summary.find("compile") != std::string::npos ||
           tr.summary.find("rollback") != std::string::npos)) {
        recover_note =
            "Compile FAIL. Reemite un hunk Aider sobre el baseline restaurado "
            "(un path, SEARCH = span del pack / disco actual). No copies stderr entero.\n";
      }
    } else {
      emit("L2 ▸ acción no soportada; reintentando. " + action.error);
      result.steps = step;
      ++consecutive_invalid;
      consecutive_turn_errors = 0;
      continue;
    }
    consecutive_invalid = 0;
    if (tr.ok && tr.error != "repeated_plan_targets_pushback" &&
        recover_note.find("Pack cubierto") == std::string::npos) {
      recover_note.clear();
    }
    if (tr.ok && l2_feat::enabled("EDIT_LEAN_PROMPT") &&
        (tr.summary.find("post_edit_coverage") != std::string::npos ||
         tr.summary.find("edit_covered_path") != std::string::npos)) {
      recover_note =
          "Coverage incompleta: falta un path de Instruction (Observations / código fresco).\n"
          "Emite UN hunk Aider de ESE path. SEARCH = líneas exactas del bloque **fresh** "
          "(no de la firma).\n"
          "Empieza con el path y la marca <<<<<<< SEARCH\n"
          "PROHIBIDO JSON, plan, tool y done. No reemitas un path ya cubierto.\n";
    }
    if (tr.ok && l2_feat::enabled("EDIT_LEAN_PROMPT") &&
        tr.summary.find("map_review") != std::string::npos) {
      recover_note =
          "Instruction cubierta. Emite {\"action\":\"done\",\"summary\":\"…qué cambiaste…\"} "
          "sin next.\n"
          "PROHIBIDO plan/tool/edit extra (un edit se ignora y se cierra).\n";
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

    // Rescue: model never emits a_done after enough A0/A1 — crown best Expand/Useful.
    // Generic: queue score (ignore #window), seed overlap, modality; a1_job_root = tie-break.
    // loci_draft is a candidate only (same ranking) — never first-wins / auto-primary.
    if (tr.ok && phase == "explore_a" && tuide::a_effect_summary_enabled() && step >= 8) {
      tuide::AState ast_res = Level2Session::load_a_state(opts.workspace_root);
      if (ast_res.a1_queue.empty() || step >= 12) {
        struct RescueCand {
          std::string anchor;
          std::string stem;
          std::string why;
          float score = -1.f;
        };
        std::vector<RescueCand> cands;

        // Exact seed match ranked by L1 order (earlier = stronger). Queue score is tie-break only.
        auto seed_boost = [&](const std::string& stem, const std::string& target) {
          float boost = 0.f;
          int best_rank = -1;
          for (size_t i = 0; i < ast_res.seeds.size(); ++i) {
            const auto& s = ast_res.seeds[i];
            if (s.empty()) {
              continue;
            }
            const bool exact =
                (!stem.empty() && stem == s) ||
                (target.size() >= s.size() + 1 &&
                 target.compare(target.size() - s.size(), s.size(), s) == 0 &&
                 target[target.size() - s.size() - 1] == ':');
            if (exact) {
              if (best_rank < 0 || static_cast<int>(i) < best_rank) {
                best_rank = static_cast<int>(i);
              }
              continue;
            }
            if ((!stem.empty() && (stem.find(s) != std::string::npos ||
                                   s.find(stem) != std::string::npos)) ||
                target.find(s) != std::string::npos) {
              boost = std::max(boost, 1e5f);
            }
          }
          if (best_rank >= 0) {
            // Dominate typical queue gaps (~1e6–1e7); earlier seeds win.
            const float rank_weight =
                static_cast<float>(static_cast<int>(ast_res.seeds.size()) - best_rank);
            boost = std::max(boost, 1e8f + rank_weight * 1e6f);
          }
          return boost;
        };

        std::string job_root = ast_res.a1_job_root;
        tuide::a_strip_window(&job_root, nullptr);

        for (const auto& n : ast_res.notes) {
          if (n.verdict != tuide::AVerdictKind::Expand &&
              n.verdict != tuide::AVerdictKind::Useful) {
            continue;
          }
          std::string tgt = n.target.empty() ? n.anchor : n.target;
          if (tgt.empty()) {
            continue;
          }
          tuide::a_strip_window(&tgt, nullptr);
          float sc = tuide::a_queue_item_score(ast_res, tgt);
          if (n.expand_with == tuide::AExpandModality::Trail) {
            sc += 200.f;
          } else if (n.expand_with == tuide::AExpandModality::Dataflow) {
            sc += 100.f;
          }
          sc += seed_boost(n.stem, tgt);
          if (n.verdict == tuide::AVerdictKind::Useful) {
            sc += 50.f;
          }
          // Current A1 job is a tiny tie-break — never dominates queue/seed ranking.
          if (!job_root.empty() && tgt == job_root) {
            sc += 1.f;
          }
          RescueCand c;
          c.anchor = n.anchor.empty() ? n.target : n.anchor;
          tuide::a_strip_window(&c.anchor, nullptr);
          if (c.anchor.empty()) {
            c.anchor = tgt;
          }
          c.stem = n.stem.empty() ? tuide::a_stem_from_path(c.anchor) : n.stem;
          c.why = n.why.empty() ? "expand note (rescue a_done)" : n.why;
          c.score = sc;
          bool merged = false;
          for (auto& existing : cands) {
            if (existing.anchor == c.anchor) {
              if (c.score > existing.score) {
                existing = c;
              }
              merged = true;
              break;
            }
          }
          if (!merged) {
            cands.push_back(std::move(c));
          }
        }

        // loci_draft competes in the same ranking (never auto-primary).
        for (const auto& loc : ast_res.loci_draft) {
          std::string tgt = loc.anchor;
          tuide::a_strip_window(&tgt, nullptr);
          if (tgt.empty()) {
            continue;
          }
          float sc = tuide::a_queue_item_score(ast_res, tgt);
          sc += seed_boost(loc.stem, tgt);
          RescueCand c;
          c.anchor = tgt;
          c.stem = loc.stem.empty() ? tuide::a_stem_from_path(tgt) : loc.stem;
          c.why = loc.why.empty() ? "loci_draft (rescue cand)" : loc.why;
          c.score = sc;
          bool merged = false;
          for (auto& existing : cands) {
            if (existing.anchor == c.anchor) {
              if (c.score > existing.score) {
                existing.score = c.score;
                if (existing.why.empty()) {
                  existing.why = c.why;
                }
              }
              merged = true;
              break;
            }
          }
          if (!merged) {
            cands.push_back(std::move(c));
          }
        }

        // Fallback: if notes empty but a1_job_root exists, still crown it.
        if (cands.empty() && !job_root.empty()) {
          RescueCand c;
          c.anchor = job_root;
          c.stem = tuide::a_stem_from_path(c.anchor);
          c.why = "a1_job_root (rescue a_done)";
          c.score = tuide::a_queue_item_score(ast_res, c.anchor) + seed_boost(c.stem, c.anchor);
          cands.push_back(std::move(c));
        }

        std::stable_sort(cands.begin(), cands.end(),
                         [](const RescueCand& a, const RescueCand& b) {
                           return a.score > b.score;
                         });

        if (!cands.empty()) {
          std::vector<tuide::ALocus> loci;
          for (size_t i = 0; i < cands.size() && loci.size() < 1 + tuide::kAMaxSecondaryLoci;
               ++i) {
            tuide::ALocus loc;
            loc.anchor = cands[i].anchor;
            loc.stem = cands[i].stem;
            loc.role = i == 0 ? tuide::ALocusRole::Primary : tuide::ALocusRole::Secondary;
            loc.why = cands[i].why;
            tuide::a_normalize_locus(&loc);
            loci.push_back(std::move(loc));
          }
          {
            bool has_reject = false;
            for (const auto& n : ast_res.notes) {
              if (n.verdict == tuide::AVerdictKind::Reject) {
                has_reject = true;
                break;
              }
            }
            if (!has_reject) {
              std::string primary = loci.front().anchor;
              tuide::a_strip_window(&primary, nullptr);
              auto demote_one = [&](tuide::AVerdictKind from) {
                for (auto& n : ast_res.notes) {
                  if (n.verdict != from) {
                    continue;
                  }
                  std::string t = n.target.empty() ? n.anchor : n.target;
                  tuide::a_strip_window(&t, nullptr);
                  if (t.empty() || t == primary) {
                    continue;
                  }
                  n.verdict = tuide::AVerdictKind::Reject;
                  if (n.why.empty()) {
                    n.why = "rescue: contraste vs locus";
                  }
                  return true;
                }
                return false;
              };
              if (demote_one(tuide::AVerdictKind::Uncertain) ||
                  demote_one(tuide::AVerdictKind::Expand)) {
                Level2Session::save_a_state(opts.workspace_root, ast_res, nullptr);
                emit("L2 ▸ rescue contraste: demote competidor → reject");
              }
            }
          }
          emit("L2 ▸ rescue a_done from expand `" + loci.front().anchor + "`" +
               (loci.size() > 1
                    ? (" (+" + std::to_string(loci.size() - 1) + " secondary)")
                    : ""));
          const auto done_tr =
              session.apply_a_done(opts.workspace_root, loci, "rescue: expand→locus");
          emit(std::string("L2 ▸ rescue a_done ") + (done_tr.ok ? "OK" : "FAIL") + " — " +
               (done_tr.ok ? done_tr.phase : done_tr.error).substr(0, 160));
          if (done_tr.ok) {
            tr = done_tr;
            if (opts.stop_at_phase_a) {
              result.ok = true;
              result.phase = "explore_a_ok";
              result.summary = tr.summary;
              result.steps = step;
              emit(phase_banner("explore_a", step, max_steps) +
                   " — Phase A OK (rescue a_done; stop_at_phase_a)");
              return result;
            }
          } else {
            recover_note = "a_done rescue falló: " + done_tr.error +
                           "\nEmite a_done con primary `" + loci.front().anchor + "` stem=`" +
                           loci.front().stem + "`.\n";
          }
        }
      }
    }

    if (!tr.ok && !tr.error.empty()) {
      emit("L2 ▸ turn error: " + tr.error);
      ++consecutive_turn_errors;
      if (consecutive_turn_errors >= 8) {
        emit("L2 ▸ demasiados turn errors seguidos — cerrando en clarify");
        const auto fin = session.mark_done(
            opts.workspace_root,
            "loop: demasiados errores de turno seguidos; ¿concretas path:símbolo?", "clarify");
        result.ok = true;
        result.phase = fin.phase.empty() ? "clarify" : fin.phase;
        result.summary = fin.summary;
        return result;
      }
    } else {
      consecutive_turn_errors = 0;
    }
    if (tr.phase == "done" || tr.phase == "clarify") {
      if (opts.stop_at_explore && tr.phase == "clarify") {
        result.ok = false;
        result.phase = "clarify";
        result.summary = tr.summary.empty() ? action.summary : tr.summary;
        ai_trace(AiTraceChannel::L2, "l2_run_end",
                 std::string("{\"ok\":0,\"phase\":\"clarify\",\"steps\":") +
                     std::to_string(result.steps) +
                     ",\"total_ms\":" + std::to_string(elapsed_ms(run_t0)) + "}");
        emit(phase_banner("explore", step, max_steps) + " — clarify (explore fail)");
        return result;
      }
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
    if (opts.stop_at_explore && tr.phase == "edit") {
      const std::string st2 = session.status_text(opts.workspace_root);
      const bool pack_ok = st2.find("has_pack: yes") != std::string::npos &&
                           st2.find("pack_incomplete: yes") == std::string::npos;
      const std::string rv_flags = session.status_flags(opts.workspace_root);
      const bool review_ok =
          !pack_review_enabled() || status_has_pack_review_ok(rv_flags);
      result.ok = pack_ok && review_ok;
      result.phase = (pack_ok && review_ok) ? "explore_ok" : "edit";
      result.summary = pack_ok && review_ok
                           ? "explore: pack completo → edit"
                           : (pack_ok ? "explore→edit sin pack review OK"
                                      : ("explore→edit sin pack completo: " + tr.summary));
      ai_trace(AiTraceChannel::L2, "l2_run_end",
               std::string("{\"ok\":") + (result.ok ? "1" : "0") +
                   ",\"phase\":\"explore_ok\",\"steps\":" + std::to_string(result.steps) +
                   ",\"total_ms\":" + std::to_string(elapsed_ms(run_t0)) + "}");
      emit(phase_banner("explore", step, max_steps) + " — " +
           (result.ok ? "explore OK (pack completo)"
                      : (review_ok ? "explore incompleto" : "explore sin review OK")));
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
