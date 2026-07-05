#include "app/application.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "i18n/locale.hpp"
#include "i18n/tr.hpp"
#include "util/core_analyzer_support.hpp"
#include "util/crash_handler.hpp"

namespace {

bool config_is_complete(const tgdb::AppConfig &config) {
  if (config.program.empty()) {
    return false;
  }
  if (config.mode == tgdb::SessionMode::kAttach) {
    return config.attach_pid > 0 || !config.attach_target.empty();
  }
  if (config.mode == tgdb::SessionMode::kCore) {
    return !config.core_path.empty();
  }
  return true;   
}

bool path_is_existing_directory(const std::string &path) {
  std::error_code ec;
  return std::filesystem::is_directory(std::filesystem::status(path, ec));
}

bool path_is_existing_regular_file(const std::string &path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(std::filesystem::status(path, ec));
}

bool path_looks_like_source_or_text(const std::filesystem::path &path) {
  static const char *kExtensions[] = {
      ".cpp", ".cxx",   ".cc",   ".c",    ".h",    ".hpp",  ".hxx",
      ".hh",  ".inl",   ".md",   ".txt",  ".json", ".yaml", ".yml",
      ".xml", ".cmake", ".toml", ".rs",   ".py",   ".sh",   ".bash",
      ".s",   ".asm",   ".vert", ".frag", ".glsl",
  };
  const std::string ext = path.extension().string();
  for (const char *candidate : kExtensions) {
    if (ext == candidate) {
      return true;
    }
  }
  return false;
}

bool path_is_executable_file(const std::string &path) {
  std::error_code ec;
  const auto perms = std::filesystem::status(path, ec).permissions();
  if (ec) {
    return false;
  }
  using std::filesystem::perms;
  const auto executable =
      perms::owner_exec | perms::group_exec | perms::others_exec;
  return (perms & executable) != perms::none;
}

bool path_is_ide_open_file(const std::string &path) {
  if (!path_is_existing_regular_file(path)) {
    return false;
  }
  if (path_looks_like_source_or_text(std::filesystem::path(path))) {
    return true;
  }
  return !path_is_executable_file(path);
}

void print_usage() {
  using tgdb::i18n::tr;
  std::cerr << tr("cli.usage") << '\n'
            << tr("cli.options_header") << '\n'
            << tr("cli.opt.cwd") << '\n'
            << tr("cli.opt.args") << '\n'
            << tr("cli.opt.attach") << '\n'
            << tr("cli.opt.target") << '\n'
            << tr("cli.opt.core") << '\n'
            << tr("cli.opt.core_analyzer") << '\n'
            << tr("cli.opt.help") << '\n'
            << '\n'
            << tr("cli.help.no_args") << '\n'
            << tr("cli.help.positional") << '\n'
            << tr("cli.help.shortcuts") << '\n'
            << '\n'
            << tr("cli.examples_header") << '\n'
            << tr("cli.example.welcome") << '\n'
            << tr("cli.example.dot") << '\n'
            << tr("cli.example.open_file") << '\n'
            << tr("cli.example.cwd") << '\n'
            << tr("cli.example.program") << '\n'
            << tr("cli.example.attach") << '\n';
}

} // namespace

int main(int argc, char **argv) {
#if !defined(_WIN32)
  setenv("COLORTERM", "truecolor", 1);
  if (std::getenv("TERM") == nullptr) {
    setenv("TERM", "xterm-256color", 1);
  }
#endif

  tgdb::i18n::set_locale(tgdb::i18n::UiLocale::kAuto);

  tgdb::AppConfig config;

  {
    std::error_code ec;
    config.launch_directory =
        std::filesystem::absolute(std::filesystem::current_path(ec)).string();
  }

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      print_usage();
      return 0;
    }
    if (arg == "--cwd" && i + 1 < argc) {
      config.workspace_root = argv[++i];
      config.show_welcome_screen = false;
      continue;
    }
    if (arg == "--attach" && i + 1 < argc) {
      config.mode = tgdb::SessionMode::kAttach;
      config.attach_pid = std::stoi(argv[++i]);
      continue;
    }
    if (arg == "--target" && i + 1 < argc) {
      config.mode = tgdb::SessionMode::kAttach;
      config.attach_target = argv[++i];
      continue;
    }
    if (arg == "--core" && i + 1 < argc) {
      config.mode = tgdb::SessionMode::kCore;
      config.core_path = argv[++i];
      continue;
    }
    if (arg == "--core-analyzer") {
      config.core_analysis = tgdb::CoreAnalysisMode::kCoreAnalyzer;
      continue;
    }
    if (arg == "--args") {
      while (i + 1 < argc) {
        config.args.push_back(argv[++i]);
      }
      break;
    }
    if (arg.rfind('-', 0) == 0) {
      std::cerr << tgdb::i18n::tr_fmt("cli.unknown_option", {arg}) << '\n';
      print_usage();
      return 1;
    }
    if (path_is_existing_directory(arg)) {
      config.workspace_root = arg;
      config.show_welcome_screen = false;
      continue;
    }
    if (path_is_ide_open_file(arg)) {
      if (!config.initial_file.empty()) {
        std::cerr << tgdb::i18n::tr("cli.too_many_args") << '\n';
        print_usage();
        return 1;
      }
      config.initial_file = arg;
      config.show_welcome_screen = false;
      continue;
    }
    if (!config.program.empty()) {
      std::cerr << tgdb::i18n::tr("cli.too_many_args") << '\n';
      print_usage();
      return 1;
    }
    config.program = arg;
  }

  if (config.workspace_root.empty() && config.program.empty() &&
      config.initial_file.empty()) {
    config.show_welcome_screen = true;
  } else if (!config.workspace_root.empty() && config.program.empty()) {
    config.show_welcome_screen = false;
  } else if (!config.initial_file.empty()) {
    config.show_welcome_screen = false;
  }

  if (config_is_complete(config)) {
    config.show_welcome_screen = false;
    config.auto_debug = true;
  }

  std::error_code ec;
  if (!config.program.empty()) {
    config.program = std::filesystem::absolute(config.program, ec).string();
    if (!std::filesystem::exists(config.program)) {
      std::cerr << tgdb::i18n::tr_fmt("cli.program_not_found", {config.program}) << '\n';
      return 1;
    }
    if (!std::filesystem::is_regular_file(config.program)) {
      std::cerr << tgdb::i18n::tr_fmt("cli.program_not_file", {config.program}) << '\n';
      return 1;
    }
  }
  if (!config.core_path.empty()) {
    config.core_path = std::filesystem::absolute(config.core_path, ec).string();
    if (!std::filesystem::exists(config.core_path)) {
      std::cerr << tgdb::i18n::tr_fmt("cli.core_not_found", {config.core_path}) << '\n';
      return 1;
    }
  }
  if (config.core_analysis == tgdb::CoreAnalysisMode::kCoreAnalyzer &&
      !tgdb::core_analyzer_supported()) {
    std::cerr << tgdb::i18n::tr("cli.core_analyzer_unavailable") << '\n';
    return 1;
  }
  if (!config.initial_file.empty()) {
    config.initial_file =
        std::filesystem::absolute(config.initial_file, ec).string();
    if (!std::filesystem::exists(config.initial_file)) {
      std::cerr << tgdb::i18n::tr_fmt("cli.file_not_found", {config.initial_file}) << '\n';
      return 1;
    }
    if (!std::filesystem::is_regular_file(config.initial_file)) {
      std::cerr << tgdb::i18n::tr_fmt("cli.not_a_file", {config.initial_file}) << '\n';
      return 1;
    }
    if (config.workspace_root.empty()) {
      config.workspace_root =
          std::filesystem::path(config.initial_file).parent_path().string();
    }
  }
  if (!config.workspace_root.empty()) {
    config.workspace_root =
        std::filesystem::absolute(config.workspace_root, ec).string();
    if (!std::filesystem::is_directory(config.workspace_root)) {
      std::cerr << tgdb::i18n::tr_fmt("cli.cwd_not_directory", {config.workspace_root}) << '\n';
      return 1;
    }
  }

  tgdb::install_crash_handlers();

  try {
    tgdb::Application app(std::move(config));
    return app.run();
  } catch (const std::exception &e) {
    std::cerr << tgdb::i18n::tr_fmt("cli.fatal_error", {e.what()}) << '\n';
    tgdb::print_current_backtrace(e.what());
    return 1;
  } catch (...) {
    std::cerr << tgdb::i18n::tr("cli.fatal_unknown") << '\n';
    tgdb::print_current_backtrace("unknown");
    return 1;
  }
}