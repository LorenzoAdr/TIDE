#include "ai/ai_controller.hpp"

#include <chrono>
#include <sstream>

#include "ai/ai_packages.hpp"
#include "ai/ai_trace.hpp"
#include "ai/level0_router.hpp"
#include "ai/level1_agent.hpp"
#include "ai/model_store.hpp"
#include "i18n/tr.hpp"
#include "ui/busy_strip.hpp"
#include "ui/main_layout.hpp"
#include "ui/ui_wake.hpp"

namespace tuide {

AiController::AiController(AiControllerDeps deps) : deps_(std::move(deps)) {
  refresh_settings();
  append("AI listo (A/B/C). L0 delante; L1 agent local bajo demanda. /help");
}

AiController::~AiController() {
  cancel_all();
  join_agent_thread();
  join_task_thread();
  join_symbol_embed_thread();
}

void AiController::set_deps(AiControllerDeps deps) {
  deps_ = std::move(deps);
  tools_ready_ = false;
  refresh_settings();
}

void AiController::refresh_settings() {
  if (deps_.config != nullptr) {
    settings_ = deps_.config->ai;
  } else if (deps_.workspace != nullptr && !deps_.workspace->root.empty()) {
    settings_ = WorkspaceConfig::load(deps_.workspace->root).ai;
  }
  const std::string root = deps_.workspace != nullptr ? deps_.workspace->root : std::string{};
  ai_trace_configure(settings_.trace_enabled, root, settings_.trace_path);
  apply_llama_bundle_preference(settings_);
  sync_task_runner();
}

void AiController::sync_task_runner() {
  tasks_.set_whitelist(settings_.command_whitelist);
  std::vector<AiTaskSpec> specs;
  for (const auto& [name, cmd] : settings_.tasks) {
    specs.push_back({name, cmd});
  }
  tasks_.set_tasks(std::move(specs));
  const std::string root = deps_.workspace != nullptr ? deps_.workspace->root : std::string{};
  tasks_.ensure_default_tasks(root);
}

std::vector<std::string> AiController::snapshot_lines() const {
  std::lock_guard lock(lines_mu_);
  return lines_;
}

void AiController::append(const std::string& line) {
  // Multiline payloads (investigate lists, tool dumps) must become separate rows:
  // the AI console renders each vector entry with ftxui::text() at height 1.
  std::vector<std::string> parts;
  {
    std::string normalized;
    normalized.reserve(line.size());
    for (char ch : line) {
      if (ch == '\r') {
        continue;  // drop CR (progress / CRLF); avoid turning CRLF into blank rows
      }
      normalized.push_back(ch);
    }
    std::istringstream iss(normalized);
    std::string part;
    while (std::getline(iss, part)) {
      parts.push_back(std::move(part));
    }
    if (parts.empty()) {
      parts.push_back(std::string{});
    }
  }

  std::size_t n = 0;
  {
    std::lock_guard lock(lines_mu_);
    for (const auto& part : parts) {
      lines_.push_back(part);
    }
    if (lines_.size() > kMaxLines) {
      lines_.erase(lines_.begin(),
                   lines_.begin() + static_cast<std::ptrdiff_t>(lines_.size() - kMaxLines));
    }
    n = lines_.size();
  }
  // Avoid flooding NDJSON during compile (thousands of lines); keep short status.
  if (parts.size() == 1 && parts.front().size() <= 240) {
    ai_trace(AiTraceChannel::System, "transcript",
             "{\"line\":\"" + ai_trace_escape(parts.front()) + "\",\"n\":" + std::to_string(n) +
                 "}");
  } else if (parts.size() > 1) {
    ai_trace(AiTraceChannel::System, "transcript",
             "{\"lines\":" + std::to_string(parts.size()) + ",\"n\":" + std::to_string(n) + "}");
  }
  wake(false);
}

void AiController::clear() {
  std::lock_guard lock(lines_mu_);
  lines_.clear();
}

void AiController::wake(bool force) {
  // Streaming must NOT use InputCorrelated: each compile line would run the full
  // UI tick (index/git/editor) and freeze the IDE while rebuilding this repo.
  constexpr auto kMinStreamWake = std::chrono::milliseconds(50);
  {
    std::lock_guard lock(wake_mu_);
    const auto now = std::chrono::steady_clock::now();
    if (!force && stream_wake_pending_ && (now - last_stream_wake_) < kMinStreamWake) {
      return;
    }
    last_stream_wake_ = now;
    stream_wake_pending_ = !force;
  }
  wake_console_panel_stream(deps_.layout, "ai.transcript");
}

void AiController::ensure_tools() {
  if (tools_ready_) {
    return;
  }
  AiToolContext ctx;
  ctx.workspace = deps_.workspace;
  ctx.symbols = deps_.symbols;
  ctx.indexer = deps_.indexer;
  ctx.symbol_indexer = deps_.symbol_indexer;
  ctx.git = deps_.git;
  ctx.workspace_root = deps_.workspace != nullptr ? deps_.workspace->root : std::string{};
  tools_ = ToolRegistry{};
  ToolRegistry::register_builtin_read_tools(&tools_, ctx);
  tools_ready_ = true;
}

void AiController::join_agent_thread() {
  std::thread t;
  {
    std::lock_guard lock(agent_mu_);
    if (agent_thread_.joinable()) {
      t = std::move(agent_thread_);
    }
  }
  if (t.joinable()) {
    t.join();
  }
}

void AiController::join_task_thread() {
  std::thread t;
  {
    std::lock_guard lock(task_mu_);
    if (task_thread_.joinable()) {
      t = std::move(task_thread_);
    }
  }
  if (t.joinable()) {
    t.join();
  }
}

void AiController::cancel_level1() {
  agent_cancel_.store(true);
  if (agent_busy_.load()) {
    append("L1: cancel solicitado…");
  }
}

void AiController::cancel_all() {
  cancel_level1();
  if (tasks_.busy() || task_busy_.load()) {
    tasks_.cancel();
    append("task: cancel solicitado…");
  }
}

void AiController::cancel_current() {
  if (!busy()) {
    append("nada que cancelar");
    return;
  }
  cancel_all();
  wake(true);
}

void AiController::set_missing_package_handler(MissingPackageFn fn) {
  on_missing_package_ = std::move(fn);
}

void AiController::clear_missing_package_notice(const std::string& pack_id) {
  if (pack_id.empty()) {
    missing_notified_.clear();
  } else {
    missing_notified_.erase(pack_id);
  }
  intent_embed_attempted_ = false;
}

void AiController::request_missing_package(const std::string& pack_id) {
  if (pack_id.empty() || !on_missing_package_) {
    return;
  }
  if (missing_notified_.count(pack_id) > 0) {
    return;
  }
  missing_notified_.insert(pack_id);
  if (const AiPackage* pack = find_ai_package(pack_id); pack != nullptr) {
    append(i18n::tr_fmt("ai_toast.terminal_hint", {i18n::tr(pack->name_i18n_key)}));
  } else {
    append(i18n::tr_fmt("ai_toast.terminal_hint", {pack_id}));
  }
  on_missing_package_(pack_id);
}

void AiController::begin_thinking() {
  set_busy_spinner(deps_.layout, BusyActivity::AiThinking);
  wake(true);
}

void AiController::end_thinking() {
  clear_busy_if(deps_.layout, BusyActivity::AiThinking);
  wake(true);
}

void AiController::begin_download(std::string_view label) {
  download_busy_.store(true);
  set_busy_percent(deps_.layout, BusyActivity::AiDownloading, 0, label);
  wake(true);
}

void AiController::update_download_percent(int percent) {
  download_busy_.store(true);
  // Busy strip paints via ANSI; avoid UI_WAKE spam on every percent tick.
  set_busy_percent(deps_.layout, BusyActivity::AiDownloading, percent);
}

void AiController::end_download() {
  const bool was = download_busy_.exchange(false);
  clear_busy_if(deps_.layout, BusyActivity::AiDownloading);
  // If L1 is still running after the download, restore "Pensando".
  if (was && agent_busy_.load()) {
    begin_thinking();
  } else {
    wake(true);
  }
}

ModelStore::ProgressFn AiController::make_store_progress() {
  return [this](const std::string& line) {
    if (line.rfind("__pct__:", 0) == 0) {
      try {
        const int pct = std::stoi(line.substr(8));
        update_download_percent(pct);
      } catch (...) {
      }
      return;
    }
    const bool downloadish =
        line.find("descargando") != std::string::npos ||
        line.find("ModelStore: falta") != std::string::npos ||
        line.find("ModelStore: descargando") != std::string::npos ||
        line.find("ModelStore: actualizando runtime") != std::string::npos;
    if (downloadish && !download_busy_.load()) {
      begin_download();
    }
    if (line.find("extrayendo bundle") != std::string::npos) {
      update_download_percent(100);
    }
    append(line);
  };
}

bool AiController::is_cancel_input(const std::string& line) {
  std::size_t start = 0;
  while (start < line.size() &&
         (line[start] == ' ' || line[start] == '\t')) {
    ++start;
  }
  std::size_t end = line.size();
  while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\t')) {
    --end;
  }
  const std::string_view t(line.data() + start, end - start);
  return t == "/cancel" || t == "cancel" || t == "cancelar";
}

bool AiController::ensure_backend_ready() {
  refresh_settings();
  const std::string missing = first_missing_ai_package_for_l1(settings_);
  if (!missing.empty()) {
    request_missing_package(missing);
    return false;
  }
  std::string error;
  const bool ok = backend_.ensure_ready(settings_, make_store_progress(), &error);
  end_download();
  if (!ok) {
    append("✗ L1 backend: " + error);
    append("  Instala el paquete desde F10 → Toolpacks, o /model download");
  }
  return ok;
}

void AiController::run_level1_async(const std::string& message) {
  if (download_busy_.load()) {
    append("Hay una descarga pendiente (modelo/runtime). Espera a que termine o /cancel.");
    return;
  }
  if (agent_busy_.exchange(true)) {
    append(download_busy_.load()
               ? "Hay una descarga pendiente (modelo/runtime). Espera a que termine o /cancel."
               : "L1 ya está ocupado (/cancel para abortar)");
    return;
  }
  join_agent_thread();
  agent_cancel_.store(false);
  tools_ready_ = false;
  ensure_tools();
  sync_task_runner();
  begin_thinking();

  std::lock_guard lock(agent_mu_);
  agent_thread_ = std::thread([this, message] {
    if (!ensure_backend_ready()) {
      agent_busy_.store(false);
      end_thinking();
      return;
    }
    // Best-effort: warm stem embed index for coding-pack semantic recall.
    ensure_coding_stem_index_ready();
    Level1AgentDeps deps;
    deps.tools = &tools_;
    deps.tasks = &tasks_;
    deps.workspace = deps_.workspace;
    deps.symbol_indexer = deps_.symbol_indexer;
    deps.backend = &backend_;
    deps.embed = embed_backend_.ready() ? &embed_backend_ : nullptr;
    deps.coding_stem_index = coding_stem_index_.ready() ? &coding_stem_index_ : nullptr;
    // Query-time lexical shortlist + two-stage embed (no full-corpus symbol index).
    deps.coding_symbol_index = nullptr;
    deps.settings = settings_;
    deps.on_tool = [this](const std::string& name, const std::string& arg) {
      last_l0_tool_ = name;
      last_l0_arg_ = arg;
            ai_trace(AiTraceChannel::L1, "l1_tool_memory", "{\"tool\":\"" + ai_trace_escape(name) + "\",\"arg\":\"" +
                         ai_trace_escape(arg) + "\"}");
          };
    Level1Agent agent(deps);
    const Level1RunResult result =
        agent.run(message, [this](const std::string& line) { append(line); }, &agent_cancel_);
    if (!result.ok && !result.error.empty()) {
      append("L1 terminó con error: " + result.error);
    } else if (result.needs_level2) {
      append("L1 listo: payload L2 en dry-run (Fase D/E pendiente para coder real).");
    } else if (!result.final_text.empty()) {
      append("L1 done.");
      if (settings_.level2_mode == "dry_run") {
        append("L1: mapa sin bodies (L2 dry-run). El embed de ranking ya se aplicó al ordenar.");
      }
    }
    // Warm stem index only (cheap); no full-corpus symbol embed.
    maybe_start_coding_stem_warm_async();
    agent_busy_.store(false);
    end_thinking();
  });
}

void AiController::show_model_status() {
  ModelStore store(settings_.models_cache_dir.empty() ? ModelStore::default_cache_dir()
                                                      : settings_.models_cache_dir);
  const AiModelInfo info = default_l1_model();
  append("=== L1 model status ===");
  append("cache: " + store.cache_dir());
  append("default: " + info.id + " (" + info.license_note + ")");
  append("gguf: " + store.model_path(info) +
         (store.has_model(info) ? " [present]" : " [missing]"));
  append("llama-cli resolved: " +
         (store.resolve_llama_cli().empty() ? std::string("(none)") : store.resolve_llama_cli()));
  append("auto_download=" + std::string(settings_.level1.auto_download ? "true" : "false"));
  append(backend_.status_text());

  const AiModelInfo emb = default_intent_embed_model();
  append("=== L0 intent embeddings ===");
  append("default: " + emb.id + " (" + emb.license_note + ")");
  append("gguf: " + store.intent_embed_model_path(emb) +
         (store.has_intent_embed_model(emb) ? " [present]" : " [missing]"));
  append("llama-server resolved: " +
         (store.resolve_llama_server().empty() ? std::string("(none)")
                                              : store.resolve_llama_server()));
  append("index: " + std::string(intent_index_.ready() ? "ready" : "not ready") +
         " catalog=" + intent_index_.catalog_path());
  append(embed_backend_.status_text());
  append(ai_trace_status_text());
}

void AiController::download_models(const std::string& what) {
  if (download_busy_.load() || agent_busy_.load()) {
    append("Hay una operación IA en curso (descarga o L1). Espera o /cancel.");
    return;
  }
  begin_thinking();
  ModelStore store(settings_.models_cache_dir.empty() ? ModelStore::default_cache_dir()
                                                      : settings_.models_cache_dir);
  std::string error;
  auto progress = make_store_progress();
  if (what == "runtime") {
    const std::string cli = store.ensure_llama_cli(true, progress, &error);
    end_download();
    if (cli.empty()) {
      append("✗ " + error);
    } else {
      append("llama-cli: " + cli);
      backend_.set_cli_path(cli);
      append("llama-server: " +
             (store.resolve_llama_server().empty() ? std::string("(missing)")
                                                  : store.resolve_llama_server()));
    }
    end_thinking();
    return;
  }
  if (what == "embed" || what == "intent" || what == "l0") {
    const AiModelInfo emb = default_intent_embed_model();
    const std::string path = store.ensure_intent_embed_model(emb, true, progress, &error);
    end_download();
    if (path.empty()) {
      append("✗ " + error);
    } else {
      append("intent embed model: " + path);
      intent_embed_attempted_ = false;
      ensure_intent_embeddings_ready();
    }
    end_thinking();
    return;
  }
  const AiModelInfo info = default_l1_model();
  const std::string path = store.ensure_model(info, true, progress, &error);
  end_download();
  if (path.empty()) {
    append("✗ " + error);
  } else {
    append("model: " + path);
    backend_.set_model_path(path);
  }
  end_thinking();
}

bool AiController::ensure_intent_embeddings_ready(bool prompt_if_missing) {
  if (intent_index_.ready() && embed_backend_.ready()) {
    ai_trace(AiTraceChannel::Embed, "ready_cached", "{\"index\":true,\"backend\":true}");
    return true;
  }
  refresh_settings();
  const std::string missing = first_missing_ai_package_for_embed(settings_);
  if (!missing.empty()) {
    ai_trace(AiTraceChannel::Embed, "skip_missing_pack",
             "{\"pack\":\"" + ai_trace_escape(missing) + "\",\"prompt\":" +
                 (prompt_if_missing ? "true" : "false") + "}");
    if (prompt_if_missing) {
      request_missing_package(missing);
    }
    return false;
  }
  if (intent_embed_attempted_ && !settings_.level0.embeddings.auto_download) {
    // Packages are present but a previous ensure failed (server/runtime glitch).
    ai_trace(AiTraceChannel::Embed, "skip_no_autodl",
             "{\"attempted\":true,\"auto_download\":false}");
    return false;
  }
  intent_embed_attempted_ = true;
  std::string error;
  auto progress = make_store_progress();
  ai_trace(AiTraceChannel::Embed, "ensure_begin",
           "{\"model_id\":\"" + ai_trace_escape(settings_.level0.embeddings.model_id) +
               "\",\"model_path\":\"" + ai_trace_escape(settings_.level0.embeddings.model_path) +
               "\",\"auto_download\":" +
               (settings_.level0.embeddings.auto_download ? "true" : "false") + ",\"port\":" +
               std::to_string(settings_.level0.embeddings.server_port) + "}");
  if (!embed_backend_.ensure_ready(settings_, progress, &error)) {
    end_download();
    ai_trace(AiTraceChannel::Embed, "backend_fail",
             "{\"error\":\"" + ai_trace_escape(error) + "\",\"model\":\"" +
                 ai_trace_escape(embed_backend_.model_path()) + "\"}");
    append("L0 embed: " + error + " (keywords L0 activos; F10 Toolpacks / /model download embed)");
    return false;
  }
  end_download();
  if (!intent_index_.load_catalog(Level0IntentIndex::resolve_default_catalog_path(), &error)) {
    ai_trace(AiTraceChannel::Embed, "catalog_fail",
             "{\"error\":\"" + ai_trace_escape(error) + "\"}");
    append("L0 intent catalog: " + error);
    return false;
  }
  const std::string cache =
      settings_.models_cache_dir.empty() ? ModelStore::default_cache_dir() : settings_.models_cache_dir;
  if (!intent_index_.build(&embed_backend_, cache, settings_.level0.embeddings.model_id, progress,
                           &error)) {
    ai_trace(AiTraceChannel::Embed, "index_build_fail",
             "{\"error\":\"" + ai_trace_escape(error) + "\"}");
    append("L0 intent index: " + error);
    return false;
  }
  ai_trace(AiTraceChannel::Embed, "ready_ok",
           "{\"catalog\":\"" + ai_trace_escape(intent_index_.catalog_path()) + "\",\"model\":\"" +
               ai_trace_escape(embed_backend_.model_path()) +
               "\",\"port\":" + std::to_string(embed_backend_.port()) + "}");
  append("L0 semantic routing listo");
  return true;
}

bool AiController::ensure_coding_stem_index_ready() {
  if (coding_stem_index_.ready() && embed_backend_.ready()) {
    return true;
  }
  if (!ensure_intent_embeddings_ready()) {
    return false;
  }
  if (deps_.symbol_indexer == nullptr) {
    return false;
  }
  const auto snap = deps_.symbol_indexer->snapshot();
  if (!snap || snap->symbols.empty()) {
    return false;
  }
  std::string error;
  auto progress = [this](const std::string& line) { append(line); };
  const std::string cache =
      settings_.models_cache_dir.empty() ? ModelStore::default_cache_dir() : settings_.models_cache_dir;
  if (!coding_stem_index_.ensure(snap.get(), &embed_backend_, cache,
                                 settings_.level0.embeddings.model_id, progress, &error)) {
    append("coding stem embed: " + error);
    return false;
  }
  return true;
}

void AiController::join_symbol_embed_thread() {
  std::lock_guard lock(symbol_embed_mu_);
  if (symbol_embed_thread_.joinable()) {
    symbol_embed_thread_.join();
  }
}

void AiController::on_symbol_map_ready() {
  if (!settings_.enabled) {
    return;
  }
  refresh_settings();
  const std::string root = deps_.workspace != nullptr ? deps_.workspace->root : std::string{};
  if (root.empty()) {
    return;
  }
  if (root != symbol_embed_workspace_root_) {
    symbol_embed_workspace_root_ = root;
    if (!symbol_embed_running_.load()) {
      coding_stem_index_.invalidate();
      coding_symbol_index_.invalidate();
      symbol_embed_started_.store(false);
    } else {
      symbol_embed_restart_pending_.store(true);
      return;
    }
  }
  maybe_start_coding_stem_warm_async();
}

void AiController::maybe_start_coding_stem_warm_async() {
  if (coding_stem_index_.ready()) {
    return;
  }
  if (symbol_embed_running_.load()) {
    return;
  }
  bool expected = false;
  if (!symbol_embed_started_.compare_exchange_strong(expected, true)) {
    return;
  }
  if (deps_.symbol_indexer == nullptr) {
    symbol_embed_started_.store(false);
    return;
  }
  const auto snap_probe = deps_.symbol_indexer->snapshot();
  if (!snap_probe || snap_probe->symbols.empty()) {
    symbol_embed_started_.store(false);
    return;
  }

  std::lock_guard lock(symbol_embed_mu_);
  if (symbol_embed_thread_.joinable()) {
    symbol_embed_thread_.join();
  }
  symbol_embed_running_.store(true);
  symbol_embed_thread_ = std::thread([this] {
    auto finish = [this](bool ok) {
      symbol_embed_busy_active_.store(false);
      refresh_ai_embedding_busy(deps_.layout, false, 0, 0);
      symbol_embed_running_.store(false);
      if (!ok) {
        symbol_embed_started_.store(false);
      }
      const bool restart = symbol_embed_restart_pending_.exchange(false);
      if (restart) {
        coding_stem_index_.invalidate();
        symbol_embed_started_.store(false);
        std::thread([this] { maybe_start_coding_stem_warm_async(); }).detach();
      }
    };
    if (!embed_backend_.ready()) {
      if (!ensure_intent_embeddings_ready() || !embed_backend_.ready()) {
        append("coding stem embed: backend no listo");
        finish(false);
        wake(true);
        return;
      }
    }
    const bool ok = ensure_coding_stem_index_ready();
    if (ok) {
      append("coding stem embed: listo (n=" + std::to_string(coding_stem_index_.size()) + ")");
    }
    finish(ok);
    wake(true);
  });
}

void AiController::run_tool(const std::string& name, const std::string& arg) {
  ensure_tools();
  append("→ tool " + name + (arg.empty() ? "" : (" " + arg)));
  // repo_map / search can take seconds on the UI thread; avoid "Pensando" which
  // also blocks Mapping progress updates (AiThinking has priority).
  const bool show_thinking = name != "repo_map" && name != "search";
  if (show_thinking) {
    begin_thinking();
  }
  const AiToolResult result = tools_.invoke(name, arg);
  if (show_thinking) {
    end_thinking();
  }
  if (!result.ok) {
    append("✗ " + result.text);
    return;
  }
  std::istringstream iss(result.text);
  std::string line;
  int n = 0;
  while (std::getline(iss, line)) {
    append(line);
    if (++n >= 200) {
      append("… (salida truncada)");
      break;
    }
  }
  if (n == 0 && !result.text.empty()) {
    append(result.text);
  }
  // L0: cierre humano breve (no requiere L1/L2).
  if (name == "git_pull") {
    append("✓ Listo: git pull completado.");
  } else if (name == "git_status") {
    if (!arg.empty()) {
      append("✓ Listo: cambios filtrados por `" + arg + "`.");
    } else {
      append("✓ Listo: status del working tree arriba.");
    }
  } else if (name == "git_branches") {
    append("✓ Listo: listado de ramas arriba.");
  } else if (name == "git_diff") {
    append("✓ Listo: diffs arriba.");
  } else if (name == "git_log") {
    append("✓ Listo: historial de commits arriba.");
  } else if (name == "git_show") {
    append("✓ Listo: detalle del commit arriba.");
  }
}

void AiController::run_task(const std::string& name) {
  sync_task_runner();
  if (tasks_.busy()) {
    append("✗ ya hay una task en curso (/cancel)");
    return;
  }
  if (task_busy_.exchange(true)) {
    append("✗ ya hay una task en curso (/cancel)");
    return;
  }
  join_task_thread();
  const std::string root = deps_.workspace != nullptr ? deps_.workspace->root : std::string{};
  std::string command = name;
  for (const auto& t : tasks_.tasks()) {
    if (t.name == name) {
      command = t.command;
      break;
    }
  }
  append("→ task " + name + " (background)");
  append("  $ " + command + (root.empty() ? "" : ("  [cwd=" + root + "]")));
  ai_trace(AiTraceChannel::Tool, "task_start",
           "{\"name\":\"" + ai_trace_escape(name) + "\",\"cmd\":\"" + ai_trace_escape(command) +
               "\",\"root\":\"" + ai_trace_escape(root) + "\"}");
  // Do not begin_thinking(): AiThinking + per-line InputCorrelated wakes used to
  // run full UI ticks on every compile stdout line and freeze the IDE.
  std::lock_guard lock(task_mu_);
  task_thread_ = std::thread([this, name, root] {
    const TaskRunnerResult result =
        tasks_.run(name, root, [this](const std::string& line) { append(line); });
    if (!result.allowed) {
      append("✗ deny: " + result.deny_reason);
      append("  (añade el comando a ai.command_whitelist en .tuide/config.json)");
    } else if (!result.stderr_text.empty() && result.exit_code != 0) {
      append("exit_code=" + std::to_string(result.exit_code) + " (" + result.stderr_text + ")");
    } else {
      append("exit_code=" + std::to_string(result.exit_code));
    }
    ai_trace(AiTraceChannel::Tool, "task_done",
             "{\"name\":\"" + ai_trace_escape(name) + "\",\"allowed\":" +
                 (result.allowed ? "true" : "false") + ",\"exit\":" +
                 std::to_string(result.exit_code) + "}");
    task_busy_.store(false);
    wake(true);
  });
}

void AiController::dump_context_pack(const std::vector<std::string>& seeds) {
  ensure_tools();
  std::string arg;
  for (std::size_t i = 0; i < seeds.size(); ++i) {
    if (i) {
      arg.push_back(' ');
    }
    arg += seeds[i];
  }
  run_tool("context_pack", arg);
}

void AiController::handle_route(const AiRouteResult& route, const std::string& original) {
  {
    const char* kind = "unknown";
    switch (route.kind) {
      case AiRouteKind::Help:
        kind = "help";
        break;
      case AiRouteKind::ResolveTool:
        kind = "tool";
        break;
      case AiRouteKind::ResolveTask:
        kind = "task";
        break;
      case AiRouteKind::EscalateLevel1:
        kind = "escalate_l1";
        break;
      case AiRouteKind::ForceLevel1:
        kind = "force_l1";
        break;
      case AiRouteKind::CancelAgent:
        kind = "cancel";
        break;
      case AiRouteKind::ModelStatus:
        kind = "model_status";
        break;
      case AiRouteKind::ModelDownload:
        kind = "model_download";
        break;
      case AiRouteKind::Trace:
        kind = "trace";
        break;
      case AiRouteKind::Error:
        kind = "error";
        break;
    }
    ai_trace(AiTraceChannel::L0, "dispatch",
             std::string("{\"kind\":\"") + kind + "\",\"tool\":\"" +
                 ai_trace_escape(route.tool_name) + "\",\"task\":\"" +
                 ai_trace_escape(route.task_name) + "\",\"arg\":\"" + ai_trace_escape(route.arg) +
                 "\",\"msg\":\"" + ai_trace_escape(route.message) + "\",\"input\":\"" +
                 ai_trace_escape(original) + "\"}");
  }
  switch (route.kind) {
    case AiRouteKind::Help: {
      append("Comandos L0:");
      append("  /help  /build|/compile  /launch  /search <q>  /diag  /git [status|pull|branch|log]");
      append("  /read <path>  /ls [filter]  /symbols <q>  /hover [path:line:col]");
      append("  /context [seeds…]  /contextdump [q]  /codeof <path:Sym>  /repomap [query|status]");
      append("  /apply_demo  /tools");
      append("  /l1 <msg>  /explain <msg>  /model […]  /trace [status|on|off|tail|clear]  /cancel");
      append("NL rápida: \"compila\", \"busca Foo\", \"lista errores\", \"git status\", "
             "\"git pull\", \"últimos commits\", \"dame contexto de …\"");
      append("NL ambigua → Nivel 1. Compile/launch en background (/cancel aborta). "
             "Trace: .tuide/ai/trace.ndjson (/trace status); mapa: .tuide/ai/map_last.md");
      break;
    }
    case AiRouteKind::ResolveTool:
      if (route.tool_name == "context_pack" && !route.seeds.empty()) {
        dump_context_pack(route.seeds);
      } else {
        run_tool(route.tool_name, route.arg);
      }
      break;
    case AiRouteKind::ResolveTask:
      run_task(route.task_name);
      break;
    case AiRouteKind::EscalateLevel1:
      append("L0 → escalate_to_level1" +
             (route.message.empty() ? std::string{} : (": " + route.message)));
      run_level1_async(original);
      break;
    case AiRouteKind::ForceLevel1:
      append("L0 → force L1");
      run_level1_async(route.arg.empty() ? original : route.arg);
      break;
    case AiRouteKind::CancelAgent:
      cancel_all();
      break;
    case AiRouteKind::ModelStatus:
      show_model_status();
      break;
    case AiRouteKind::ModelDownload:
      download_models(route.arg);
      break;
    case AiRouteKind::Trace: {
      const std::string cmd = route.arg;
      if (cmd.empty() || cmd == "status") {
        append(ai_trace_status_text());
        append("Canales: system | L0 | L1 | L2 | embed | tool");
        append("Config: ai.trace.enabled / ai.trace.path en .tuide/config.json");
      } else if (cmd == "on") {
        const std::string root =
            deps_.workspace != nullptr ? deps_.workspace->root : std::string{};
        settings_.trace_enabled = true;
        ai_trace_configure(true, root, settings_.trace_path);
        append(ai_trace_status_text());
      } else if (cmd == "off") {
        const std::string root =
            deps_.workspace != nullptr ? deps_.workspace->root : std::string{};
        settings_.trace_enabled = false;
        ai_trace_configure(false, root, settings_.trace_path);
        append(ai_trace_status_text());
      } else if (cmd == "clear") {
        std::string err;
        if (!ai_trace_clear(&err)) {
          append("✗ " + err);
        } else {
          append("trace limpiado: " + ai_trace_path());
        }
      } else if (cmd == "tail" || cmd.rfind("tail ", 0) == 0) {
        std::istringstream iss(ai_trace_tail(40));
        std::string line;
        while (std::getline(iss, line)) {
          append(line);
        }
      } else {
        append("uso: /trace [status|on|off|tail|clear]");
      }
      break;
    }
    case AiRouteKind::Error:
      append("✗ " + route.message);
      break;
  }
}

void AiController::handle_user_input(const std::string& line) {
  if (!settings_.enabled) {
    append("AI deshabilitado (ai.enabled=false)");
    return;
  }
  tools_ready_ = false;
  refresh_settings();
  append("> " + line);

  if (download_busy_.load() && !is_cancel_input(line)) {
    append("Hay una descarga pendiente (modelo/runtime). Espera a que termine o /cancel.");
    return;
  }

  Level0SemanticMatcher semantic;
  // Slash stays deterministic; NL benefits from embeddings when available.
  // Skip embed ensure while L1 is busy — avoids racing a Vulkan/runtime download.
  if (!line.empty() && line[0] != '/') {
    if (agent_busy_.load()) {
      append("L1 ya está ocupado (/cancel para abortar)");
      return;
    }
    if (ensure_intent_embeddings_ready(true)) {
      semantic = [this](const std::string& query) {
        std::string err;
        const Level0IntentMatch m = intent_index_.match(
            query, &embed_backend_, settings_.level0.min_score, settings_.level0.min_margin, &err);
        ai_trace(AiTraceChannel::L0, "intent_match",
                 "{\"ok\":" + std::string(m.ok ? "true" : "false") + ",\"name\":\"" +
                     ai_trace_escape(m.name) + "\",\"policy\":\"" +
                     ai_trace_escape(m.arg_policy) + "\",\"score\":" + std::to_string(m.score) +
                     ",\"margin\":" + std::to_string(m.margin) + ",\"min_score\":" +
                     std::to_string(settings_.level0.min_score) + ",\"min_margin\":" +
                     std::to_string(settings_.level0.min_margin) + ",\"err\":\"" +
                     ai_trace_escape(err) + "\",\"q\":\"" + ai_trace_escape(query, 200) + "\"}");
        return m;
      };
    }
  }

  AiRouteResult route = route_level0(line, last_l0_tool_, last_l0_arg_, semantic);
  ai_trace(AiTraceChannel::L0, "route",
           "{\"prev\":\"" + ai_trace_escape(last_l0_tool_) + "\",\"prev_arg\":\"" +
               ai_trace_escape(last_l0_arg_) + "\",\"input\":\"" + ai_trace_escape(line) +
               "\",\"semantic\":" + (semantic ? "true" : "false") + ",\"kind\":" +
               std::to_string(static_cast<int>(route.kind)) + ",\"tool\":\"" +
               ai_trace_escape(route.tool_name) + "\",\"task\":\"" +
               ai_trace_escape(route.task_name) + "\",\"arg\":\"" +
               ai_trace_escape(route.arg) + "\"}");
  if (route.kind == AiRouteKind::ResolveTool || route.kind == AiRouteKind::ResolveTask) {
    last_l0_tool_ = route.kind == AiRouteKind::ResolveTool ? route.tool_name : route.task_name;
    last_l0_arg_ = route.arg;
  }
  handle_route(route, line);
}

}  // namespace tuide
