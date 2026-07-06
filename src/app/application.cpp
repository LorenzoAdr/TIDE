#include "app/application.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <thread>
#include <unistd.h>

#include "app/workspace_config.hpp"
#include "app/workspace_session.hpp"
#include "backend/idebug_backend.hpp"
#include "build/build_environment.hpp"
#include "build/build_environment_service.hpp"
#include "dap/gdb_launcher.hpp"
#include "editor/text_search.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "i18n/locale.hpp"
#include "i18n/tr.hpp"
#include "packet_monitor/pkt_monitor_service.hpp"
#include "ui/binary_symbols_panel.hpp"
#include "ui/connection_wizard.hpp"
#include "ui/console_panel.hpp"
#include "ui/context_menu.hpp"
#include "ui/core_analyzer_panel.hpp"
#include "ui/cursor_blink.hpp"
#include "ui/file_picker.hpp"
#include "ui/git_panel.hpp"
#include "ui/glyphs.hpp"
#include "ui/key_bindings.hpp"
#include "ui/main_layout.hpp"
#include "ui/open_file_confirm.hpp"
#include "ui/press_ids.hpp"
#include "ui/quit_confirm.hpp"
#include "ui/settings_modal.hpp"
#include "ui/shutdown_overlay.hpp"
#include "ui/source_panel.hpp"
#include "ui/status_layout_popover.hpp"
#include "ui/symbol_picker.hpp"
#include "ui/terminal_display.hpp"
#include "ui/terminal_keyboard.hpp"
#include "ui/theme.hpp"
#include "util/bundled_tools.hpp"
#include "util/clang_format_config.hpp"
#include "util/clangd_workspace_setup.hpp"
#include "util/core_analyzer_support.hpp"
#include "util/crash_handler.hpp"
#include "util/debug_session_log.hpp"
#include "util/docker_shell.hpp"
#include "util/monitor_log.hpp"
#include "util/path_normalize.hpp"
#include "util/shell_args.hpp"
#include "util/thread_name.hpp"

namespace fs = std::filesystem;

namespace tgdb {

using namespace ftxui;

namespace {

static int64_t steady_now_ms() {
	using namespace std::chrono;
	return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool event_is_alt_up(const Event &event) {
	return event == Event::Special("\x1B[1;3A");
}

bool event_is_alt_down(const Event &event) {
	return event == Event::Special("\x1B[1;3B");
}

void force_immediate_repaint(MainLayoutState *layout, ScreenInteractive *screen) {
	if (layout != nullptr) {
		layout->request_ui_tick = true;
	}
	if (screen == nullptr) {
		return;
	}
	screen->PostEvent(Event::Custom);
}

class EventPoller {
  public:
	using MainThreadTask = std::function<void()>;

	static constexpr int64_t kActiveIntervalMs = 50;
	static constexpr int64_t kIdleIntervalMs = 200;
	static constexpr int64_t kIdleThresholdMs = 2000;

	EventPoller(ScreenInteractive *screen, MainLayoutState *layout, MainThreadTask on_main_thread)
	    : screen_(screen), layout_(layout), on_main_thread_(std::move(on_main_thread)),
	      last_user_activity_ms_(steady_now_ms()) {
		thread_ = std::thread([this] {
			set_current_thread_name("ui-poller");
			while (running_.load(std::memory_order_acquire)) {
				const int64_t since_activity =
				    steady_now_ms() - last_user_activity_ms_.load(std::memory_order_relaxed);
				const int64_t interval =
				    since_activity >= kIdleThresholdMs ? kIdleIntervalMs : kActiveIntervalMs;
				std::this_thread::sleep_for(std::chrono::milliseconds(interval));
				if (layout_ != nullptr &&
				    layout_->shutdown_ui_poll_paused.load(std::memory_order_acquire)) {
					continue;
				}
				if (screen_ != nullptr) {
					if (layout_ != nullptr) {
						layout_->ui_heartbeat.store(true, std::memory_order_release);
					}
					if (on_main_thread_) {
					screen_->Post(on_main_thread_);
				}
				screen_->PostEvent(Event::Custom);
			}
		}
	});
	}

	~EventPoller() {
		running_.store(false, std::memory_order_release);
		if (thread_.joinable()) {
			thread_.join();
		}
	}

	void notify_user_activity() {
		last_user_activity_ms_.store(steady_now_ms(), std::memory_order_relaxed);
	}

	EventPoller(const EventPoller &) = delete;
	EventPoller &operator=(const EventPoller &) = delete;

  private:
	static int64_t steady_now_ms() {
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	ScreenInteractive *screen_;
	MainLayoutState *layout_;
	MainThreadTask on_main_thread_;
	std::atomic<bool> running_{true};
	std::atomic<int64_t> last_user_activity_ms_;
	std::thread thread_;
};

// FTXUI CatchEvent invoca el handler antes que el hijo; RequestUiTickGuard en el
// handler se destruía con request_ui_tick aún en false. Este wrapper postea Custom
// después de que todo el árbol haya procesado el evento.
class UiTickPostWrapper : public ComponentBase {
  public:
	UiTickPostWrapper(MainLayoutState *layout, ScreenInteractive *screen)
	    : layout_(layout), screen_(screen) {}

	bool OnEvent(Event event) override {
		// Evaluate event type before the move; moving invalidates the string payload but
		// leaves type_ and mouse_ data intact — checking before is cleaner.
		const bool is_custom = (event == Event::Custom);
		// Pure mouse moves (no button pressed) already trigger a direct screen.Post from
		// the mouse handler in CatchEvent; skip the duplicate UiTickPost to avoid running
		// the UI loop at the raw mouse polling rate (~100-200 Hz).
		const bool is_pure_mouse_move = !is_custom && event.is_mouse() &&
		                                event.mouse().motion == Mouse::Moved &&
		                                event.mouse().button == Mouse::None;
		const bool handled = ComponentBase::OnEvent(std::move(event));
		// Never chain Custom→Custom: each tick that sets request_ui_tick would otherwi
		// enqueue another Custom in the same RunOnce drain loop and starve keyboard input.
		if (!is_custom && !is_pure_mouse_move) {
			post_pending_tick();
		}
		
		return handled;
	}

  private:
	void post_pending_tick() {
		if (layout_ == nullptr || screen_ == nullptr || !layout_->request_ui_tick) {
			return;
		}
		layout_->request_ui_tick = false;
		screen_->PostEvent(Event::Custom);
	}

	MainLayoutState *layout_;
	ScreenInteractive *screen_;
};

Component WrapUiTickPost(Component child, MainLayoutState *layout, ScreenInteractive *screen) {
	auto wrapper = Make<UiTickPostWrapper>(layout, screen);
	wrapper->Add(std::move(child));
	return wrapper;
}

} // namespace

Application::Application(AppConfig config) : config_(std::move(config)) {
	std::error_code ec;

	if (config_.workspace_root.empty()) {
		config_.workspace_root = fs::current_path().string();
	} else {
		config_.workspace_root = fs::absolute(config_.workspace_root, ec).string();
	}

	if (!config_.program.empty()) {
		config_.program = fs::absolute(config_.program, ec).string();
	}

	if (!config_.initial_file.empty()) {
		config_.initial_file = fs::absolute(config_.initial_file, ec).string();
	}

	model_.workspace_root = config_.workspace_root;
	model_.program = config_.program;
	model_.program_args = config_.args;
	workspace_.root = config_.workspace_root;

	connection_wizard_state_.browser.launch_root = config_.launch_directory;
	if (connection_wizard_state_.browser.launch_root.empty()) {
		connection_wizard_state_.browser.launch_root = config_.workspace_root;
	}
	workspace_wizard_state_.launch_root = connection_wizard_state_.browser.launch_root;

	symbol_provider_ = std::make_shared<LspSymbolProvider>();
	app_settings_ = AppSettings::load();
	i18n::set_locale(app_settings_.ui_locale);
	workspace_.status_message = i18n::tr("workspace.select");
	model_.status_message = i18n::tr("debug.disconnected");
	configure_glyphs(resolve_icon_mode(app_settings_.icon_mode));
	layout_state_.app_settings = &app_settings_;
	layout_state_.workspace_clang_format = &clang_format_config_;
	layout_state_.apply_app_settings_callback = [this] { apply_app_settings(); };
	if (has_bundled_clangd()) {
		set_runtime_force_bundled_clangd(app_settings_.force_bundled_clangd);
	}
	if (has_bundled_gdb()) {
		set_runtime_force_bundled_gdb(app_settings_.force_bundled_gdb);
	}
	symbol_provider_->set_lsp_enabled(app_settings_.lsp_enabled);
	monitor_log::set_enabled(app_settings_.monitor_enabled);
	debug_available_ = gdb_supports_dap();
	workspace_.open_file_confirm = &open_file_confirm_state_;
	secondary_workspace_.open_file_confirm = &open_file_confirm_state_;
	const auto wire_ui_tasks = [this](WorkspaceModel *workspace) {
		if (workspace == nullptr) {
			return;
		}
		workspace->enqueue_ui_task = [this](WorkspaceModel::UiTask task) {
			enqueue_ui_task(std::move(task));
		};
	};
	wire_ui_tasks(&workspace_);
	wire_ui_tasks(&secondary_workspace_);

	if (config_.show_welcome_screen) {
		layout_state_.welcome_visible = true;
		layout_state_.console_visible = false;
		layout_state_.terminal_start_requested = false;
		workspace_.status_message = i18n::tr("workspace.welcome");
		workspace_.clear_tabs();
	} else {
		std::string anchor = config_.workspace_root;
		if (!config_.initial_file.empty()) {
			anchor = fs::path(config_.initial_file).parent_path().string();
		}
		const WorkspaceDetectResult detected = resolve_workspace_for_anchor(anchor);
		config_.workspace_root = detected.workspace_root;
		model_.workspace_root = detected.workspace_root;
		workspace_.root = detected.workspace_root;
		const WorkspaceDetectResult *detect_ptr =
		    app_settings_.workspace_auto_detect_enabled ? &detected : nullptr;
		set_workspace(detected.workspace_root, detect_ptr);
		if (!config_.initial_file.empty()) {
			workspace_.open_file(config_.initial_file);
		}
		if (config_.auto_debug && connection_config_complete()) {
			if (debug_available_) {
				app_mode_ = AppMode::kDebug;
				layout_state_.console_tabs.selected_tab = ConsolePanelTabs::kDebug;
				layout_state_.console_visible = true;
				layout_state_.terminal_start_requested = true;
			} else {
				set_workspace_status(i18n::tr("app.debug_unavailable"));
			}
		}
	}

	backend_ = std::make_unique<DapBackend>(command_queue_, event_queue_);
}

Application::~Application() {
	if (shutdown_thread_.joinable()) {
		shutdown_thread_.join();
	}
	monitor_log::set_enabled(false);
	if (shutdown_performed_) {
		return;
	}
	stop_all_subprocesses();
}

void Application::stop_all_subprocesses() {
	save_workspace_session();
	build_artifact_watcher_.stop();
	global_build_environment_service().shutdown();
	shell_session_.stop();
	if (symbol_provider_) {
		symbol_provider_->on_workspace_closed();
	}
	symbol_indexer_.stop();
	indexer_.stop();
	if (backend_) {
		backend_->stop();
	}
}

void Application::request_terminal_autostart() {
	if (model_.workspace_root.empty()) {
		return;
	}
	layout_state_.console_visible = true;
	layout_state_.terminal_start_requested = true;
}

void Application::rebuild_shell_launch_config() {
	cached_shell_launch_config_.host_cwd = workspace_.root;
	cached_shell_launch_config_.docker_container.clear();
	cached_shell_launch_config_.docker_cwd.clear();
	cached_shell_launch_config_.env_vars.clear();
	cached_shell_launch_config_.setup_scripts.clear();
	if (workspace_.root.empty()) {
		return;
	}
	cached_shell_launch_config_ = resolve_shell_launch_config(workspace_.root, workspace_config_);
}

void Application::setup_build_environment_watching() {
	build_artifact_watcher_.stop();
	if (workspace_.root.empty()) {
		return;
	}

	const BuildEnvironment &active = global_build_environment_service().active_environment();
	std::vector<std::string> watch_dirs = active.marker_paths;
	if (!active.working_dir.empty()) {
		watch_dirs.push_back(active.working_dir);
	}

	global_build_environment_service().set_environment_changed_callback(
	    [this] { schedule_debounced_lsp_restart(); });

	build_artifact_watcher_.set_change_callback([this] {
		if (workspace_.root.empty()) {
			return;
		}
		EnvironmentSelectionHints hints;
		hints.active_file_path =
		    workspace_.buffer.path.empty() ? workspace_.active_file : workspace_.buffer.path;
		global_build_environment_service().notify_artifacts_changed(workspace_.root,
		                                                            workspace_config_, hints);
		schedule_debounced_lsp_restart();
		if (!model_.program.empty()) {
			refresh_binary_symbols_if_matches(&layout_state_, model_.program);
		}
	});
	build_artifact_watcher_.start(workspace_.root, watch_dirs);
}

void Application::schedule_debounced_lsp_restart() {
	pending_lsp_restart_ = true;
	lsp_restart_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
}

void Application::process_build_environment_updates() {
	if (!pending_lsp_restart_) {
		return;
	}
	if (std::chrono::steady_clock::now() < lsp_restart_deadline_) {
		return;
	}
	pending_lsp_restart_ = false;

	const std::string fingerprint =
	    global_build_environment_service().active_environment_fingerprint();
	if (!fingerprint.empty() && fingerprint == last_lsp_environment_fingerprint_) {
		return;
	}
	last_lsp_environment_fingerprint_ = fingerprint;
	rebuild_shell_launch_config();
	restart_lsp_for_workspace();
}

namespace {

std::string editor_buffer_text(const EditorBuffer &buffer) {
	std::string text;
	for (std::size_t i = 0; i < buffer.lines.size(); ++i) {
		if (i > 0) {
			text.push_back('\n');
		}
		text += buffer.lines[i];
	}
	return text;
}

void reopen_workspace_documents(WorkspaceModel *workspace,
                                const std::shared_ptr<ISymbolProvider> &symbols) {
	if (workspace == nullptr || symbols == nullptr) {
		return;
	}
	workspace->flush_active_tab();
	for (const auto &tab : workspace->tabs) {
		if (tab.path.empty()) {
			continue;
		}
		symbols->on_document_opened(tab.path, editor_buffer_text(tab.buffer));
	}
}

} // namespace

void Application::restart_lsp_for_workspace() {
	if (!symbol_provider_ || workspace_.root.empty()) {
		return;
	}
	const auto setup = ensure_compile_commands_for_clangd(workspace_.root, workspace_config_);
	symbol_provider_->set_workspace_clangd_options(workspace_config_.clangd_use_gcc_query_driver,
	                                               workspace_config_.clangd_background_index);
	symbol_provider_->on_workspace_opened(workspace_.root, setup.compile_dir);
	enqueue_ui_task([this]() {
		reopen_workspace_documents(&workspace_, symbol_provider_);
		workspace_.buffer.view_token++;
		secondary_workspace_.buffer.view_token++;
		layout_state_.request_ui_tick = true;
	});
	if (!setup.status_note.empty()) {
		workspace_.status_message += " | " + setup.status_note;
	}
}

void Application::sync_symbol_workspace_indexer() {
	if (workspace_.root.empty()) {
		symbol_indexer_.stop();
		return;
	}
	// With LSP on, bulk regex indexing scans the whole tree and duplicates clangd work.
	if (app_settings_.lsp_enabled) {
		symbol_indexer_.stop();
		return;
	}
	symbol_indexer_.start_scan(workspace_.root, symbol_provider_, &indexer_);
}

IndexFilterOptions Application::index_filter_options() const {
	IndexFilterOptions options;
	options.show_all_files = app_settings_.show_all_workspace_files;
	return options;
}

void Application::restart_workspace_indexing() {
	if (workspace_.root.empty()) {
		return;
	}
	const IndexFilterOptions options = index_filter_options();
	indexer_.start_scan(workspace_.root, options);
}

void Application::reindex_project() {
	if (workspace_.root.empty()) {
		set_workspace_status(i18n::tr("status.index_no_workspace"));
		layout_state_.request_ui_tick = true;
		return;
	}
	if (!resolve_clangd().has_value()) {
		set_workspace_status(i18n::tr("status.index_no_clangd"));
		layout_state_.request_ui_tick = true;
		return;
	}
	if (!app_settings_.lsp_enabled) {
		app_settings_.lsp_enabled = true;
		app_settings_.save();
		if (settings_modal_state_.open) {
			settings_modal_state_.draft_lsp_enabled = true;
		}
		apply_app_settings();
		set_workspace_status(i18n::tr("status.index_lsp_enabled"));
		layout_state_.request_ui_tick = true;
		return;
	}
	if (reindex_in_progress_.exchange(true)) {
		set_workspace_status(i18n::tr("status.index_started"));
		layout_state_.request_ui_tick = true;
		return;
	}
	set_workspace_status(i18n::tr("status.index_started"));
	layout_state_.request_ui_tick = true;
	enqueue_ui_task([this]() {
		workspace_.flush_active_tab();
		std::thread([this]() {
			set_current_thread_name("reindex");
			restart_lsp_for_workspace();
			sync_symbol_workspace_indexer();
			restart_workspace_indexing();
			enqueue_ui_task([this]() {
				reopen_workspace_documents(&workspace_, symbol_provider_);
				workspace_.buffer.view_token++;
				reindex_in_progress_.store(false);
				set_workspace_status(i18n::tr("status.index_started"));
				layout_state_.request_ui_tick = true;
			});
		}).detach();
	});
}

void Application::enqueue_ui_task(std::function<void()> task) {
	if (!task) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock(ui_task_mutex_);
		ui_tasks_.push_back(std::move(task));
	}
	layout_state_.request_ui_tick = true;
}

void Application::drain_ui_tasks() {
	std::deque<std::function<void()>> tasks;
	{
		std::lock_guard<std::mutex> lock(ui_task_mutex_);
		tasks.swap(ui_tasks_);
	}
	for (auto &task : tasks) {
		if (task) {
			task();
		}
	}
}

void Application::apply_workspace_settings(const WorkspaceConfig &config) {
	workspace_config_ = config;
	theme::set_mode(workspace_config_.theme);
	theme::set_ui_overrides(workspace_config_.ui_colors);
	if (workspace_.root.empty()) {
		return;
	}
	workspace_config_.save(workspace_.root);
	invalidate_docker_mount_cache();
	rebuild_shell_launch_config();
	apply_clangd_workspace_config(workspace_.root, workspace_config_);
	shell_session_.stop();
	request_terminal_autostart();
	restart_lsp_for_workspace();
	setup_build_environment_watching();
	sync_symbol_workspace_indexer();
	workspace_.buffer.view_token++;
	layout_state_.request_ui_tick = true;
}

void Application::save_workspace_session() {
	if (workspace_.root.empty()) {
		return;
	}
	workspace_.flush_active_tab();
	WorkspaceSession session;
	session.open_tabs.reserve(workspace_.tabs.size());
	for (const auto &tab : workspace_.tabs) {
		if (!tab.path.empty()) {
			session.open_tabs.push_back(tab.path);
		}
	}
	if (workspace_.active_tab >= 0 &&
	    workspace_.active_tab < static_cast<int>(workspace_.tabs.size())) {
		session.active_tab_path =
		    workspace_.tabs[static_cast<std::size_t>(workspace_.active_tab)].path;
	}
	session.launch_args = workspace_launch_args_;
	session.last_launch_program = last_launch_program_;
	session.last_attach_program = last_attach_program_;
	session.save(workspace_.root);
}

void Application::begin_shutdown(ScreenInteractive *screen) {
	if (shutdown_state_.is_active()) {
		return;
	}
	quit_confirm_state_.open = false;
	quit_confirm_state_.unsaved_paths.clear();
	layout_state_.shutdown_ui_poll_paused.store(true, std::memory_order_release);
	shutdown_state_.begin(7);
	shutdown_step_index_ = 0;
	shutdown_state_.set_current(i18n::tr("app.shutdown.save_session"));
	layout_state_.request_ui_tick = true;
	if (screen != nullptr) {
		screen->Post([this, screen] {
			force_immediate_repaint(&layout_state_, screen);
			screen->Post([this, screen] {
				force_immediate_repaint(&layout_state_, screen);
				screen->Post([this, screen] { schedule_next_shutdown_step(screen); });
			});
		});
	}
}

void Application::schedule_next_shutdown_step(ScreenInteractive *screen) {
	if (!shutdown_state_.is_active() || shutdown_state_.is_complete()) {
		return;
	}
	if (shutdown_thread_.joinable()) {
		shutdown_thread_.join();
	}

	shutdown_thread_ = std::thread([this, screen] {
		set_current_thread_name("shutdown");
		tick_shutdown();
		if (screen == nullptr) {
			return;
		}
		screen->Post([this, screen] {
			force_immediate_repaint(&layout_state_, screen);
			if (shutdown_state_.is_complete()) {
				screen->ExitLoopClosure()();
				return;
			}
			screen->Post([this, screen] { schedule_next_shutdown_step(screen); });
		});
	});
}

void Application::tick_shutdown() {
	struct ShutdownStep {
		std::string label;
		std::function<void()> run;
	};

	const std::vector<ShutdownStep> steps = {
	    {i18n::tr("app.shutdown.save_session"), [this] { save_workspace_session(); }},
	    {i18n::tr("app.shutdown.close_debug"),
	     [this] {
		     if (backend_started_) {
			     submit_command(UiCommand{UiCommandKind::kQuit});
		     }
		     if (backend_) {
			     backend_->stop();
			     backend_started_ = false;
		     }
	     }},
	    {i18n::tr("app.shutdown.close_terminal"), [this] { shell_session_.stop(); }},
	    {i18n::tr("app.shutdown.close_clangd"),
	     [this] {
		     if (symbol_provider_) {
			     symbol_provider_->on_workspace_closed();
		     }
	     }},
	    {i18n::tr("app.shutdown.stop_indexers"),
	     [this] {
		     symbol_indexer_.stop();
		     indexer_.stop();
	     }},
	    {i18n::tr("app.shutdown.stop_watcher"), [this] { build_artifact_watcher_.stop(); }},
	    {i18n::tr("app.shutdown.stop_build_env"),
	     [this] { global_build_environment_service().shutdown(); }},
	};

	if (shutdown_step_index_ >= static_cast<int>(steps.size())) {
		if (!shutdown_state_.is_complete()) {
			shutdown_state_.mark_complete();
			shutdown_performed_ = true;
		}
		return;
	}

	const auto &step = steps[static_cast<std::size_t>(shutdown_step_index_)];
	step.run();
	shutdown_state_.complete_step(step.label);
	++shutdown_step_index_;

	if (shutdown_step_index_ < static_cast<int>(steps.size())) {
		shutdown_state_.set_current(steps[static_cast<std::size_t>(shutdown_step_index_)].label);
	} else {
		shutdown_state_.mark_complete();
		shutdown_performed_ = true;
	}
	layout_state_.request_ui_tick = true;
}

void Application::force_exit() {
	_exit(1);
}

void Application::restore_workspace_session() {
	if (workspace_.root.empty()) {
		return;
	}
	const WorkspaceSession session = WorkspaceSession::load(workspace_.root);
	workspace_launch_args_ = session.launch_args;
	last_launch_program_ = session.last_launch_program;
	last_attach_program_ = session.last_attach_program;
	if (session.open_tabs.empty()) {
		return;
	}
	for (const auto &path : session.open_tabs) {
		std::error_code ec;
		if (!fs::is_regular_file(fs::path(path), ec)) {
			continue;
		}
		workspace_.open_file(path);
	}
	if (!session.active_tab_path.empty()) {
		workspace_.open_file(session.active_tab_path);
	}
}

void Application::dismiss_welcome_screen() {
	if (!layout_state_.welcome_visible) {
		return;
	}
	layout_state_.welcome_visible = false;
	layout_state_.console_visible = true;
	layout_state_.terminal_start_requested = true;
	layout_state_.request_ui_tick = true;
}

void Application::set_workspace(const std::string &workspace_root,
                                const WorkspaceDetectResult *detect) {
	dismiss_welcome_screen();
	workspace_initialized_ = true;
	save_workspace_session();
	std::error_code ec;
	const auto absolute = fs::absolute(workspace_root, ec).string();
	config_.workspace_root = absolute;
	model_.workspace_root = absolute;
	workspace_.root = absolute;
	workspace_.cursor_history.clear();
	workspace_.clear_tabs();
	secondary_workspace_.root = absolute;
	secondary_workspace_.clear_tabs();
	if (detect != nullptr && detect->marker_found) {
		workspace_.status_message = i18n::tr_fmt(
		    "workspace.detected_from", {fs::path(absolute).filename().string(), detect->marker});
	} else {
		workspace_.status_message =
		    i18n::tr_fmt("workspace.open_prefix", {fs::path(absolute).filename().string()});
	}
	file_picker_state_.indexed_root.clear();
	file_picker_state_.all_files.clear();
	file_picker_state_.all_files_lower.clear();
	shell_session_.stop();
	model_.console_output.clear();
	request_terminal_autostart();
	workspace_config_ = WorkspaceConfig::load(absolute);
	theme::set_mode(workspace_config_.theme);
	theme::set_ui_overrides(workspace_config_.ui_colors);
	const ClangFormatConfig format_config = load_clang_format(absolute);
	clang_format_config_ = format_config;
	{
		const std::string format_path = clang_format_path(absolute);
		std::error_code format_ec;
		if (!format_path.empty() && !fs::is_regular_file(format_path, format_ec)) {
			save_clang_format(absolute, format_config);
		}
	}
	invalidate_docker_mount_cache();
	rebuild_shell_launch_config();
	apply_clangd_workspace_config(absolute, workspace_config_);
	if (symbol_provider_) {
		const auto setup = ensure_compile_commands_for_clangd(absolute, workspace_config_);
		if (setup.compile_dir.empty()) {
			std::error_code cmake_ec;
			if (fs::is_regular_file(fs::path(absolute) / "CMakeLists.txt", cmake_ec)) {
				workspace_.status_message += i18n::tr("app.no_compile_commands");
			} else if (detect_build_system_kind(absolute) == BuildSystemKind::kMakefile ||
			           detect_build_system_kind(absolute) == BuildSystemKind::kHybrid) {
				workspace_.status_message += " | generando entorno make";
			} else {
				workspace_.status_message += i18n::tr("app.no_compile_commands_json");
			}
		} else {
			if (!setup.status_note.empty()) {
				workspace_.status_message += " | " + setup.status_note;
			} else if (has_bundled_clangd()) {
				// Warm up extraction before clangd spawn so LSP startup is faster.
				(void)resolve_clangd();
			}
		}
		symbol_provider_->set_workspace_clangd_options(
		    workspace_config_.clangd_use_gcc_query_driver,
		    workspace_config_.clangd_background_index);
		symbol_provider_->on_workspace_opened(absolute, setup.compile_dir);
		last_lsp_environment_fingerprint_ =
		    global_build_environment_service().active_environment_fingerprint();
		setup_build_environment_watching();
	}
	indexer_.start_scan(absolute, index_filter_options());
	sync_symbol_workspace_indexer();
	git_service_.open(absolute);
	restore_workspace_session();
}

WorkspaceDetectResult Application::resolve_workspace_for_anchor(const std::string &anchor) const {
	if (app_settings_.workspace_auto_detect_enabled) {
		return detect_workspace_root(anchor);
	}
	return detect_workspace_root(anchor, 0);
}

void Application::on_workspace_complete(const std::string &workspace_root,
                                        ScreenInteractive * /*screen*/) {
	workspace_wizard_state_.open = false;
	pending_workspace_load_ = workspace_root;
}

void Application::process_pending_workspace_load() {
	if (!pending_workspace_load_.has_value()) {
		return;
	}
	const std::string chosen = std::move(*pending_workspace_load_);
	pending_workspace_load_.reset();
	const WorkspaceDetectResult detected = resolve_workspace_for_anchor(chosen);
	const WorkspaceDetectResult *detect_ptr =
	    app_settings_.workspace_auto_detect_enabled ? &detected : nullptr;
	set_workspace(detected.workspace_root, detect_ptr);
	config_.show_welcome_screen = false;
	focus_state_.region = workspace_.tabs.empty() ? FocusRegion::Explorer : FocusRegion::Editor;
	layout_state_.text_input_focus = TextInputFocus::None;
	layout_state_.focus_sync_needed = true;
	// No autostart shell here: bash PTY output + repaint nudges starve the UI event loop.
	layout_state_.terminal_start_requested = false;
	layout_state_.request_ui_tick = true;
}

void Application::open_external_file_wizard() {
	if (external_file_wizard_state_.open || workspace_wizard_state_.open ||
	    connection_wizard_state_.open) {
		return;
	}
	external_file_wizard_state_.launch_root =
	    workspace_.root.empty() ? connection_wizard_state_.browser.launch_root : workspace_.root;
	external_file_wizard_state_.reset();
	external_file_wizard_state_.open = true;
}

void Application::open_workspace_wizard() {
	if (workspace_wizard_state_.open || connection_wizard_state_.open) {
		return;
	}
	if (debugging_started_) {
		exit_debug_mode();
	}
	workspace_wizard_state_.launch_root =
	    workspace_.root.empty() ? connection_wizard_state_.browser.launch_root : workspace_.root;
	workspace_wizard_state_.reset();
	workspace_wizard_state_.open = true;
}

bool Application::connection_config_complete() const {
	if (config_.program.empty()) {
		return false;
	}
	if (config_.mode == SessionMode::kAttach) {
		return config_.attach_pid > 0 || !config_.attach_target.empty();
	}
	if (config_.mode == SessionMode::kCore) {
		return !config_.core_path.empty();
	}
	return true;
}

void Application::ensure_backend_started() {
	if (backend_started_ || !debug_available_) {
		return;
	}
	backend_->start();
	backend_started_ = true;
}

void Application::set_status(const std::string &message) {
	model_.status_message = message;
	workspace_.status_message = message;
}

void Application::set_workspace_status(const std::string &message) {
	workspace_.status_message = message;
	if (app_mode_ == AppMode::kNormal) {
		model_.status_message = message;
	}
}

void Application::exit_debug_mode() {
	if (debugging_started_) {
		if (config_.mode == SessionMode::kAttach) {
			submit_command(UiCommand{UiCommandKind::kDetach});
		} else {
			submit_command(UiCommand{UiCommandKind::kDisconnect});
		}
		debugging_started_ = false;
		session_ready_ = false;
	}
	if (backend_started_) {
		backend_->stop();
		backend_started_ = false;
		command_queue_.reset();
		event_queue_.reset();
		backend_ = std::make_unique<DapBackend>(command_queue_, event_queue_);
	}

	model_.stack_frames.clear();
	model_.locals.clear();
	model_.variable_children.clear();
	model_.watches.clear();
	model_.console_output.clear();
	model_.core_analyzer_log.clear();
	model_.core_analyzer_instances.clear();
	model_.core_analyzer_selected_instance = -1;
	model_.core_path.clear();
	model_.is_post_mortem = false;
	model_.session_mode = SessionMode::kLaunch;
	model_.core_analysis_mode = CoreAnalysisMode::kGdbOnly;
	model_.state = DebugState::kDisconnected;

	app_mode_ = AppMode::kNormal;
	layout_state_.text_input_focus = TextInputFocus::None;
	layout_state_.console_tabs.selected_tab = ConsolePanelTabs::kTerminal;
	layout_state_.show_core_analyzer_tab = false;
	layout_state_.packet_monitor_service->reset();
	set_workspace_status(i18n::tr("app.edit_mode"));
	request_terminal_autostart();
}

std::string Application::launch_cwd_for_program(const std::string &program) const {
	if (program.empty()) {
		return config_.workspace_root;
	}
	std::error_code ec;
	const fs::path parent = fs::path(program).parent_path();
	if (parent.empty()) {
		return config_.workspace_root;
	}
	const fs::path resolved = fs::weakly_canonical(parent, ec);
	return ec ? parent.string() : resolved.string();
}

void Application::sync_model_breakpoints_to_backend() {
	if (!backend_started_) {
		return;
	}
	for (const auto &[file, lines] : model_.breakpoints_by_file) {
		UiCommand command;
		command.kind = UiCommandKind::kSetBreakpoints;
		command.breakpoint_file = file;
		command.breakpoint_lines = model_.enabled_breakpoint_lines(file);
		backend_->submit(command);
	}
}

void Application::apply_connection_and_start() {
	if (!session_ready_ || debugging_started_) {
		return;
	}
	if (!connection_config_complete()) {
		return;
	}

	debugging_started_ = true;
	dismiss_welcome_screen();
	workspace_initialized_ = true;
	model_.program = config_.program;
	model_.workspace_root = config_.workspace_root;
	model_.program_args = config_.args;
	model_.session_mode = config_.mode;
	model_.core_analysis_mode = config_.core_analysis;
	model_.core_path = config_.core_path;
	model_.is_post_mortem = config_.mode == SessionMode::kCore;

	if (!workspace_.buffer.path.empty()) {
		model_.active_file = workspace_.buffer.path;
		model_.active_line = workspace_.buffer.primary_line() + 1;
		model_.view_token++;
	}

	app_mode_ = AppMode::kDebug;
	focus_state_.region = FocusRegion::Terminal;
	layout_state_.right_panel_active_section = 1;
	layout_state_.focus_sync_needed = true;
	layout_state_.core_analyzer_focus = MainLayoutState::CoreAnalyzerFocus::kCommand;
	layout_state_.show_core_analyzer_tab = core_analyzer_supported() &&
	                                       config_.mode == SessionMode::kCore &&
	                                       config_.core_analysis == CoreAnalysisMode::kCoreAnalyzer;
	if (layout_state_.show_core_analyzer_tab) {
		layout_state_.console_tabs.selected_tab = ConsolePanelTabs::kCoreAnalyzer;
		layout_state_.text_input_focus = TextInputFocus::Console;
	} else {
		layout_state_.text_input_focus = TextInputFocus::Console;
		layout_state_.console_tabs.selected_tab = ConsolePanelTabs::kDebug;
	}
	layout_state_.console_visible = true;
	layout_state_.terminal_start_requested = true;
	layout_state_.packet_monitor_service->reset();
	layout_state_.packet_monitor_service->set_enabled(config_.packet_monitor_enabled);
	layout_state_.packet_monitor_service->set_workspace_root(workspace_.root);
	if (config_.packet_monitor_enabled) {
		auto &filters = layout_state_.packet_monitor_service->state().filters;
		filters.src_ip = config_.packet_monitor_filter_src;
		filters.dst_ip = config_.packet_monitor_filter_dst;
	}
	model_.console_output.clear();
	if (!config_.program.empty()) {
		request_binary_symbols_panel(&layout_state_, config_.program, {}, NmBindingFilter::kAll,
		                             false);
	}

	sync_model_breakpoints_to_backend();

	if (config_.mode == SessionMode::kAttach) {
		UiCommand attach;
		attach.kind = UiCommandKind::kAttach;
		attach.attach.program = config_.program;
		attach.attach.cwd = config_.workspace_root;
		attach.attach.pid = config_.attach_pid;
		attach.attach.target = config_.attach_target;
		submit_command(attach);
		if (config_.attach_pid > 0) {
			set_status(i18n::tr_fmt("app.attach_pid", {std::to_string(config_.attach_pid)}));
		} else if (!config_.attach_target.empty()) {
			set_status(i18n::tr_fmt("app.attach_target", {config_.attach_target}));
		}
	} else if (config_.mode == SessionMode::kCore) {
		UiCommand load_core;
		load_core.kind = UiCommandKind::kLoadCore;
		load_core.core.program = config_.program;
		load_core.core.core_path = config_.core_path;
		load_core.core.cwd = launch_cwd_for_program(config_.program);
		load_core.core.analysis = config_.core_analysis;
		submit_command(load_core);
		const std::string mode_label = config_.core_analysis == CoreAnalysisMode::kCoreAnalyzer
		                                   ? i18n::tr("app.core_mode.ca")
		                                   : i18n::tr("app.core_mode.gdb_postmortem");
		set_status(i18n::tr_fmt("app.load_core", {mode_label, config_.core_path}));
	} else {
		UiCommand launch;
		launch.kind = UiCommandKind::kLaunch;
		launch.launch.program = config_.program;
		launch.launch.cwd = launch_cwd_for_program(config_.program);
		launch.launch.args = config_.args;
		launch.launch.stop_at_main = true;
		launch.launch.packet_monitor_enabled = config_.packet_monitor_enabled;
		launch.launch.packet_monitor_filter_src = config_.packet_monitor_filter_src;
		launch.launch.packet_monitor_filter_dst = config_.packet_monitor_filter_dst;
		submit_command(launch);
		set_status(i18n::tr_fmt("app.launch_program", {config_.program}));
	}
}

void Application::on_connection_complete(const ConnectionResult &result) {
	pending_connection_ = result;
}

void Application::apply_pending_connection() {
	if (!pending_connection_.has_value()) {
		return;
	}

	if (workspace_.buffer.dirty) {
		set_status(i18n::tr("app.save_before_debug"));
		return;
	}

	const ConnectionResult result = *pending_connection_;
	pending_connection_.reset();

	config_.mode = result.mode;
	config_.program = result.program;
	config_.attach_pid = result.attach_pid;
	config_.attach_target.clear();
	config_.args = result.args;
	config_.core_path = result.core_path;
	config_.core_analysis = result.core_analysis;
	config_.packet_monitor_enabled = result.packet_monitor_enabled;
	config_.packet_monitor_filter_src = result.packet_monitor_filter_src;
	config_.packet_monitor_filter_dst = result.packet_monitor_filter_dst;
	if (config_.core_analysis == CoreAnalysisMode::kCoreAnalyzer && !core_analyzer_supported()) {
		config_.core_analysis = CoreAnalysisMode::kGdbOnly;
		set_status(i18n::tr("app.core_analyzer_fallback"));
	}

	if (result.mode == SessionMode::kLaunch && !result.program.empty()) {
		workspace_launch_args_[normalize_path(result.program)] = result.args_line;
		last_launch_program_ = result.program;
		save_workspace_session();
	} else if (result.mode == SessionMode::kAttach && !result.program.empty()) {
		last_attach_program_ = result.program;
		save_workspace_session();
	}

	model_.program = config_.program;
	model_.program_args = config_.args;

	set_status(i18n::tr("app.connecting_dap"));
	ensure_backend_started();
}

void Application::open_connection_wizard() {
	if (!prepare_connection_wizard()) {
		return;
	}
	connection_wizard_state_.reset();
	connection_wizard_state_.open = true;
	set_status(i18n::tr("app.configure_debug"));
}

bool Application::prepare_connection_wizard() {
	if (connection_wizard_state_.open || workspace_wizard_state_.open) {
		return false;
	}
	if (!debug_available_) {
		set_status(i18n::tr("app.debug_unavailable_short"));
		return false;
	}

	if (debugging_started_) {
		exit_debug_mode();
	} else if (app_mode_ == AppMode::kDebug) {
		app_mode_ = AppMode::kNormal;
		layout_state_.console_tabs.selected_tab = ConsolePanelTabs::kTerminal;
	}

	connection_wizard_state_.workspace_root = workspace_.root;
	connection_wizard_state_.launch_args_by_program = workspace_launch_args_;
	connection_wizard_state_.browser.launch_root =
	    connection_wizard_state_.browser.launch_root.empty()
	        ? workspace_.root
	        : connection_wizard_state_.browser.launch_root;
	return true;
}

void Application::open_launch_wizard() {
	if (!prepare_connection_wizard()) {
		return;
	}
	connection_wizard_state_.reset();
	connection_wizard_state_.mode = WizardMode::Launch;
	connection_wizard_state_.mode_selected = 0;
	connection_wizard_state_.step = WizardStep::PickBinary;
	connection_wizard_state_.open = true;
	set_status(i18n::tr("app.configure_debug"));
}

void Application::open_debug_wizard() {
	open_connection_wizard();
}

namespace {

bool program_path_exists(const std::string &path) {
	if (path.empty()) {
		return false;
	}
	std::error_code ec;
	return fs::is_regular_file(fs::path(path), ec);
}

} // namespace

void Application::quick_launch_last() {
	if (!prepare_connection_wizard()) {
		return;
	}
	if (!program_path_exists(last_launch_program_)) {
		open_launch_wizard();
		return;
	}

	ConnectionResult result;
	result.mode = SessionMode::kLaunch;
	result.program = last_launch_program_;
	result.workspace_root = workspace_.root.empty() ? config_.workspace_root : workspace_.root;
	const std::string program_key = normalize_path(last_launch_program_);
	const auto saved = workspace_launch_args_.find(program_key);
	if (saved != workspace_launch_args_.end()) {
		result.args_line = saved->second;
		result.args = split_shell_args(saved->second);
	}
	on_connection_complete(result);
	apply_pending_connection();
	layout_state_.request_ui_tick = true;
}

void Application::quick_attach_last() {
	if (!prepare_connection_wizard()) {
		return;
	}
	if (!program_path_exists(last_attach_program_)) {
		open_debug_wizard();
		return;
	}

	connection_wizard_state_.reset();
	connection_wizard_state_.mode = WizardMode::Attach;
	connection_wizard_state_.mode_selected = 1;
	connection_wizard_state_.step = WizardStep::PickProcess;
	connection_wizard_state_.selected_program = last_attach_program_;
	connection_wizard_state_.all_processes.clear();
	connection_wizard_state_.process_query.clear();
	connection_wizard_state_.process_selected = 0;
	connection_wizard_state_.refresh_process_matches();
	connection_wizard_state_.open = true;
	set_status(i18n::tr("app.configure_debug"));
	layout_state_.request_ui_tick = true;
}

void Application::submit_command(const UiCommand &command) {
	if (backend_started_) {
		backend_->submit(command);
	}
}

void Application::refresh_all_watches() {
	for (const auto &watch : model_.watches) {
		UiCommand command;
		command.kind = UiCommandKind::kAddWatch;
		command.expression = watch.expression;
		if (model_.variables_frame_id >= 0) {
			command.frame_id = model_.variables_frame_id;
		} else if (!model_.stack_frames.empty()) {
			command.frame_id = model_.stack_frames[model_.selected_frame].id;
		}
		submit_command(command);
	}
}

void Application::apply_event(const DebugEvent &event) {
	switch (event.kind) {
	case DebugEventKind::kSessionReady:
		session_ready_ = true;
		model_.status_message = event.text;
		if (connection_wizard_state_.open || !connection_config_complete()) {
			break;
		}
		apply_connection_and_start();
		break;
	case DebugEventKind::kOutput:
		model_.append_console(event.text);
		break;
	case DebugEventKind::kStopped:
		model_.set_stopped(event.thread_id, event.stop_reason);
		if (!event.text.empty() && event.stop_reason != "attach" && event.stop_reason != "pause") {
			model_.append_console("[stopped] " + event.text);
		} else if (event.stop_reason == "breakpoint") {
			model_.append_console("[stopped] breakpoint alcanzado");
		}
		refresh_all_watches();
		break;
	case DebugEventKind::kContinued:
		model_.set_running();
		clear_source_debug_hover(&source_state_.debug_hover);
		break;
	case DebugEventKind::kTerminated:
		model_.set_terminated();
		clear_source_debug_hover(&source_state_.debug_hover);
		debugging_started_ = false;
		if (!event.text.empty()) {
			model_.status_message = event.text;
		}
		break;
	case DebugEventKind::kStackUpdated:
		model_.stack_frames = event.stack_frames;
		if (!model_.stack_frames.empty()) {
			model_.selected_frame = 0;
			model_.active_file = model_.stack_frames.front().file;
			model_.active_line = model_.stack_frames.front().line;
			model_.view_token++;
		}
		break;
	case DebugEventKind::kVariablesUpdated:
		model_.locals = event.variables;
		model_.variable_children.clear();
		if (event.stack_frame_id >= 0) {
			model_.variables_frame_id = event.stack_frame_id;
		}
		break;
	case DebugEventKind::kVariableChildrenUpdated:
		model_.variable_children[event.parent_expression] = event.variables;
		break;
	case DebugEventKind::kHoverValue:
		if (source_state_.debug_hover.fetch_key == event.hover_key) {
			source_state_.debug_hover.title = event.hover_expression;
			source_state_.debug_hover.body_lines.clear();
			if (!event.hover_value.empty()) {
				source_state_.debug_hover.body_lines.push_back(event.hover_value);
			}
			source_state_.debug_hover.waiting_evaluate = false;
			source_state_.debug_hover.visible = !source_state_.debug_hover.body_lines.empty() ||
			                                    !source_state_.debug_hover.title.empty();
		}
		layout_state_.request_ui_tick = true;
		break;
	case DebugEventKind::kEvaluateResult:
		if (!event.text.empty()) {
			model_.append_console(event.text);
		}
		break;
	case DebugEventKind::kCoreAnalyzerResult:
		if (!event.text.empty()) {
			model_.append_core_analyzer_log(event.text);
			if (!model_.core_analyzer_search_query.empty()) {
				apply_core_analyzer_search_result(&model_, event.text,
				                                  model_.core_analyzer_search_query);
				if (!model_.core_analyzer_instances.empty()) {
					layout_state_.core_analyzer_focus =
					    MainLayoutState::CoreAnalyzerFocus::kInstances;
				}
			}
		}
		break;
	case DebugEventKind::kWatchUpdated:
		for (auto &watch : model_.watches) {
			if (watch.expression == event.watch_expression) {
				watch.value = event.watch_value;
			}
		}
		break;
	case DebugEventKind::kInferiorPid:
		layout_state_.packet_monitor_service->set_inferior_pid(event.inferior_pid);
		break;
	case DebugEventKind::kBreakpointsUpdated:
		for (const auto &bp : event.breakpoints) {
			if (bp.verified) {
				model_.breakpoints_by_file[bp.file].insert(bp.line);
			} else if (bp.line > 0 && !bp.file.empty()) {
				auto &lines = model_.breakpoints_by_file[bp.file];
				lines.erase(bp.line);
				if (lines.empty()) {
					model_.breakpoints_by_file.erase(bp.file);
				}
				std::string msg =
				    "[breakpoint] no verificado: " + bp.file + ":" + std::to_string(bp.line);
				if (!bp.message.empty()) {
					msg += " — " + bp.message;
				}
				model_.append_console(msg);
			}
		}
		break;
	case DebugEventKind::kError:
		model_.append_console("[error] " + event.text);
		model_.status_message = event.text;
		break;
	default:
		break;
	}
}

void Application::drain_events() {
	while (auto event = event_queue_.try_pop()) {
		apply_event(*event);
	}
}

void Application::process_index_changes() {
	if (workspace_.root.empty()) {
		return;
	}
	for (const auto &change : indexer_.drain_changes()) {
		if (change.kind == FileIndexChangeKind::Remove) {
			indexer_.remove_file(workspace_.root, change.relative_path);
			symbol_indexer_.remove_file(workspace_.root, change.relative_path);
		} else {
			indexer_.upsert_file(workspace_.root, change.relative_path, change.absolute_path);
			symbol_indexer_.reindex_file(workspace_.root, change.relative_path,
			                             change.absolute_path);
		}
	}
}

bool Application::any_modal_open() const {
	return workspace_wizard_state_.open || external_file_wizard_state_.open ||
	       connection_wizard_state_.open || file_picker_state_.open || symbol_picker_state_.open ||
	       shortcuts_modal_state_.open || settings_modal_state_.open || quit_confirm_state_.open ||
	       open_file_confirm_state_.is_open() || context_menu_active(&layout_state_.context_menu);
}

void Application::apply_app_settings() {
	i18n::set_locale(app_settings_.ui_locale);
	if (has_bundled_clangd()) {
		set_runtime_force_bundled_clangd(app_settings_.force_bundled_clangd);
	}
	if (has_bundled_gdb()) {
		set_runtime_force_bundled_gdb(app_settings_.force_bundled_gdb);
		debug_available_ = gdb_supports_dap();
	}
	if (symbol_provider_) {
		symbol_provider_->set_lsp_enabled(app_settings_.lsp_enabled);
	}
	monitor_log::set_enabled(app_settings_.monitor_enabled);
	configure_glyphs(resolve_icon_mode(app_settings_.icon_mode));
	sync_symbol_workspace_indexer();
	restart_workspace_indexing();
	if (!app_settings_.secondary_panel_enabled && focus_state_.region == FocusRegion::RightPanel) {
		focus_state_.region = FocusRegion::Editor;
		layout_state_.text_input_focus = TextInputFocus::None;
		layout_state_.focus_sync_needed = true;
	}
	if (!layout_state_.explorer_visible && focus_state_.region == FocusRegion::Explorer) {
		focus_state_.region = FocusRegion::Editor;
		layout_state_.text_input_focus = TextInputFocus::None;
		layout_state_.focus_sync_needed = true;
	}
	if (focus_state_.region == FocusRegion::SecondaryEditor &&
	    (!focus_state_.secondary_editor_visible || !focus_state_.secondary_editor_visible())) {
		focus_state_.region = FocusRegion::Editor;
		layout_state_.focus_sync_needed = true;
	}
	workspace_.buffer.view_token++;
	layout_state_.request_ui_tick = true;
}

void Application::toggle_helix_mode() {
	app_settings_.helix_mode_enabled = !app_settings_.helix_mode_enabled;
	app_settings_.save();
	if (layout_state_.reset_helix_editors) {
		layout_state_.reset_helix_editors();
	}
	if (!app_settings_.helix_mode_enabled) {
		layout_state_.helix_status = {};
		layout_state_.editor_helix_prefix_pending = false;
	}
	apply_app_settings();
}

bool Application::handle_focus_shortcuts(const Event &event) {
	auto mark_focus_sync = [this] { layout_state_.focus_sync_needed = true; };

	if (event == Event::CtrlA) {
		if (!layout_state_.explorer_visible) {
			layout_state_.explorer_visible = true;
			layout_state_.request_ui_tick = true;
		}
		focus_state_.region = FocusRegion::Explorer;
		mark_focus_sync();
		return true;
	}
	if (event == Event::CtrlE) {
		focus_state_.region = FocusRegion::Editor;
		layout_state_.text_input_focus = TextInputFocus::None;
		mark_focus_sync();
		return true;
	}
	if (event_is_open_outline_panel(event)) {
		if (!app_settings_.secondary_panel_enabled) {
			app_settings_.secondary_panel_enabled = true;
			app_settings_.save();
			apply_app_settings();
		}
		focus_state_.region = FocusRegion::RightPanel;
		layout_state_.right_panel_active_section = 0;
		layout_state_.text_input_focus = TextInputFocus::None;
		mark_focus_sync();
		return true;
	}
	if (event_is_open_search_panel(event)) {
		layout_state_.console_visible = true;
		layout_state_.console_tabs.selected_tab = ConsolePanelTabs::kSearch;
		focus_state_.region = FocusRegion::Terminal;
		layout_state_.right_sidebar.pending_focus_search = true;
		layout_state_.text_input_focus = TextInputFocus::SearchQuery;
		mark_focus_sync();
		return true;
	}
	if (event_is_open_binary_symbols_panel(event)) {
		layout_state_.console_visible = true;
		layout_state_.console_tabs.selected_tab = ConsolePanelTabs::kBinarySymbols;
		focus_state_.region = FocusRegion::Terminal;
		if (!model_.program.empty()) {
			request_binary_symbols_panel(&layout_state_, model_.program);
		} else {
			request_binary_symbols_panel(&layout_state_, std::string{});
		}
		mark_focus_sync();
		return true;
	}
	if (event == Event::F4) {
		focus_state_.region = FocusRegion::Terminal;
		layout_state_.console_visible = true;
		layout_state_.console_tabs.selected_tab = ConsolePanelTabs::kTerminal;
		layout_state_.text_input_focus = TextInputFocus::Console;
		layout_state_.terminal_start_requested = true;
		mark_focus_sync();
		return true;
	}
	if (!is_editor_focus_region(focus_state_.region)) {
		if (focus_state_.region == FocusRegion::Terminal) {
			if (event_is_alt_left(event)) {
				cycle_console_tab(&layout_state_, &focus_state_, -1, &app_mode_, &git_service_,
				                  &git_panel_state_);
				layout_state_.focus_sync_needed = true;
				return true;
			}
			if (event_is_alt_right(event)) {
				cycle_console_tab(&layout_state_, &focus_state_, 1, &app_mode_, &git_service_,
				                  &git_panel_state_);
				layout_state_.focus_sync_needed = true;
				return true;
			}
		}
		if (event_is_alt_left(event)) {
			if (focus_state_.region == FocusRegion::RightPanel &&
			    !app_settings_.secondary_panel_enabled) {
				focus_state_.region = FocusRegion::Editor;
			} else if (focus_state_.region == FocusRegion::Explorer &&
			           !layout_state_.explorer_visible) {
				focus_state_.region = FocusRegion::Editor;
			} else {
				focus_state_.move_left();
			}
			mark_focus_sync();
			return true;
		}
		if (event_is_alt_right(event)) {
			if (focus_state_.region == FocusRegion::Editor &&
			    !app_settings_.secondary_panel_enabled) {
				return true;
			}
			if (focus_state_.region == FocusRegion::Editor && !layout_state_.explorer_visible) {
				return true;
			}
			focus_state_.move_right();
			mark_focus_sync();
			return true;
		}
		if (event_is_alt_down(event)) {
			focus_state_.move_down();
			layout_state_.console_visible = true;
			layout_state_.terminal_start_requested = true;
			if (layout_state_.console_tabs.selected_tab == ConsolePanelTabs::kTerminal ||
			    (app_mode_ == AppMode::kDebug &&
			     layout_state_.console_tabs.selected_tab == ConsolePanelTabs::kDebug)) {
				layout_state_.text_input_focus = TextInputFocus::Console;
			}
			mark_focus_sync();
			return true;
		}
		if (event_is_alt_up(event)) {
			focus_state_.move_up();
			mark_focus_sync();
			return true;
		}
	}
	return false;
}

int Application::run() {
	if (config_.auto_debug && connection_config_complete() && !config_.show_welcome_screen &&
	    debug_available_) {
		ensure_backend_started();
	}

	const bool ui_smoke = std::getenv("TGDB_UI_SMOKE") != nullptr;
	auto screen = ui_smoke ? ScreenInteractive::TerminalOutput() : ScreenInteractive::Fullscreen();
	if (!ui_smoke) {
		screen.ForceHandleCtrlC(false);
		screen.ForceHandleCtrlZ(false);
		enable_extended_key_reporting();
	}

	std::unique_ptr<EventPoller> event_poller;
	if (!ui_smoke) {
		event_poller = std::make_unique<EventPoller>(&screen, &layout_state_, [this]() {
			if (pending_workspace_load_.has_value()) {
				process_pending_workspace_load();
			}
		});
	} else {
		auto exit_loop = screen.ExitLoopClosure();
		std::thread([exit_loop] {
			std::this_thread::sleep_for(std::chrono::milliseconds(150));
			exit_loop();
		}).detach();
	}

	CommandCallback on_command = [this](const UiCommand &command) { submit_command(command); };

	StopDebugCallback on_stop_debug = [this] { exit_debug_mode(); };

	ShellLaunchConfigProvider shell_launch_config = [this]() {
		return cached_shell_launch_config_;
	};

	auto build_ui = [&]() {
		return MakeMainLayout(
		    &app_mode_, &model_, &workspace_, &secondary_workspace_, &source_state_, &focus_state_,
		    symbol_provider_, on_command, &layout_state_, on_stop_debug, &shell_session_, &indexer_,
		    &symbol_indexer_, shell_launch_config, &git_service_, &git_panel_state_,
		    &welcome_screen_state_, [this] { open_external_file_wizard(); },
		    [this] { open_connection_wizard(); }, [this] { open_workspace_wizard(); });
	};

	auto layout = build_ui();

	layout_state_.terminal_width = [&screen]() { return screen.dimx(); };
	layout_state_.terminal_height = [&screen]() { return screen.dimy(); };
	layout_state_.on_file_saved = [this](const std::string &path) {
		git_service_.invalidate(path);
		if (symbol_provider_) {
			symbol_provider_->on_document_saved(path);
		}
		layout_state_.request_ui_tick = true;
	};
	layout_state_.status_open_settings = [this]() {
		if (settings_modal_state_.open) {
			close_settings_modal(
			    &settings_modal_state_, &app_settings_,
			    [this](const AppSettings &) { apply_app_settings(); },
			    [this](const WorkspaceConfig &config) { apply_workspace_settings(config); },
			    [this](const ClangFormatConfig &) {
				    workspace_.buffer.view_token++;
				    layout_state_.request_ui_tick = true;
			    });
		} else if (!any_modal_open()) {
			open_settings_modal(&settings_modal_state_, app_settings_, workspace_.root,
			                    workspace_config_);
		}
		layout_state_.request_ui_tick = true;
	};
	layout_state_.status_open_shortcuts = [this]() {
		if (shortcuts_modal_state_.open) {
			shortcuts_modal_state_.open = false;
			shortcuts_modal_state_.first_visible = 0;
		} else {
			shortcuts_modal_state_.open = true;
			shortcuts_modal_state_.first_visible = 0;
		}
		layout_state_.request_ui_tick = true;
	};
	layout_state_.status_open_launch = [this]() { open_launch_wizard(); };
	layout_state_.status_reindex_project = [this]() { reindex_project(); };
	layout_state_.status_quick_launch = [this]() { quick_launch_last(); };
	layout_state_.status_open_debug = [this]() { open_debug_wizard(); };
	layout_state_.status_quick_debug = [this]() { quick_attach_last(); };
	layout_state_.status_set_files_visible = [this](bool visible) {
		if (layout_state_.explorer_visible == visible) {
			return;
		}
		layout_state_.explorer_visible = visible;
		if (!visible && focus_state_.region == FocusRegion::Explorer) {
			focus_state_.region = FocusRegion::Editor;
			layout_state_.text_input_focus = TextInputFocus::None;
		}
		layout_state_.focus_sync_needed = true;
		layout_state_.request_ui_tick = true;
	};
	layout_state_.status_set_outline_visible = [this](bool visible) {
		if (app_settings_.secondary_panel_enabled == visible) {
			return;
		}
		app_settings_.secondary_panel_enabled = visible;
		app_settings_.save();
		apply_app_settings();
	};
	layout_state_.status_set_terminal_visible = [this](bool visible) {
		if (layout_state_.console_visible == visible) {
			return;
		}
		layout_state_.console_visible = visible;
		if (visible) {
			layout_state_.terminal_start_requested = true;
		}
		layout_state_.focus_sync_needed = true;
		layout_state_.request_ui_tick = true;
	};
	layout_state_.helix_ide.open_quick_file = [this, &screen]() {
		file_picker_state_.open = true;
		file_picker_state_.query.clear();
		file_picker_state_.selected = 0;
		file_picker_state_.sync_index(indexer_.snapshot(), model_.workspace_root);
		file_picker_state_.mark_matches_dirty();
		file_picker_state_.reset_preview();
		file_picker_state_.arm_ctrl_chord();
		force_immediate_repaint(&layout_state_, &screen);
	};
	layout_state_.helix_ide.open_symbol_picker = [this, &screen]() {
		symbol_picker_state_.open = true;
		symbol_picker_state_.query.clear();
		symbol_picker_state_.selected = 0;
		symbol_picker_state_.loaded_file.clear();
		symbol_picker_state_.catalog_key.clear();
		symbol_picker_state_.catalog.reset();
		symbol_picker_state_.mark_matches_dirty();
		force_immediate_repaint(&layout_state_, &screen);
	};
	layout_state_.helix_ide.save_file = [this]() {
		workspace_.save_buffer();
		if (!workspace_.root.empty() && !workspace_.buffer.path.empty()) {
			if (layout_state_.on_file_saved) {
				layout_state_.on_file_saved(workspace_.buffer.path);
			}
			std::error_code ec;
			const auto rel =
			    std::filesystem::relative(std::filesystem::path(workspace_.buffer.path),
			                              std::filesystem::path(workspace_.root), ec);
			if (!ec) {
				const std::string rel_str = rel.generic_string();
				indexer_.upsert_file(workspace_.root, rel_str, workspace_.buffer.path);
				symbol_indexer_.reindex_file(workspace_.root, rel_str, workspace_.buffer.path);
			}
		}
		layout_state_.request_ui_tick = true;
	};
	layout_state_.helix_ide.request_quit = [this]() {
		workspace_.flush_active_tab();
		secondary_workspace_.flush_active_tab();
		quit_confirm_state_.unsaved_paths.clear();
		for (const auto &path : workspace_.dirty_open_paths()) {
			quit_confirm_state_.unsaved_paths.push_back(path);
		}
		for (const auto &path : secondary_workspace_.dirty_open_paths()) {
			if (std::find(quit_confirm_state_.unsaved_paths.begin(),
			              quit_confirm_state_.unsaved_paths.end(),
			              path) == quit_confirm_state_.unsaved_paths.end()) {
				quit_confirm_state_.unsaved_paths.push_back(path);
			}
		}
		quit_confirm_state_.open = true;
		quit_confirm_state_.selected = quit_confirm_state_.unsaved_paths.empty() ? 0 : 1;
		layout_state_.request_ui_tick = true;
	};
	git_service_.set_update_callback([this] { layout_state_.request_ui_tick = true; });

	file_picker_state_.set_preview_notify([this] { layout_state_.request_ui_tick = true; });
	file_picker_state_.set_repaint_notify(
	    [this, &screen] { force_immediate_repaint(&layout_state_, &screen); });
	symbol_picker_state_.set_search_notify([this] { layout_state_.request_ui_tick = true; });

	auto with_symbol_picker =
	    MakeSymbolPickerOverlay(layout, &workspace_, &symbol_picker_state_, &focus_state_,
	                            symbol_provider_, &symbol_indexer_);

	auto with_picker = MakeFilePickerOverlay(with_symbol_picker, &model_, &workspace_,
	                                         &file_picker_state_, &focus_state_, &indexer_);

	auto with_debug_wizard = MakeConnectionWizardOverlay(
	    with_picker, &connection_wizard_state_, &model_, &layout_state_,
	    [this](const ConnectionResult &result) { on_connection_complete(result); },
	    [&screen] { screen.ExitLoopClosure()(); });

	auto with_workspace_wizard = MakeWorkspaceWizardOverlay(
	    with_debug_wizard, &workspace_wizard_state_, &layout_state_,
	    [this, &screen](const std::string &root) { on_workspace_complete(root, &screen); },
	    [&screen] { screen.ExitLoopClosure()(); });

	auto with_external_file_wizard = MakeExternalFileWizardOverlay(
	    with_workspace_wizard, &external_file_wizard_state_, &layout_state_,
	    [this](const std::string &path) {
		    dismiss_welcome_screen();
		    if (!workspace_initialized_) {
			    const WorkspaceDetectResult detected =
			        resolve_workspace_for_anchor(fs::path(path).parent_path().string());
			    const WorkspaceDetectResult *detect_ptr =
			        app_settings_.workspace_auto_detect_enabled ? &detected : nullptr;
			    set_workspace(detected.workspace_root, detect_ptr);
			    workspace_.open_file(path);
		    } else {
			    workspace_.open_external_file(path);
		    }
		    layout_state_.request_ui_tick = true;
	    });

	auto with_shortcuts =
	    MakeShortcutsModalOverlay(with_external_file_wizard, &shortcuts_modal_state_);

	SettingsApplyCallback on_settings_apply = [this](const AppSettings &) { apply_app_settings(); };
	WorkspaceSettingsApplyCallback on_workspace_apply = [this](const WorkspaceConfig &config) {
		apply_workspace_settings(config);
	};
	ClangFormatApplyCallback on_clang_format_apply = [this](const ClangFormatConfig &config) {
		clang_format_config_ = config;
		editor_indent::apply(config);
		workspace_.buffer.view_token++;
		secondary_workspace_.buffer.view_token++;
		layout_state_.request_ui_tick = true;
	};
	auto with_settings =
	    MakeSettingsModalOverlay(with_shortcuts, &settings_modal_state_, &app_settings_,
	                             on_settings_apply, on_workspace_apply, on_clang_format_apply);

	auto with_quit_confirm =
	    MakeQuitConfirmOverlay(with_settings, &quit_confirm_state_, &layout_state_,
	                           &shutdown_state_, [this, &screen] { begin_shutdown(&screen); });

	workspace_.open_file_confirm = &open_file_confirm_state_;
	secondary_workspace_.open_file_confirm = &open_file_confirm_state_;
	auto with_open_file_confirm =
	    MakeOpenFileConfirmOverlay(with_quit_confirm, &open_file_confirm_state_, &layout_state_,
	                               &workspace_, [this](const std::string &path, int line, int col) {
		                               model_.active_file = path;
		                               model_.active_line = line + 1;
		                               model_.view_token++;
		                               layout_state_.request_ui_tick = true;
	                               });

	auto with_context_menu = MakeContextMenuOverlay(
	    with_open_file_confirm, &layout_state_.context_menu, &workspace_, &secondary_workspace_,
	    &model_, &focus_state_, &layout_state_, symbol_provider_, &indexer_, &symbol_indexer_,
	    &workspace_config_, [this]() {
		    const auto &handlers = editor_handlers_for(&layout_state_, focus_state_.region);
		    if (handlers.visible_line_count) {
			    return handlers.visible_line_count();
		    }
		    return 24;
	    });

	auto inner_root = CatchEvent(with_context_menu, [this, &screen, on_command,
	                                                 &event_poller](const Event &event) {
		try {
			const bool editor_browse_active = app_mode_ == AppMode::kNormal ||
			                                  model_.is_post_mortem || app_mode_ == AppMode::kDebug;

			if (shutdown_state_.is_active()) {
				return false;
			}

		if (event != Event::Custom && event_poller) {
			event_poller->notify_user_activity();
		}

		if (event == Event::Custom) {
			++layout_state_.ui_custom_tick;
			monitor_log::heartbeat();
				if (!any_modal_open()) {
					TGDB_MON_SCOPE("ui", "tick.apply_pending_connection");
					apply_pending_connection();
				}
				{
					TGDB_MON_SCOPE("ui", "tick.process_index_changes");
					process_index_changes();
				}
				{
					TGDB_MON_SCOPE("ui", "tick.process_build_environment_updates");
					process_build_environment_updates();
				}
				{
					TGDB_MON_SCOPE("ui", "tick.drain_events");
					drain_events();
				}
			cursor_blink::tick();
			layout_state_.clickable.tick();
			if (layout_state_.console_visible && layout_state_.terminal_tick_callback) {
					TGDB_MON_SCOPE("ui", "tick.terminal");
					layout_state_.terminal_tick_callback();
				}
				if (app_mode_ == AppMode::kDebug) {
					layout_state_.packet_monitor_service->tick();
				}
				if (symbol_provider_) {
					TGDB_MON_SCOPE("ui", "tick.drain_async_results");
					if (symbol_provider_->drain_async_results()) {
						workspace_.buffer.view_token++;
						secondary_workspace_.buffer.view_token++;
					}
				}
				if (layout_state_.primary_editor.tick_callback && !layout_state_.welcome_visible) {
					TGDB_MON_SCOPE("ui", "tick.primary_editor");
					layout_state_.primary_editor.tick_callback();
				}
				if (layout_state_.secondary_editor.tick_callback &&
				    !layout_state_.welcome_visible) {
					TGDB_MON_SCOPE("ui", "tick.secondary_editor");
					layout_state_.secondary_editor.tick_callback();
				}
				if (layout_state_.source_tick_callback && app_mode_ == AppMode::kDebug) {
					layout_state_.source_tick_callback();
				}
				{
					TGDB_MON_SCOPE("ui", "tick.git");
					git_service_.tick();
				}
			drain_ui_tasks();
			if (git_tab_active(&layout_state_)) {
					GitPanelEnsureSelectedDiff(&git_service_, &git_panel_state_);
				}
				if (layout_state_.outline_tick_callback) {
					TGDB_MON_SCOPE("ui", "tick.outline");
					layout_state_.outline_tick_callback();
				}
				const bool sample_perf =
				    layout_state_.console_visible &&
				    layout_state_.console_tabs.selected_tab == ConsolePanelTabs::kPerformance;
				if (layout_state_.ui_heartbeat.exchange(false, std::memory_order_acquire)) {
					TGDB_MON_SCOPE("ui", "tick.performance_sampler");
					layout_state_.performance_sampler.on_frame(sample_perf);
				}
				bool swallow_call_hierarchy_custom = false;
				if (layout_state_.right_sidebar.pending_call_hierarchy &&
				    layout_state_.call_hierarchy_key_handler) {
					layout_state_.call_hierarchy_key_handler(event);
					swallow_call_hierarchy_custom = true;
				}
				if ((search_tab_active(&layout_state_) ||
				     is_search_input_focus(layout_state_.text_input_focus)) &&
				    layout_state_.search_key_handler) {
					layout_state_.search_key_handler(event);
				}
				if (layout_state_.binary_symbols_key_handler &&
				    (binary_symbols_tab_active(&layout_state_) ||
				     layout_state_.binary_symbols_pending.open_tab ||
				     !layout_state_.binary_symbols_pending.binary_path.empty() ||
				     layout_state_.binary_symbols_pending.refresh)) {
					layout_state_.binary_symbols_key_handler(event);
				}
				if (swallow_call_hierarchy_custom) {
					return true;
				}
				return false;
			}

			if (!event.is_character() && !event.is_mouse()) {
				std::ostringstream key_msg;
				key_msg << "key event=" << event.input()
				        << " focus=" << focus_state_.region_label();
				TGDB_MON("ui", key_msg.str());
			}

			// Tide app shortcuts must run before any_modal_open() and editor interceptors.
			if (event_is_ctrl_p(event)) {
				if (event_is_kitty_key_release(event)) {
					return false;
				}
				if (file_picker_state_.open) {
					if (!file_picker_state_.matches.empty()) {
						file_picker_state_.selected =
						    (file_picker_state_.selected + 1) %
						    static_cast<int>(file_picker_state_.matches.size());
						file_picker_state_.ctrl_chord_active = true;
						file_picker_state_.update_preview_for_selection(model_.workspace_root);
					}
					force_immediate_repaint(&layout_state_, &screen);
					return true;
				}
				file_picker_state_.open = true;
				file_picker_state_.query.clear();
				file_picker_state_.selected = 0;
				file_picker_state_.sync_index(indexer_.snapshot(), model_.workspace_root);
				file_picker_state_.mark_matches_dirty();
				file_picker_state_.reset_preview();
				file_picker_state_.arm_ctrl_chord();
				force_immediate_repaint(&layout_state_, &screen);
				return true;
			}
			if (event_is_ctrl_o(event)) {
				if (event_is_kitty_key_release(event)) {
					return false;
				}
				if (symbol_picker_state_.open && !symbol_picker_state_.matches.empty()) {
					symbol_picker_state_.selected =
					    (symbol_picker_state_.selected + 1) %
					    static_cast<int>(symbol_picker_state_.matches.size());
					layout_state_.request_ui_tick = true;
					return true;
				}
				symbol_picker_state_.open = true;
				symbol_picker_state_.query.clear();
				symbol_picker_state_.selected = 0;
				symbol_picker_state_.loaded_file.clear();
				symbol_picker_state_.catalog_key.clear();
				symbol_picker_state_.catalog.reset();
				symbol_picker_state_.mark_matches_dirty();
				force_immediate_repaint(&layout_state_, &screen);
				return true;
			}

			if (event_is_f1(event)) {
				if (external_file_wizard_state_.open) {
					external_file_wizard_state_.open = false;
				} else if (!any_modal_open()) {
					open_external_file_wizard();
				}
				layout_state_.request_ui_tick = true;
				return true;
			}

			if (event_is_open_shortcuts_modal(event)) {
				if (shortcuts_modal_state_.open) {
					shortcuts_modal_state_.open = false;
					shortcuts_modal_state_.first_visible = 0;
				} else if (!any_modal_open()) {
					shortcuts_modal_state_.open = true;
					shortcuts_modal_state_.first_visible = 0;
				}
				layout_state_.request_ui_tick = true;
				return true;
			}

			if (app_mode_ == AppMode::kNormal && event == Event::F10) {
				if (settings_modal_state_.open) {
					close_settings_modal(
					    &settings_modal_state_, &app_settings_,
					    [this](const AppSettings &) { apply_app_settings(); },
					    [this](const WorkspaceConfig &config) { apply_workspace_settings(config); },
					    [this](const ClangFormatConfig &) {
						    workspace_.buffer.view_token++;
						    layout_state_.request_ui_tick = true;
					    });
				} else if (!any_modal_open()) {
					open_settings_modal(&settings_modal_state_, app_settings_, workspace_.root,
					                    workspace_config_);
				}
				return true;
			}

			if (app_mode_ == AppMode::kNormal && event == Event::F6) {
				toggle_helix_mode();
				return true;
			}

			if (settings_modal_state_.open && event.is_mouse()) {
				Event mouse_event = event;
				if (settings_modal_handle_mouse(&settings_modal_state_, mouse_event)) {
					layout_state_.request_ui_tick = true;
					return true;
				}
			}

			if (layout_state_.status_layout_popover.open) {
				if (HandleStatusLayoutPopoverKeys(&layout_state_.status_layout_popover, event)) {
					layout_state_.request_ui_tick = true;
					return true;
				}
				if (event.is_mouse()) {
					Event mouse_event = event;
					if (layout_state_.status_bar_mouse_handler &&
					    layout_state_.status_bar_mouse_handler(mouse_event)) {
						layout_state_.request_ui_tick = true;
						return true;
					}
				}
			}

			if (any_modal_open()) {
				return false;
			}

			if (layout_state_.welcome_visible && app_mode_ == AppMode::kNormal) {
				Event welcome_event = event;
				if (layout_state_.welcome_mouse_handler && event.is_mouse() &&
				    layout_state_.welcome_mouse_handler(welcome_event)) {
					layout_state_.request_ui_tick = true;
					return true;
				}
				if (!event.is_mouse() && layout_state_.welcome_key_handler &&
				    layout_state_.welcome_key_handler(welcome_event)) {
					layout_state_.request_ui_tick = true;
					return true;
				}
				if (handle_focus_shortcuts(event)) {
					return true;
				}
				if (!event.is_mouse() && !event_is_f1(event) &&
				    !event_is_open_shortcuts_modal(event) && event != Event::F2 &&
				    event != Event::F3 && event != Event::CtrlQ) {
					return false;
				}
			}

			if (layout_state_.primary_editor.modifier_handler) {
				Event mod_event = event;
				layout_state_.primary_editor.modifier_handler(mod_event);
			}
			if (layout_state_.secondary_editor.modifier_handler) {
				Event mod_event = event;
				layout_state_.secondary_editor.modifier_handler(mod_event);
			}

	// Global throttle for pure mouse-move (no button pressed) repaints: cap at ~30 Hz.
	// Clicks (Pressed/Released), drags (Moved + button held), and wheel events are
	// always posted immediately. The EventPoller at 20 Hz covers the remaining renders.
	static int64_t s_mouse_move_post_ms = 0;
	const bool is_pure_move_event = event.is_mouse() &&
	    const_cast<Event &>(event).mouse().motion == Mouse::Moved &&
	    const_cast<Event &>(event).mouse().button == Mouse::None;
		const auto post_custom_throttled = [&] {
			if (!is_pure_move_event) {
				screen.Post(Event::Custom);
				return;
			}
			const int64_t now = steady_now_ms();
			if (now - s_mouse_move_post_ms >= 33) {
				s_mouse_move_post_ms = now;
				screen.Post(Event::Custom);
			}
		};

		if (event.is_mouse() && layout_state_.split_mouse_handler &&
		    layout_state_.split_mouse_handler(event)) {
			post_custom_throttled();
			return true;
		}

		if (editor_browse_active && !layout_state_.welcome_visible && event.is_mouse() &&
		    layout_state_.explorer_mouse_handler &&
		    layout_state_.explorer_mouse_handler(event)) {
			post_custom_throttled();
			return true;
		}

		if (editor_browse_active && !layout_state_.welcome_visible && event.is_mouse() &&
		    layout_state_.sidebar_mouse_handler && layout_state_.sidebar_mouse_handler(event)) {
			post_custom_throttled();
			return true;
		}

		if (editor_browse_active && !layout_state_.welcome_visible && event.is_mouse() &&
		    layout_state_.outline_mouse_handler && layout_state_.outline_mouse_handler(event)) {
			post_custom_throttled();
			layout_state_.focus_sync_needed = true;
			return true;
		}

		if (event.is_mouse() && layout_state_.status_bar_mouse_handler &&
		    layout_state_.status_bar_mouse_handler(event)) {
			post_custom_throttled();
			return true;
		}

		if (editor_browse_active && !layout_state_.welcome_visible && event.is_mouse()) {
			bool chrome_handled = false;
			if (layout_state_.primary_editor.chrome_mouse_handler &&
			    layout_state_.primary_editor.chrome_mouse_handler(event)) {
				chrome_handled = true;
			}
			if (layout_state_.secondary_editor.chrome_mouse_handler &&
			    layout_state_.secondary_editor.chrome_mouse_handler(event)) {
				chrome_handled = true;
			}
			if (chrome_handled) {
				post_custom_throttled();
				return true;
			}
		}

		if (handle_focus_shortcuts(event)) {
			screen.Post(Event::Custom);
			return true;
		}

		if (layout_state_.console_visible && event.is_mouse() &&
		    git_tab_active(&layout_state_) && layout_state_.git_mouse_handler &&
		    layout_state_.git_mouse_handler(event)) {
			post_custom_throttled();
			layout_state_.focus_sync_needed = true;
			return true;
		}

		if (layout_state_.console_visible && event.is_mouse() &&
		    packet_monitor_tab_active(&layout_state_) &&
		    layout_state_.packet_monitor_mouse_handler &&
		    layout_state_.packet_monitor_mouse_handler(event)) {
			post_custom_throttled();
			layout_state_.focus_sync_needed = true;
			return true;
		}

		if (layout_state_.console_visible && event.is_mouse() &&
		    problems_tab_active(&layout_state_) && layout_state_.problems_key_handler &&
		    layout_state_.problems_key_handler(event)) {
			post_custom_throttled();
			layout_state_.focus_sync_needed = true;
			return true;
		}

		if (layout_state_.console_visible && layout_state_.console_mouse_handler &&
		    layout_state_.console_mouse_handler(event)) {
			post_custom_throttled();
			layout_state_.focus_sync_needed = true;
			return true;
		}

		if (app_mode_ == AppMode::kDebug && event.is_mouse()) {
			bool handled = false;
			if (!model_.is_post_mortem && layout_state_.source_mouse_handler &&
			    layout_state_.source_mouse_handler(event)) {
				handled = true;
			}
			if (layout_state_.outline_mouse_handler &&
			    layout_state_.outline_mouse_handler(event)) {
				handled = true;
			}
			if (layout_state_.watches_mouse_handler &&
			    layout_state_.watches_mouse_handler(event)) {
				handled = true;
			}
			if (handled || layout_state_.request_ui_tick) {
				post_custom_throttled();
				layout_state_.focus_sync_needed = true;
				return handled;
			}
		}

		if (editor_browse_active && !layout_state_.welcome_visible && event.is_mouse()) {
			bool mouse_handled = false;
			if (layout_state_.primary_editor.mouse_handler &&
			    layout_state_.primary_editor.mouse_handler(event)) {
				mouse_handled = true;
			}
			if (layout_state_.secondary_editor.mouse_handler &&
			    layout_state_.secondary_editor.mouse_handler(event)) {
				mouse_handled = true;
			}
			if (mouse_handled) {
				post_custom_throttled();
				layout_state_.focus_sync_needed = true;
				return true;
			}
		}

			// Intercept console keys before editor (FTXUI focus may still be on editor).
			const bool terminal_tab =
			    app_mode_ != AppMode::kDebug ||
			    layout_state_.console_tabs.selected_tab == ConsolePanelTabs::kTerminal;
			const bool shell_terminal_focus = terminal_tab &&
			                                  focus_state_.region == FocusRegion::Terminal &&
			                                  shell_session_.running();
		if ((layout_state_.text_input_focus == TextInputFocus::Console ||
		     shell_terminal_focus) &&
		    !event_is_tide_global_shortcut(event) && layout_state_.console_key_handler &&
		    layout_state_.console_key_handler(event)) {
			screen.Post(Event::Custom);
			return true;
		}
			if (app_mode_ == AppMode::kNormal && event_is_workspace_search_with_selection(event) &&
			    is_editor_focus_region(focus_state_.region)) {
				WorkspaceModel &active_workspace =
				    focus_state_.region == FocusRegion::SecondaryEditor ? secondary_workspace_
				                                                        : workspace_;
				active_workspace.ensure_buffer();
				const std::string needle =
				    selection_text(active_workspace.buffer, active_workspace.buffer.primary());
				focus_search_with_filter(&layout_state_, needle, "");
				focus_state_.region = FocusRegion::Terminal;
				screen.Post(Event::Custom);
				return true;
			}
			const auto &active_editor_handlers =
			    editor_handlers_for(&layout_state_, focus_state_.region);
			if (editor_browse_active && event_is_ctrl_f(event) &&
			    is_editor_focus_region(focus_state_.region) && active_editor_handlers.key_handler &&
			    active_editor_handlers.key_handler(event)) {
				screen.Post(Event::Custom);
				return true;
			}
			if (editor_browse_active && layout_state_.editor_completion_open &&
			    event == Event::Escape && active_editor_handlers.key_handler &&
			    active_editor_handlers.key_handler(event)) {
				screen.Post(Event::Custom);
				return true;
			}
			if (editor_browse_active &&
			    is_editor_chrome_input_focus(layout_state_.text_input_focus) &&
			    active_editor_handlers.key_handler && active_editor_handlers.key_handler(event)) {
				screen.Post(Event::Custom);
				return true;
			}
			if ((is_search_input_focus(layout_state_.text_input_focus) ||
			     search_tab_active(&layout_state_)) &&
			    layout_state_.search_key_handler && layout_state_.search_key_handler(event)) {
				screen.Post(Event::Custom);
				return true;
			}
			if (call_hierarchy_tab_active(&layout_state_) &&
			    focus_state_.region == FocusRegion::Terminal &&
			    layout_state_.call_hierarchy_key_handler &&
			    layout_state_.call_hierarchy_key_handler(event)) {
				screen.Post(Event::Custom);
				return true;
			}
			if (problems_tab_active(&layout_state_) &&
			    focus_state_.region == FocusRegion::Terminal &&
			    !is_editor_chrome_input_focus(layout_state_.text_input_focus) &&
			    layout_state_.problems_key_handler && layout_state_.problems_key_handler(event)) {
				screen.Post(Event::Custom);
				return true;
			}
			if ((is_binary_symbols_input_focus(layout_state_.text_input_focus) ||
			     binary_symbols_tab_active(&layout_state_)) &&
			    !is_editor_chrome_input_focus(layout_state_.text_input_focus) &&
			    layout_state_.binary_symbols_key_handler &&
			    layout_state_.binary_symbols_key_handler(event)) {
				screen.Post(Event::Custom);
				return true;
			}
			if (git_tab_active(&layout_state_) && focus_state_.region == FocusRegion::Terminal &&
			    !is_editor_chrome_input_focus(layout_state_.text_input_focus) &&
			    layout_state_.git_key_handler && layout_state_.git_key_handler(event)) {
				screen.Post(Event::Custom);
				return true;
			}
			if (core_analyzer_tab_active(&layout_state_) &&
			    focus_state_.region == FocusRegion::Terminal &&
			    !is_editor_chrome_input_focus(layout_state_.text_input_focus) &&
			    layout_state_.core_analyzer_key_handler &&
			    layout_state_.core_analyzer_key_handler(event)) {
				screen.Post(Event::Custom);
				return true;
			}
			if (packet_monitor_tab_active(&layout_state_) &&
			    focus_state_.region == FocusRegion::Terminal &&
			    !is_editor_chrome_input_focus(layout_state_.text_input_focus) &&
			    layout_state_.packet_monitor_key_handler &&
			    layout_state_.packet_monitor_key_handler(event)) {
				screen.Post(Event::Custom);
				return true;
			}
			if (app_mode_ == AppMode::kDebug &&
			    (focus_state_.region == FocusRegion::RightPanel ||
			     is_watch_input_focus(layout_state_.text_input_focus)) &&
			    layout_state_.watches_key_handler && layout_state_.watches_key_handler(event)) {
				screen.Post(Event::Custom);
				layout_state_.focus_sync_needed = true;
				return true;
			}
			if (app_mode_ == AppMode::kDebug && !model_.is_post_mortem &&
			    focus_state_.region == FocusRegion::Editor &&
			    !is_watch_input_focus(layout_state_.text_input_focus) &&
			    layout_state_.text_input_focus != TextInputFocus::Console &&
			    layout_state_.source_key_handler && layout_state_.source_key_handler(event)) {
				screen.Post(Event::Custom);
				return true;
			}
			if (is_editor_focus_region(focus_state_.region) && editor_browse_active &&
			    !is_search_input_focus(layout_state_.text_input_focus) &&
			    !is_watch_input_focus(layout_state_.text_input_focus) &&
			    !is_editor_chrome_input_focus(layout_state_.text_input_focus) &&
			    layout_state_.text_input_focus != TextInputFocus::Console &&
			    active_editor_handlers.key_handler) {
				if (active_editor_handlers.key_handler(event)) {
					layout_state_.request_ui_tick = true;
					screen.PostEvent(Event::Custom);
					
					return true;
				}
			}

			if (layout_state_.editor_helix_prefix_pending &&
			    is_editor_focus_region(focus_state_.region) && editor_browse_active &&
			    !event_has_ctrl_modifier(event) && event != Event::CtrlP &&
			    !event_is_ctrl_p(event) && active_editor_handlers.key_handler &&
			    active_editor_handlers.key_handler(event)) {
				layout_state_.request_ui_tick = true;
				screen.PostEvent(Event::Custom);
				return true;
			}
			if (layout_state_.editor_helix_prefix_pending &&
			    is_editor_focus_region(focus_state_.region) && editor_browse_active &&
			    !event_has_ctrl_modifier(event) && event != Event::CtrlP &&
			    !event_is_ctrl_p(event) && !event_is_tide_app_shortcut(event)) {
				return true;
			}

			// Tab nunca cambia foco entre paneles; en terminal va al shell, en editor indenta.
			if (event == Event::Tab || event == Event::TabReverse) {
				const bool terminal_tab =
				    app_mode_ != AppMode::kDebug ||
				    layout_state_.console_tabs.selected_tab == ConsolePanelTabs::kTerminal;
				if (terminal_tab &&
				    layout_state_.console_tabs.selected_tab != ConsolePanelTabs::kBinarySymbols &&
				    layout_state_.console_tabs.selected_tab != ConsolePanelTabs::kSearch &&
				    (layout_state_.text_input_focus == TextInputFocus::Console ||
				     (focus_state_.region == FocusRegion::Terminal && shell_session_.running())) &&
				    shell_session_.running()) {
					if (event == Event::Tab) {
						shell_session_.write_raw("\t");
					} else {
						shell_session_.write_raw("\x1b[Z");
					}
					if (layout_state_.terminal_follow_input_callback) {
						layout_state_.terminal_follow_input_callback();
					}
					force_immediate_repaint(&layout_state_, &screen);
					return true;
				}
				if (is_editor_focus_region(focus_state_.region) &&
				    (editor_browse_active || app_mode_ == AppMode::kDebug) &&
				    layout_state_.text_input_focus != TextInputFocus::Console &&
				    !is_search_input_focus(layout_state_.text_input_focus) &&
				    !is_watch_input_focus(layout_state_.text_input_focus) &&
				    !is_editor_chrome_input_focus(layout_state_.text_input_focus) &&
				    active_editor_handlers.key_handler &&
				    active_editor_handlers.key_handler(event)) {
					layout_state_.request_ui_tick = true;
					screen.PostEvent(Event::Custom);
					return true;
				}
				return false;
			}

			if (event == Event::CtrlQ) {
				workspace_.flush_active_tab();
				secondary_workspace_.flush_active_tab();
				quit_confirm_state_.unsaved_paths.clear();
				for (const auto &path : workspace_.dirty_open_paths()) {
					quit_confirm_state_.unsaved_paths.push_back(path);
				}
				for (const auto &path : secondary_workspace_.dirty_open_paths()) {
					if (std::find(quit_confirm_state_.unsaved_paths.begin(),
					              quit_confirm_state_.unsaved_paths.end(),
					              path) == quit_confirm_state_.unsaved_paths.end()) {
						quit_confirm_state_.unsaved_paths.push_back(path);
					}
				}
				quit_confirm_state_.open = true;
				quit_confirm_state_.selected = quit_confirm_state_.unsaved_paths.empty() ? 0 : 1;
				layout_state_.request_ui_tick = true;
				return true;
			}

			if (app_mode_ == AppMode::kNormal && event == Event::CtrlB) {
				workspace_.ensure_buffer();
				if (!workspace_.buffer.path.empty()) {
					ToggleBreakpointAtFile(&model_, workspace_.buffer.path,
					                       workspace_.buffer.primary_line() + 1, on_command);
					layout_state_.request_ui_tick = true;
				}
				return true;
			}

			if (app_mode_ == AppMode::kDebug) {
				UiCommand command;
				if (event == Event::CtrlB) {
					if (!model_.active_file.empty() && model_.active_line > 0) {
						ToggleBreakpointAtLine(&model_, model_.active_line, on_command);
					}
					return true;
				}
				if (event == Event::F5 && !model_.is_post_mortem) {
					command.kind = UiCommandKind::kContinue;
					submit_command(command);
					layout_state_.clickable.trigger_press(press_id::kWatchesPlay);
					layout_state_.request_ui_tick = true;
					return true;
				}
				if (event == Event::F10 && !model_.is_post_mortem) {
					command.kind = UiCommandKind::kNext;
					submit_command(command);
					return true;
				}
				if (event == Event::F11 && !model_.is_post_mortem) {
					command.kind = UiCommandKind::kStepIn;
					submit_command(command);
					return true;
				}
				if (event == Event::Special({24}) && !model_.is_post_mortem) {
					command.kind = UiCommandKind::kStepOut;
					submit_command(command);
					return true;
				}
			}

			if (event == Event::F2) {
				open_connection_wizard();
				return true;
			}
			if (event == Event::F3) {
				open_workspace_wizard();
				return true;
			}
			if (app_mode_ != AppMode::kDebug && event == Event::F5) {
				if (!layout_state_.console_visible) {
					layout_state_.console_visible = true;
				} else if (layout_state_.console_tabs.selected_tab == ConsolePanelTabs::kGit) {
					layout_state_.console_visible = false;
					layout_state_.focus_sync_needed = true;
					return true;
				}
				layout_state_.console_tabs.selected_tab = ConsolePanelTabs::kGit;
				GitPanelActivate(&git_service_, &git_panel_state_);
				focus_state_.region = FocusRegion::Terminal;
				layout_state_.text_input_focus = TextInputFocus::None;
				layout_state_.focus_sync_needed = true;
				layout_state_.request_ui_tick = true;
				return true;
			}
			if (event == Event::F9) {
				if (!layout_state_.console_visible) {
					layout_state_.console_visible = true;
				} else if (layout_state_.console_tabs.selected_tab == ConsolePanelTabs::kProblems) {
					layout_state_.console_visible = false;
					return true;
				}
				layout_state_.console_tabs.selected_tab = ConsolePanelTabs::kProblems;
				layout_state_.focus_sync_needed = true;
				return true;
			}
			if (event == Event::CtrlT) {
				layout_state_.console_visible = !layout_state_.console_visible;
				return true;
			}
			if (event == Event::Escape) {
				if (is_editor_focus_region(focus_state_.region)) {
					return false;
				}
				layout_state_.text_input_focus = TextInputFocus::None;
				return true;
			}

		return false;
	} catch (const std::exception &e) {
		model_.append_console(std::string("[crash] ") + e.what());
		set_status(i18n::tr_fmt("app.error_prefix", {e.what()}));
		print_current_backtrace(e.what());
		return true;
	}
});

auto root = MakeShutdownOverlay(inner_root, &shutdown_state_, &shutdown_overlay_state_,
	                                &layout_state_, [this] { force_exit(); });

	layout_state_.schedule_ui_tick = [this]() { layout_state_.request_ui_tick = true; };
	layout_state_.terminal_wake_callback = [this, &screen]() {
		layout_state_.request_ui_tick = true;
		screen.PostEvent(Event::Custom);
	};

	screen.Loop(WrapUiTickPost(root, &layout_state_, &screen));

	if (!ui_smoke) {
		disable_extended_key_reporting();
	}

	if (shutdown_thread_.joinable()) {
		shutdown_thread_.join();
	}

	if (backend_ && !shutdown_performed_) {
		backend_->stop();
		backend_started_ = false;
	}
	return 0;
}

} // namespace tgdb