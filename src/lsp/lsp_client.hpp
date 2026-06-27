#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "lsp/lsp_transport.hpp"
#include "symbols/symbol_provider.hpp"

namespace tgdb {

class LspClient {
 public:
  LspClient();
  ~LspClient();

  bool start(const std::string& workspace_root);
  void stop();
  bool ready() const { return ready_.load(); }

  void did_open(const std::string& absolute_path, const std::string& text);
  void did_change(const std::string& absolute_path, const std::string& text);
  void did_close(const std::string& absolute_path);

  std::vector<SymbolInfo> document_symbols(const std::string& absolute_path);
  std::vector<SymbolInfo> workspace_symbols(const std::string& workspace_root,
                                            const std::string& query);

 private:
  struct DocumentState {
    std::string uri;
    std::string text;
    int version = 0;
  };

  bool spawn_clangd(const std::string& workspace_root);
  bool initialize(const std::string& workspace_root);
  std::string find_compile_commands_dir(const std::string& workspace_root) const;
  void invalidate_cache(const std::string& absolute_path);

  static SymbolKind map_lsp_kind(int kind);
  static std::string kind_prefix(SymbolKind kind);
  static void flatten_symbols(const nlohmann::json& nodes, int depth,
                              const std::string& relative_file,
                              std::vector<SymbolInfo>* out);

  LspTransport transport_;
  std::atomic<bool> ready_{false};
  pid_t child_pid_ = -1;
  int stdin_write_fd_ = -1;
  int stdout_read_fd_ = -1;
  std::string workspace_root_;

  std::mutex mutex_;
  std::unordered_map<std::string, DocumentState> documents_;
  std::unordered_map<std::string, std::vector<SymbolInfo>> symbol_cache_;
  int next_request_id_ = 1;
};

}  // namespace tgdb
