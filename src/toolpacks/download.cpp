#include "toolpacks/download.hpp"

#include <sys/wait.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace fs = std::filesystem;

namespace tuide::toolpacks {
namespace {

int run_command(const std::string& command, std::string* output = nullptr) {
  std::array<char, 256> buffer{};
  std::string collected;
  FILE* pipe = ::popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return 127;
  }
  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    collected += buffer.data();
  }
  const int status = ::pclose(pipe);
  if (output != nullptr) {
    *output = collected;
  }
  if (status == -1) {
    return 127;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return 127;
}

std::string shell_quote(const std::string& value) {
  std::string out = "'";
  for (char ch : value) {
    if (ch == '\'') {
      out += "'\\''";
    } else {
      out += ch;
    }
  }
  out += "'";
  return out;
}

bool command_exists(const char* name) {
  return run_command(std::string("command -v ") + name + " >/dev/null 2>&1") == 0;
}

// Encode '+' in URL paths/query so GitHub release asset names with '+' resolve.
std::string encode_download_url(const std::string& url) {
  std::string out;
  out.reserve(url.size() + 8);
  for (char ch : url) {
    if (ch == '+') {
      out += "%2B";
    } else {
      out.push_back(ch);
    }
  }
  return out;
}

std::optional<int> parse_percent_token(std::string_view text) {
  const auto pct = text.rfind('%');
  if (pct == std::string_view::npos || pct == 0) {
    return std::nullopt;
  }
  std::size_t end = pct;
  while (end > 0 && (std::isdigit(static_cast<unsigned char>(text[end - 1])) != 0 ||
                     text[end - 1] == '.')) {
    --end;
  }
  if (end == pct) {
    return std::nullopt;
  }
  // Skip leading non-digit junk immediately before the number (e.g. spaces).
  while (end < pct && std::isdigit(static_cast<unsigned char>(text[end])) == 0 &&
         text[end] != '.') {
    ++end;
  }
  if (end >= pct) {
    return std::nullopt;
  }
  try {
    const int value = static_cast<int>(std::stof(std::string(text.substr(end, pct - end))));
    if (value < 0 || value > 100) {
      return std::nullopt;
    }
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

int run_download_command(const std::string& command, const ProgressFn& on_progress,
                         const fs::path& partial_path, std::uint64_t expected_size) {
  FILE* pipe = ::popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return 127;
  }

  std::atomic<bool> done{false};
  std::thread size_poller;
  if (on_progress && expected_size > 0) {
    size_poller = std::thread([&] {
      int last_reported = -1;
      while (!done.load(std::memory_order_acquire)) {
        std::error_code ec;
        const auto sz = fs::file_size(partial_path, ec);
        if (!ec && expected_size > 0) {
          const int pct = static_cast<int>(
              std::min<std::uint64_t>(100, (sz * 100) / expected_size));
          if (pct != last_reported) {
            last_reported = pct;
            report_progress(on_progress, pct);
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
      }
    });
  }

  // curl/wget --progress-bar rewrite the same line with '\r'; flush on CR/LF/%.
  std::string acc;
  int last_reported = -1;
  int ch = 0;
  auto flush_token = [&]() {
    if (!on_progress || acc.empty() || expected_size > 0) {
      // Prefer size-based progress when available (avoids pipe buffering stalls).
      return;
    }
    const auto pct = parse_percent_token(acc);
    if (!pct.has_value() || *pct == last_reported) {
      return;
    }
    last_reported = *pct;
    report_progress(on_progress, *pct);
  };
  while ((ch = std::fgetc(pipe)) != EOF) {
    if (ch == '\r' || ch == '\n') {
      flush_token();
      acc.clear();
      continue;
    }
    acc.push_back(static_cast<char>(ch));
    if (ch == '%') {
      flush_token();
    }
    // Bound memory if a tool dumps a huge binary blob on stdout.
    if (acc.size() > 4096) {
      acc.erase(0, acc.size() - 256);
    }
  }
  flush_token();
  const int status = ::pclose(pipe);
  done.store(true, std::memory_order_release);
  if (size_poller.joinable()) {
    size_poller.join();
  }
  if (status == -1) {
    return 127;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return 127;
}

}  // namespace

std::string toolpack_host_deps_error() {
  const bool have_curl = command_exists("curl");
  const bool have_wget = command_exists("wget");
  if (!have_curl && !have_wget) {
    return "hace falta curl o wget para descargar toolpacks";
  }
  if (!command_exists("tar")) {
    return "hace falta tar para extraer toolpacks";
  }
  if (!command_exists("zstd")) {
    return "hace falta zstd para extraer toolpacks (.tar.zst del catalogo)";
  }
  if (!command_exists("sha256sum")) {
    return "hace falta sha256sum para verificar toolpacks";
  }
  return {};
}

std::string download_url(const std::string& url, const std::string& dest_path,
                         ProgressFn on_progress, std::uint64_t expected_size) {
  std::error_code ec;
  fs::create_directories(fs::path(dest_path).parent_path(), ec);
  const fs::path tmp = fs::path(dest_path).string() + ".partial";
  fs::remove(tmp, ec);

  report_progress(on_progress, 0);

  if (url.rfind("file://", 0) == 0) {
    const std::string local = url.substr(7);
    fs::copy_file(local, tmp, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      return "no se pudo copiar " + local + ": " + ec.message();
    }
    fs::rename(tmp, dest_path, ec);
    if (ec) {
      return "no se pudo mover descarga a destino: " + ec.message();
    }
    report_progress(on_progress, 100);
    return {};
  }

  const std::string fetch_url = encode_download_url(url);

  std::string cmd;
  if (command_exists("curl")) {
    // Silent transfer when we poll file size; otherwise --progress-bar on stderr→stdout.
    if (expected_size > 0) {
      cmd = "curl -fL --retry 3 --connect-timeout 20 --retry-delay 1 -o " +
            shell_quote(tmp.string()) + " " + shell_quote(fetch_url) + " 2>/dev/null";
    } else {
      cmd = "curl -fL --retry 3 --connect-timeout 20 --progress-bar -o " +
            shell_quote(tmp.string()) + " " + shell_quote(fetch_url) + " 2>&1";
    }
  } else if (command_exists("wget")) {
    if (expected_size > 0) {
      cmd = "wget -q -O " + shell_quote(tmp.string()) + " " + shell_quote(fetch_url) +
            " 2>/dev/null";
    } else {
      cmd = "wget --progress=bar:force -O " + shell_quote(tmp.string()) + " " +
            shell_quote(fetch_url) + " 2>&1";
    }
  } else {
    return "hace falta curl o wget para descargar toolpacks";
  }

  const int rc = run_download_command(cmd, on_progress, tmp, expected_size);
  if (rc != 0 || !fs::is_regular_file(tmp, ec)) {
    fs::remove(tmp, ec);
    return "fallo la descarga (" + std::to_string(rc) + "): " + url;
  }
  fs::rename(tmp, dest_path, ec);
  if (ec) {
    return "no se pudo mover descarga a destino: " + ec.message();
  }
  report_progress(on_progress, 100);
  return {};
}

std::string file_sha256(const std::string& path) {
  std::string output;
  const int rc =
      run_command("sha256sum " + shell_quote(path) + " 2>/dev/null", &output);
  if (rc != 0 || output.size() < 64) {
    return {};
  }
  return output.substr(0, 64);
}

std::string extract_archive(const std::string& archive_path, const std::string& dest_dir) {
  std::error_code ec;
  fs::create_directories(dest_dir, ec);
  if (ec) {
    return "no se pudo crear " + dest_dir + ": " + ec.message();
  }

  std::string cmd;
  if (archive_path.size() >= 8 &&
      archive_path.compare(archive_path.size() - 8, 8, ".tar.zst") == 0) {
    if (!command_exists("zstd") || !command_exists("tar")) {
      return "hace falta zstd y tar para extraer toolpacks";
    }
    cmd = "zstd -dc " + shell_quote(archive_path) + " | tar -x -C " + shell_quote(dest_dir);
  } else if (archive_path.size() >= 7 &&
             archive_path.compare(archive_path.size() - 7, 7, ".tar.gz") == 0) {
    cmd = "tar -xzf " + shell_quote(archive_path) + " -C " + shell_quote(dest_dir);
  } else if (archive_path.size() >= 4 &&
             archive_path.compare(archive_path.size() - 4, 4, ".tar") == 0) {
    cmd = "tar -xf " + shell_quote(archive_path) + " -C " + shell_quote(dest_dir);
  } else {
    return "formato de archivo no soportado: " + archive_path;
  }

  const int rc = run_command(cmd + " 2>&1");
  if (rc != 0) {
    return "fallo al extraer archivo (codigo " + std::to_string(rc) + ")";
  }
  return {};
}

}  // namespace tuide::toolpacks
