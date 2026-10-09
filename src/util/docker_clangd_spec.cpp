#include "util/docker_clangd_spec.hpp"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace tuide {

namespace {

constexpr const char* kClangdQueryDriver =
    "/usr/bin/gcc*,/usr/bin/g++,/usr/bin/c++*,/usr/bin/clang*,"
    "/usr/local/bin/gcc*,/usr/local/bin/g++,/usr/local/bin/c++*,/usr/local/bin/clang*";

bool is_system_prefix(const std::string& root) {
  return root == "/" || root == "/usr" || root == "/usr/local" || root == "/opt" ||
         root == "/lib" || root == "/bin" || root == "/lib64";
}

std::string lexically_normal_generic(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  return fs::path(path).lexically_normal().generic_string();
}

}  // namespace

std::optional<std::string> docker_clangd_install_root(const std::string& binary_path,
                                                     const std::string& resource_dir) {
  const fs::path binary(binary_path);
  if (binary.filename() != "clangd" || binary.parent_path().filename() != "bin") {
    return std::nullopt;
  }
  const std::string root = lexically_normal_generic(binary.parent_path().parent_path().string());
  if (root.empty() || is_system_prefix(root)) {
    return std::nullopt;
  }
  const std::string resource = lexically_normal_generic(resource_dir);
  const std::string resource_prefix = root + "/lib/clang";
  if (resource != resource_prefix && resource.rfind(resource_prefix + "/", 0) != 0) {
    return std::nullopt;
  }
  return root;
}

std::string docker_clangd_remote_resource_dir(const std::string& host_root,
                                              const std::string& host_resource_dir) {
  const std::string fallback = std::string(kDockerClangdRemoteRoot) + "/resource";
  if (host_root.empty() || host_resource_dir.empty()) {
    return fallback;
  }
  std::error_code ec;
  const fs::path relative = fs::relative(fs::path(host_resource_dir), fs::path(host_root), ec);
  const std::string relative_text = relative.generic_string();
  if (ec || relative_text.empty() || relative_text == "." ||
      relative_text.rfind("..", 0) == 0) {
    return fallback;
  }
  return std::string(kDockerClangdRemoteRoot) + "/" + relative_text;
}

std::string clangd_path_mappings_argument(
    const std::vector<std::pair<std::string, std::string>>& client_to_server) {
  std::vector<std::pair<std::string, std::string>> pairs;
  pairs.reserve(client_to_server.size());
  for (const auto& entry : client_to_server) {
    if (entry.first.empty() || entry.second.empty()) {
      continue;
    }
    if (entry.first.find_first_of(",=") != std::string::npos ||
        entry.second.find_first_of(",=") != std::string::npos) {
      continue;
    }
    pairs.push_back(entry);
  }
  std::sort(pairs.begin(), pairs.end(),
            [](const std::pair<std::string, std::string>& a,
               const std::pair<std::string, std::string>& b) {
              const std::size_t a_len = std::max(a.first.size(), a.second.size());
              const std::size_t b_len = std::max(b.first.size(), b.second.size());
              if (a_len != b_len) {
                return a_len > b_len;
              }
              return a.first.size() > b.first.size();
            });

  std::string out;
  for (const auto& entry : pairs) {
    if (!out.empty()) {
      out.push_back(',');
    }
    out += entry.first;
    out.push_back('=');
    out += entry.second;
  }
  return out;
}

LanguageServerSpec make_docker_clangd_spec(const std::string& workspace_root,
                                           const DockerClangdLaunch& launch,
                                           const bool use_gcc_query_driver,
                                           const bool background_index) {
  LanguageServerSpec spec;
  spec.id = kLspServerClangd;
  spec.command = launch.docker_binary;
  spec.workspace_root = workspace_root;
  spec.language_ids = {"c", "cpp"};
  spec.args.push_back("exec");
  spec.args.push_back("-i");
  spec.args.push_back(launch.container);
  spec.args.push_back(launch.remote_binary);
  if (!launch.remote_resource_dir.empty()) {
    spec.args.push_back("--resource-dir=" + launch.remote_resource_dir);
  }
  if (!launch.remote_compile_commands_dir.empty()) {
    spec.args.push_back("--compile-commands-dir=" + launch.remote_compile_commands_dir);
  }
  if (!launch.path_mappings.empty()) {
    spec.args.push_back("--path-mappings=" + launch.path_mappings);
  }
  if (use_gcc_query_driver) {
    spec.args.emplace_back(std::string("--query-driver=") + kClangdQueryDriver);
  }
  spec.args.emplace_back("-j=2");
  if (background_index) {
    spec.args.emplace_back("--background-index=true");
    spec.args.emplace_back("--background-index-priority=idle");
  } else {
    spec.args.emplace_back("--background-index=false");
  }
  return spec;
}

}  // namespace tuide
