#include "util/external_viewer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>

#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "util/markdown_html.hpp"
#include "util/monitor_log.hpp"
#include "util/shell_utils.hpp"
#include "util/thread_name.hpp"
#include "i18n/tr.hpp"

namespace fs = std::filesystem;

namespace tuide {

namespace {

struct LaunchAttempt {
  bool ok = false;
  std::string message;
};

bool has_graphical_display() {
  const char* display = std::getenv("DISPLAY");
  if (display != nullptr && display[0] != '\0') {
    return true;
  }
  const char* wayland = std::getenv("WAYLAND_DISPLAY");
  return wayland != nullptr && wayland[0] != '\0';
}

std::string resolve_evince_command() {
  const char* override_path = std::getenv("EVINCE_PATH");
  if (override_path != nullptr && override_path[0] != '\0') {
    return override_path;
  }
  return "evince";
}

void redirect_stdio_to_devnull() {
  const int devnull = open("/dev/null", O_RDWR);
  if (devnull < 0) {
    return;
  }
  dup2(devnull, STDIN_FILENO);
  dup2(devnull, STDOUT_FILENO);
  dup2(devnull, STDERR_FILENO);
  if (devnull > STDERR_FILENO) {
    close(devnull);
  }
}

LaunchAttempt try_launch_evince(const std::string& evince_cmd, const std::string& pdf_path) {
  const pid_t pid = fork();
  if (pid < 0) {
    return {false, i18n::tr("pdf.evince_start_failed")};
  }
  if (pid == 0) {
    setsid();
    redirect_stdio_to_devnull();
    execlp(evince_cmd.c_str(), evince_cmd.c_str(), pdf_path.c_str(),
           static_cast<char*>(nullptr));
    _exit(127);
  }

  for (int attempt = 0; attempt < 20; ++attempt) {
    int status = 0;
    const pid_t waited = waitpid(pid, &status, WNOHANG);
    if (waited == pid) {
      if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        return {false, i18n::tr("pdf.evince_not_installed")};
      }
      return {false, i18n::tr("pdf.evince_exited")};
    }
    if (waited == 0) {
      usleep(25000);
      continue;
    }
    break;
  }
  return {true, {}};
}

LaunchAttempt try_launch_xdg_open(const std::string& path) {
  const pid_t pid = fork();
  if (pid < 0) {
    return {false, i18n::tr("markdown.browser_start_failed")};
  }
  if (pid == 0) {
    setsid();
    redirect_stdio_to_devnull();
    execlp("xdg-open", "xdg-open", path.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }

  for (int attempt = 0; attempt < 20; ++attempt) {
    int status = 0;
    const pid_t waited = waitpid(pid, &status, WNOHANG);
    if (waited == pid) {
      if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        return {false, i18n::tr("markdown.xdg_open_not_found")};
      }
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return {true, {}};
      }
      return {false, i18n::tr("markdown.browser_exited")};
    }
    if (waited == 0) {
      usleep(25000);
      continue;
    }
    break;
  }
  return {true, {}};
}

}  // namespace

bool is_pdf_path(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  std::string ext = fs::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext == ".pdf";
}

void launch_pdf_viewer_async(const std::string& absolute_path,
                             PdfViewerFinishedCallback on_finished) {
  std::thread([absolute_path, on_finished = std::move(on_finished)]() {
    set_current_thread_name("pdf-open");
    PdfLaunchResult result;
    result.path = absolute_path;

    std::error_code ec;
    if (!fs::is_regular_file(absolute_path, ec)) {
      result.message = i18n::tr("pdf.not_found");
      if (on_finished) {
        on_finished(result);
      }
      return;
    }

    if (!has_graphical_display()) {
      result.message = i18n::tr("pdf.no_display");
      TUIDE_MON("pdf", result.message + " path=" + absolute_path);
      if (on_finished) {
        on_finished(result);
      }
      return;
    }

    const std::string evince = resolve_evince_command();
    if (!command_exists(evince)) {
      result.message = i18n::tr("pdf.evince_not_in_path");
      TUIDE_MON("pdf", result.message + " path=" + absolute_path);
      if (on_finished) {
        on_finished(result);
      }
      return;
    }

    const LaunchAttempt launch = try_launch_evince(evince, absolute_path);
    result.ok = launch.ok;
    if (launch.ok) {
      result.message = i18n::tr_fmt(
          "pdf.opened", {fs::path(absolute_path).filename().string()});
      TUIDE_MON("pdf", "launched path=" + absolute_path);
    } else {
      result.message = launch.message;
      TUIDE_MON("pdf", "launch failed: " + launch.message + " path=" + absolute_path);
    }
    if (on_finished) {
      on_finished(result);
    }
  }).detach();
}

bool is_markdown_path(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  std::string ext = fs::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext == ".md" || ext == ".markdown";
}


namespace {

std::string user_home_dir() {
  if (const char* home = std::getenv("HOME"); home != nullptr && home[0] == '/') {
    return home;
  }
  if (const passwd* pw = getpwuid(getuid());
      pw != nullptr && pw->pw_dir != nullptr && pw->pw_dir[0] == '/') {
    return pw->pw_dir;
  }
  return {};
}

std::string safe_preview_stem(const std::string& md_path) {
  std::string stem = fs::path(md_path).stem().string();
  std::string safe;
  for (const char c : stem) {
    safe += (std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
  }
  return safe.empty() ? std::string{"preview"} : safe;
}

fs::path markdown_preview_dir(const std::string& md_path) {
  const std::string home = user_home_dir();
  if (!home.empty()) {
    return fs::path(home) / "tide-md-previews";
  }
  std::error_code ec;
  fs::path parent = fs::path(md_path).parent_path();
  if (parent.empty()) {
    parent = fs::current_path(ec);
  }
  parent = fs::absolute(parent, ec);
  return parent;
}

std::string build_preview_html_path(const std::string& md_path) {
  const fs::path cache_dir = markdown_preview_dir(md_path);
  std::error_code ec;
  fs::create_directories(cache_dir, ec);
  const std::size_t hash = std::hash<std::string>{}(md_path);
  const fs::path html_path =
      cache_dir / (safe_preview_stem(md_path) + "_" + std::to_string(hash) + ".html");
  return fs::absolute(html_path, ec).string();
}

}  // namespace

void launch_markdown_browser_async(const std::string& absolute_path,
                                   MarkdownViewerFinishedCallback on_finished) {
  std::thread([absolute_path, on_finished = std::move(on_finished)]() {
    set_current_thread_name("md-preview");
    MarkdownLaunchResult result;
    result.path = absolute_path;

    std::error_code ec;
    if (!fs::is_regular_file(absolute_path, ec)) {
      result.message = i18n::tr("markdown.not_found");
      if (on_finished) {
        on_finished(result);
      }
      return;
    }

    if (!has_graphical_display()) {
      result.message = i18n::tr("markdown.no_display");
      TUIDE_MON("markdown", result.message + " path=" + absolute_path);
      if (on_finished) {
        on_finished(result);
      }
      return;
    }

    if (!command_exists("xdg-open")) {
      result.message = i18n::tr("markdown.xdg_open_not_found");
      TUIDE_MON("markdown", result.message + " path=" + absolute_path);
      if (on_finished) {
        on_finished(result);
      }
      return;
    }

    std::ifstream f(absolute_path);
    if (!f) {
      result.message = i18n::tr("markdown.read_failed");
      if (on_finished) {
        on_finished(result);
      }
      return;
    }
    const std::string md_src((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());

    const std::string title = fs::path(absolute_path).filename().string();
    const std::string html = markdown_to_html(md_src, title);
    const std::string html_path = build_preview_html_path(absolute_path);
    {
      std::ofstream out(html_path, std::ios::trunc);
      if (!out) {
        result.message = i18n::tr("markdown.write_tmp_failed");
        if (on_finished) {
          on_finished(result);
        }
        return;
      }
      out << html;
      out.flush();
      if (!out) {
        result.message = i18n::tr("markdown.write_tmp_failed");
        if (on_finished) {
          on_finished(result);
        }
        return;
      }
    }
    if (!fs::is_regular_file(html_path, ec)) {
      result.message = i18n::tr("markdown.write_tmp_failed");
      if (on_finished) {
        on_finished(result);
      }
      return;
    }
    ::chmod(html_path.c_str(), 0644);

    const LaunchAttempt launch = try_launch_xdg_open(html_path);
    result.ok = launch.ok;
    if (launch.ok) {
      result.message = i18n::tr_fmt("markdown.opened", {title});
      TUIDE_MON("markdown", "launched path=" + html_path);
    } else {
      result.message = launch.message;
      TUIDE_MON("markdown", "launch failed: " + launch.message + " path=" + html_path);
    }
    if (on_finished) {
      on_finished(result);
    }
  }).detach();
}

}  // namespace tuide
