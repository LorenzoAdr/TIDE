#include "ai/level2_autonomous_loop.hpp"

#include <chrono>
#include <fstream>
#include <sstream>

#include "ai/ai_trace.hpp"
#include "ai/l2_action.hpp"

namespace tuide {
namespace {

constexpr std::size_t kMaxPromptCharsExplore = 24000;
constexpr std::size_t kMaxPromptCharsEdit = 12000;

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
  // Prefer session header (query/instruction) + recent Observations tail.
  const std::string obs_mark = "## Observations";
  const auto obs_pos = body.find(obs_mark);
  if (obs_pos != std::string::npos && obs_pos < max_chars / 3) {
    const std::string head = body.substr(0, std::min(obs_pos + obs_mark.size() + 2, max_chars / 4));
    const std::size_t tail_budget = max_chars > head.size() + 40 ? max_chars - head.size() - 40 : max_chars / 2;
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

// Explore: keep instruction header + top of ## Ranked map (highest scores), then a short
// Observations tail if present. Dropping low-ranked map tail is intentional.
std::string read_session_for_explore(const std::string& path, std::size_t max_chars) {
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

  const std::string map_mark = "## Ranked map";
  const std::string obs_mark = "## Observations";
  const auto map_pos = body.find(map_mark);
  const auto obs_pos = body.find(obs_mark);

  if (map_pos == std::string::npos) {
    return read_file_limited(path, max_chars);
  }

  const std::string header = body.substr(0, map_pos + map_mark.size());
  std::string map_body;
  std::string obs_body;
  if (obs_pos != std::string::npos && obs_pos > map_pos) {
    map_body = body.substr(map_pos + map_mark.size(), obs_pos - (map_pos + map_mark.size()));
    obs_body = body.substr(obs_pos);
  } else {
    map_body = body.substr(map_pos + map_mark.size());
  }

  constexpr std::size_t kObsBudget = 4000;
  if (obs_body.size() > kObsBudget) {
    obs_body = obs_body.substr(0, kObsBudget) + "\n…[observations truncadas]…\n";
  }

  const std::size_t reserved = header.size() + obs_body.size() + 80;
  const std::size_t map_budget =
      max_chars > reserved ? max_chars - reserved : max_chars / 2;
  if (map_body.size() > map_budget) {
    map_body = map_body.substr(0, map_budget) +
               "\n\n…[ranked map cola omitida; prioriza entradas con score alto]…\n";
  }

  return header + map_body + obs_body;
}

std::string phase_banner(const std::string& phase, int step, int max_steps) {
  return "L2 ▸ fase=" + phase + " paso=" + std::to_string(step) + "/" +
         std::to_string(max_steps);
}

std::string build_system_prompt(const std::string& extra) {
  std::ostringstream out;
  out << "Eres el Nivel 2 (coder) de tuide. Exploras el repo con tools de lectura y "
         "emites ediciones Search/Replace de match único.\n"
         "Responde SIEMPRE con UN solo objeto JSON. PROHIBIDO markdown/prosa fuera del JSON.\n"
         "Formatos:\n"
         "{\"action\":\"tool\",\"name\":\"get_code_of\",\"arg\":\"src/foo.cpp:Symbol\"}\n"
         "{\"action\":\"tool\",\"name\":\"search\",\"arg\":\"needle\"}\n"
         "{\"action\":\"tool\",\"name\":\"file_outline\",\"arg\":\"src/foo.cpp\"}\n"
         "{\"action\":\"done\",\"summary\":\"evidencia paths:líneas\",\"next\":\"edit\"}\n"
         "{\"action\":\"done\",\"summary\":\"no encontré X; ¿concretas?\",\"next\":\"clarify\"}\n"
         "{\"action\":\"edit\",\"hunks\":[{\"path\":\"src/foo.cpp\",\"search\":\"exacto único\","
         "\"replace\":\"nuevo\"}]}\n"
         "Fases: en explore SOLO tool|done (preferible done next=edit al cerrar). "
         "El ## Ranked map de la sesión es el punto de partida: prioriza entradas altas y "
         "decide qué leer. "
         "action=edit es para phase=edit; si emites edit aún en explore el runtime "
         "auto-promueve a edit y aplica. "
         "Reglas: next=edit solo con evidencia en Observations; search debe ser único en el "
         "archivo; no inventes paths; tras edit_feedback (search no encontrado/ambiguo) "
         "corrige el search (get_code_of si hace falta) y NO repitas el mismo hunk; "
         "tras compile fail reemite edit corrigiendo.\n";
  out << Level2Session::tool_guide_markdown();
  if (!extra.empty()) {
    out << "\n" << extra << "\n";
  }
  return out.str();
}

std::string build_user_prompt(const std::string& workspace_root, const std::string& phase,
                              int step) {
  std::ostringstream out;
  out << "phase=" << phase << " step=" << step << "\n\n";
  if (phase == "edit") {
    out << "Corrige con action=edit (Search/Replace único) usando el feedback reciente "
           "(edit_feedback: search no encontrado/ambiguo, o stderr tail + old/new). "
           "Si falló el apply, cambia el search; no repitas el hunk idéntico. "
           "No pidas la traza completa.\n\n";
    out << read_file_tail(Level2Session::session_path(workspace_root), kMaxPromptCharsEdit);
  } else {
    out << "El ## Ranked map es tu base. Explora (tools) y decide la siguiente acción JSON.\n\n";
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
    {
      const auto p = status.find("phase: ");
      if (p != std::string::npos) {
        const auto end = status.find_first_of(" \n", p + 7);
        phase = status.substr(p + 7, end == std::string::npos ? std::string::npos : end - (p + 7));
      }
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
    breq.user_prompt = build_user_prompt(opts.workspace_root, phase, step);
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
    if (action.kind == L2ActionKind::Tool) {
      emit("L2 ▸ tool " + action.name +
           (action.arg.empty() ? "" : ("(" + action.arg.substr(0, 80) + ")")));
      tr = session.apply_tool(opts.workspace_root, action.name, action.arg);
    } else if (action.kind == L2ActionKind::Done) {
      emit("L2 ▸ done next=" + (action.next.empty() ? "(none)" : action.next) + " — " +
           action.summary.substr(0, 120));
      tr = session.mark_done(opts.workspace_root, action.summary, action.next);
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
