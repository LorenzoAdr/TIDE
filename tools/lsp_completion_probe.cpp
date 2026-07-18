#include "lsp/lsp_client.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

int col_after_token(const std::string& text, int line, const char* token) {
  int current = 0;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == '\n') {
      if (current == line) {
        const std::string_view row(text.data() + start, i - start);
        const auto pos = row.find(token);
        if (pos == std::string_view::npos) {
          return 0;
        }
        return static_cast<int>(pos + std::string_view(token).size());
      }
      ++current;
      start = i + 1;
    }
  }
  return 0;
}

void probe_line(tuide::LspClient& client, const std::string& path, const std::string& text,
                int line, int col, const char* label) {
  const auto items = client.completions_at(path, text, line, col);
  std::cout << label << " line=" << line << " col=" << col << " items=" << items.size() << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  const std::string workspace = argc > 1 ? argv[1] : "/home/lorenzo/workspace/tgdb";
  const std::string file = argc > 2 ? argv[2] : workspace + "/src/app/application.cpp";
  const std::string compile_dir = argc > 3 ? argv[3] : workspace + "/.tuide";

  const std::string text = read_file(file);
  if (text.empty()) {
    std::cerr << "failed to read " << file << '\n';
    return 1;
  }

  tuide::LspClient client;
  if (!client.start(workspace, compile_dir, true, false)) {
    std::cerr << "LspClient::start failed\n";
    return 1;
  }

  client.did_open(file, text);

  const int early_col = col_after_token(text, 100, "void");
  const int late_col = col_after_token(text, 2470, "screen");
  probe_line(client, file, text, 100, early_col > 0 ? early_col : 4, "early");
  probe_line(client, file, text, 2470, late_col > 0 ? late_col : 4, "late");

  client.stop();
  return 0;
}
