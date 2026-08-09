#include "toolpacks/download.hpp"

#include <sys/wait.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>

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

}  // namespace

std::string download_url(const std::string& url, const std::string& dest_path) {
  std::error_code ec;
  fs::create_directories(fs::path(dest_path).parent_path(), ec);
  const fs::path tmp = fs::path(dest_path).string() + ".partial";
  fs::remove(tmp, ec);

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
    return {};
  }

  std::string cmd;
  if (command_exists("curl")) {
    cmd = "curl -fsSL --retry 3 -o " + shell_quote(tmp.string()) + " " + shell_quote(url);
  } else if (command_exists("wget")) {
    cmd = "wget -q -O " + shell_quote(tmp.string()) + " " + shell_quote(url);
  } else {
    return "hace falta curl o wget para descargar toolpacks";
  }

  const int rc = run_command(cmd + " 2>&1");
  if (rc != 0 || !fs::is_regular_file(tmp, ec)) {
    fs::remove(tmp, ec);
    return "fallo la descarga (" + std::to_string(rc) + "): " + url;
  }
  fs::rename(tmp, dest_path, ec);
  if (ec) {
    return "no se pudo mover descarga a destino: " + ec.message();
  }
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
