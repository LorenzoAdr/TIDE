#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ai/get_code_of.hpp"
#include "ai/level2_session.hpp"
#include "ai/search_replace.hpp"
#include "ai/tool_registry.hpp"

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using tuide::AiToolResult;
using tuide::GetCodeOfRequest;
using tuide::Level2BootstrapOpts;
using tuide::Level2Session;
using tuide::ToolRegistry;
using tuide::get_code_of;
using tuide::parse_get_code_of_arg;

namespace {

std::string trim(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\r' || s.back() == '\t')) {
    s.pop_back();
  }
  std::size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
    ++i;
  }
  return s.substr(i);
}

std::string read_file(const fs::path& p) {
  std::ifstream in(p);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string run_cmd(const std::string& cmd) {
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    return {};
  }
  std::ostringstream out;
  char buf[4096];
  while (fgets(buf, sizeof(buf), pipe) != nullptr) {
    out << buf;
  }
  pclose(pipe);
  return out.str();
}

std::string shell_quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  out.push_back('\'');
  return out;
}

void register_read_tools(ToolRegistry* reg, const std::string& root) {
  reg->register_tool("get_code_of", "body", [root](const std::string& arg) {
    const GetCodeOfRequest req = parse_get_code_of_arg(arg, root);
    const auto got = get_code_of(req);
    if (!got.ok) {
      return AiToolResult{false, got.error.empty() ? "get_code_of failed" : got.error};
    }
    std::ostringstream out;
    out << got.path;
    if (got.start_line > 0) {
      out << ':' << got.start_line;
      if (got.end_line > got.start_line) {
        out << '-' << got.end_line;
      }
    }
    if (!got.name.empty()) {
      out << " — " << got.name;
    }
    out << '\n' << got.text;
    return AiToolResult{true, out.str()};
  });

  reg->register_tool("file_outline", "outline", [root](const std::string& arg) {
    const std::string trimmed = trim(arg);
    fs::path abs = trimmed;
    if (!abs.is_absolute()) {
      abs = fs::path(root) / trimmed;
    }
    abs = abs.lexically_normal();
    if (!fs::exists(abs)) {
      return AiToolResult{false, "no existe: " + trimmed};
    }
    const std::string cmd =
        "rg -n --no-heading -e "
        "'^[a-zA-Z_].*\\(.*\\)\\s*\\{?\\s*$|^\\s*(class|struct|namespace|enum)\\s+' " +
        shell_quote(abs.string()) + " | head -n 80";
    std::string body = run_cmd(cmd);
    if (body.empty()) {
      body = "(sin outline rg)\n";
    }
    return AiToolResult{true, "outline: " + trimmed + "\n" + body};
  });

  reg->register_tool("search", "rg", [root](const std::string& arg) {
    const std::string cmd = "rg -n --no-heading -S -g '!build/**' -g '!.git/**' -g '!third_party/**' " +
                            shell_quote(arg) + " " + shell_quote(root) + " | head -n 40";
    std::string body = run_cmd(cmd);
    if (body.empty()) {
      body = "(sin hits)\n";
    }
    return AiToolResult{true, "q=" + arg + "\n" + body};
  });

  reg->register_tool("list_tools", "list", [](const std::string&) {
    return AiToolResult{true, "get_code_of\nfile_outline\nsearch\nlist_tools\n"};
  });
}

void usage() {
  std::cerr << "Usage: l2_harness_cli bootstrap|tool|turn|done|edit|compile|status …\n"
            << "  done [summary] [--edit|--clarify]\n"
            << "  edit <hunks.json>\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 2;
  }
  const std::string cmd = argv[1];
  const std::string root = [] {
    const char* env = std::getenv("TUIDE_ROOT");
    if (env && *env) {
      return std::string(env);
    }
    return fs::current_path().string();
  }();

  ToolRegistry tools;
  register_read_tools(&tools, root);
  Level2Session session(tuide::Level2SessionDeps{&tools, {}, {}});

  if (cmd == "status") {
    std::cout << session.status_text(root);
    return 0;
  }
  if (cmd == "bootstrap") {
    Level2BootstrapOpts opts;
    opts.workspace_root = root;
    opts.query = argc >= 3 ? argv[2] : std::string{};
    if (opts.query.empty()) {
      const std::string map = read_file(fs::path(root) / ".tuide" / "ai" / "map_last.md");
      const auto qpos = map.find("query:");
      if (qpos != std::string::npos) {
        const auto line_end = map.find('\n', qpos);
        opts.query = trim(map.substr(qpos + 6, line_end == std::string::npos
                                                   ? std::string::npos
                                                   : line_end - (qpos + 6)));
      }
    }
    if (opts.query.empty()) {
      opts.query = "(harness cli bootstrap)";
    }
    opts.instruction =
        "Explore then done next=edit; emit Search/Replace hunks; runtime compiles.";
    std::string err;
    if (!session.bootstrap(opts, &err)) {
      std::cerr << "bootstrap failed: " << err << '\n';
      return 1;
    }
    std::cout << session.status_text(root);
    return 0;
  }
  if (cmd == "tool") {
    if (argc < 3) {
      usage();
      return 2;
    }
    const std::string name = argv[2];
    std::ostringstream arg;
    for (int i = 3; i < argc; ++i) {
      if (i > 3) {
        arg << ' ';
      }
      arg << argv[i];
    }
    const auto tr = session.apply_tool(root, name, arg.str());
    if (!tr.ok) {
      std::cerr << "tool failed: " << tr.error << '\n';
      return 1;
    }
    std::cout << "ok turn=" << tr.turn << " phase=" << tr.phase << " — " << tr.summary << '\n';
    return 0;
  }
  if (cmd == "turn") {
    const auto tr = session.process_request_file(root);
    std::cout << "action=" << tr.action << " turn=" << tr.turn << " phase=" << tr.phase
              << " ok=" << (tr.ok ? 1 : 0) << '\n';
    if (!tr.ok && tr.action != "compile") {
      std::cerr << "turn failed: " << tr.error << '\n';
      return 1;
    }
    return tr.ok ? 0 : 1;
  }
  if (cmd == "done") {
    std::string summary;
    std::string next;
    for (int i = 2; i < argc; ++i) {
      const std::string a = argv[i];
      if (a == "--edit") {
        next = "edit";
        continue;
      }
      if (a == "--clarify") {
        next = "clarify";
        continue;
      }
      if (!summary.empty()) {
        summary.push_back(' ');
      }
      summary += a;
    }
    const auto tr = session.mark_done(root, summary, next);
    if (!tr.ok) {
      std::cerr << "done failed: " << tr.error << '\n';
      return 1;
    }
    std::cout << "ok done turn=" << tr.turn << " phase=" << tr.phase;
    if (tr.phase == "clarify") {
      std::cout << " — need user clarification";
    }
    std::cout << '\n';
    return 0;
  }
  if (cmd == "edit") {
    if (argc < 3) {
      usage();
      return 2;
    }
    const std::string raw = read_file(argv[2]);
    std::string err;
    std::vector<tuide::SearchReplaceHunk> hunks;
    try {
      const auto j = nlohmann::json::parse(raw);
      hunks = tuide::parse_search_replace_json(j, &err);
    } catch (const std::exception& ex) {
      std::cerr << "json: " << ex.what() << '\n';
      return 1;
    }
    if (hunks.empty()) {
      std::cerr << "edit failed: " << err << '\n';
      return 1;
    }
    const auto tr = session.apply_edit(root, hunks);
    std::cout << "edit action=" << tr.action << " phase=" << tr.phase << " ok=" << (tr.ok ? 1 : 0)
              << " — " << tr.summary << '\n';
    return tr.ok ? 0 : 1;
  }
  if (cmd == "compile") {
    const auto tr = session.run_compile(root);
    std::cout << "compile action=" << tr.action << " phase=" << tr.phase
              << " ok=" << (tr.ok ? 1 : 0) << '\n';
    return tr.ok ? 0 : 1;
  }
  usage();
  return 2;
}
