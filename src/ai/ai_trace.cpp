#include "ai/ai_trace.hpp"

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace fs = std::filesystem;

namespace tuide {
namespace {

std::mutex g_mu;
bool g_enabled = true;
std::string g_workspace;
std::string g_path_override;
std::string g_resolved_path;

std::string resolve_path_unlocked() {
  if (!g_path_override.empty()) {
    return g_path_override;
  }
  if (g_workspace.empty()) {
    return {};
  }
  return (fs::path(g_workspace) / ".tuide" / "ai" / "trace.ndjson").string();
}

}  // namespace

const char* ai_trace_channel_name(AiTraceChannel ch) {
  switch (ch) {
    case AiTraceChannel::System:
      return "system";
    case AiTraceChannel::L0:
      return "L0";
    case AiTraceChannel::L1:
      return "L1";
    case AiTraceChannel::L2:
      return "L2";
    case AiTraceChannel::Embed:
      return "embed";
    case AiTraceChannel::Tool:
      return "tool";
  }
  return "system";
}

std::string ai_trace_escape(std::string_view s, std::size_t max_len) {
  std::string out;
  out.reserve(std::min(s.size(), max_len) + 16);
  for (std::size_t i = 0; i < s.size() && out.size() < max_len; ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == '"' || c == '\\') {
      out.push_back('\\');
      out.push_back(static_cast<char>(c));
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else if (c < 0x20) {
      continue;
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
  if (s.size() > max_len) {
    out += "…";
  }
  return out;
}

void ai_trace_configure(bool enabled, std::string workspace_root, std::string path_override) {
  std::lock_guard lock(g_mu);
  g_enabled = enabled;
  g_workspace = std::move(workspace_root);
  g_path_override = std::move(path_override);
  g_resolved_path = resolve_path_unlocked();
  if (g_enabled && !g_resolved_path.empty()) {
    std::error_code ec;
    fs::create_directories(fs::path(g_resolved_path).parent_path(), ec);
  }
}

bool ai_trace_enabled() {
  std::lock_guard lock(g_mu);
  return g_enabled && !resolve_path_unlocked().empty();
}

std::string ai_trace_path() {
  std::lock_guard lock(g_mu);
  return resolve_path_unlocked();
}

std::string ai_trace_status_text() {
  std::lock_guard lock(g_mu);
  const std::string path = resolve_path_unlocked();
  std::ostringstream oss;
  oss << "ai.trace enabled=" << (g_enabled ? "true" : "false");
  oss << " path=" << (path.empty() ? std::string("(no workspace)") : path);
  return oss.str();
}

void ai_trace(AiTraceChannel channel, std::string_view event, std::string_view data_json) {
  std::string path;
  {
    std::lock_guard lock(g_mu);
    if (!g_enabled) {
      return;
    }
    path = resolve_path_unlocked();
    if (path.empty()) {
      return;
    }
    g_resolved_path = path;
  }

  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);

  std::ofstream f(path, std::ios::app);
  if (!f) {
    return;
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
  f << "{\"ts\":" << ms << ",\"level\":\"" << ai_trace_channel_name(channel) << "\",\"event\":\""
    << ai_trace_escape(event, 80) << "\",\"data\":"
    << (data_json.empty() ? "{}" : data_json) << "}\n";
}

bool ai_trace_clear(std::string* error) {
  std::string path;
  {
    std::lock_guard lock(g_mu);
    path = resolve_path_unlocked();
  }
  if (path.empty()) {
    if (error) {
      *error = "trace path vacío (abre un workspace)";
    }
    return false;
  }
  std::error_code ec;
  fs::remove(path, ec);
  if (ec && fs::exists(path)) {
    if (error) {
      *error = ec.message();
    }
    return false;
  }
  return true;
}

std::string ai_trace_tail(std::size_t max_lines) {
  std::string path;
  {
    std::lock_guard lock(g_mu);
    path = resolve_path_unlocked();
  }
  if (path.empty()) {
    return "(sin path de trace)";
  }
  std::ifstream ifs(path);
  if (!ifs) {
    return "(trace vacío o inexistente: " + path + ")";
  }
  std::deque<std::string> lines;
  std::string line;
  while (std::getline(ifs, line)) {
    lines.push_back(std::move(line));
    if (lines.size() > max_lines) {
      lines.pop_front();
    }
  }
  std::ostringstream oss;
  oss << "=== ai trace tail (" << lines.size() << " líneas) @ " << path << " ===\n";
  for (const auto& l : lines) {
    oss << l << '\n';
  }
  return oss.str();
}

}  // namespace tuide
