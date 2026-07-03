#include "indexer/symbol_workspace_indexer.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>

#include "symbols/regex_symbol_provider.hpp"
#include "indexer/index_rules.hpp"
#include "util/monitor_log.hpp"
#include "util/thread_name.hpp"

namespace fs = std::filesystem;

namespace tgdb {

namespace {

std::vector<IndexedSymbol> symbols_for_relative_file(
    const std::shared_ptr<ISymbolProvider>& provider, const std::string& workspace_root,
    const std::string& relative_file) {
  std::vector<IndexedSymbol> out;
  if (!provider || relative_file.empty()) {
    return out;
  }
  const auto absolute = (fs::path(workspace_root) / relative_file).string();
  for (const auto& sym : provider->symbols_for_file(absolute)) {
    IndexedSymbol entry;
    entry.display_name = sym.name;
    entry.kind = sym.kind;
    entry.line = sym.line;
    entry.file = sym.file.empty() ? relative_file : sym.file;
    out.push_back(std::move(entry));
  }
  return out;
}

}  // namespace

SymbolWorkspaceIndexer::SymbolWorkspaceIndexer() {
  snapshot_ = std::make_shared<SymbolIndexSnapshot>();
}

SymbolWorkspaceIndexer::~SymbolWorkspaceIndexer() {
  stop();
}

void SymbolWorkspaceIndexer::start_scan(const std::string& workspace_root,
                                        const std::shared_ptr<ISymbolProvider>& provider,
                                        WorkspaceIndexer* file_indexer) {
  stop();
  provider_ = provider;
  stop_requested_ = false;
  running_ = true;
  {
    auto snap = std::make_shared<SymbolIndexSnapshot>();
    snap->workspace_root = workspace_root;
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = snap;
  }
  worker_ = std::thread([this, workspace_root, provider, file_indexer] {
    set_current_thread_name("idx-syms");
    worker_main(workspace_root, provider, file_indexer);
  });
}

void SymbolWorkspaceIndexer::reindex_file(const std::string& workspace_root,
                                          const std::string& relative_file,
                                          const std::string& absolute_path) {
  if (!is_indexed_source_path(relative_file)) {
    return;
  }
  std::shared_ptr<ISymbolProvider> provider;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    provider = provider_;
  }
  if (!provider || relative_file.empty()) {
    return;
  }

  auto updated = std::make_shared<SymbolIndexSnapshot>();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_ || snapshot_->workspace_root != workspace_root) {
      return;
    }
    updated->workspace_root = workspace_root;
    updated->symbols.reserve(snapshot_->symbols.size());
    for (const auto& sym : snapshot_->symbols) {
      if (sym.file != relative_file) {
        updated->symbols.push_back(sym);
      }
    }
  }

  for (const auto& sym : provider->symbols_for_file(absolute_path)) {
    IndexedSymbol entry;
    entry.display_name = sym.name;
    entry.kind = sym.kind;
    entry.line = sym.line;
    entry.file = sym.file.empty() ? relative_file : sym.file;
    updated->symbols.push_back(std::move(entry));
  }

  std::sort(updated->symbols.begin(), updated->symbols.end(),
            [](const IndexedSymbol& a, const IndexedSymbol& b) {
              if (a.file != b.file) {
                return a.file < b.file;
              }
              return a.display_name < b.display_name;
            });

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = updated;
}

void SymbolWorkspaceIndexer::remove_file(const std::string& workspace_root,
                                         const std::string& relative_file) {
  if (relative_file.empty()) {
    return;
  }

  auto updated = std::make_shared<SymbolIndexSnapshot>();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_ || snapshot_->workspace_root != workspace_root) {
      return;
    }
    updated->workspace_root = workspace_root;
    for (const auto& sym : snapshot_->symbols) {
      if (sym.file != relative_file) {
        updated->symbols.push_back(sym);
      }
    }
  }

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = updated;
}

void SymbolWorkspaceIndexer::stop() {
  stop_requested_ = true;
  if (worker_.joinable()) {
    worker_.join();
  }
  running_ = false;
}

std::shared_ptr<const SymbolIndexSnapshot> SymbolWorkspaceIndexer::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

bool SymbolWorkspaceIndexer::scanning() const {
  return running_.load();
}

void SymbolWorkspaceIndexer::worker_main(std::string workspace_root,
                                         std::shared_ptr<ISymbolProvider> provider,
                                         WorkspaceIndexer* file_indexer) {
  TGDB_MON_SCOPE("idx", "symbol_workspace_indexer.scan");
  RegexSymbolProvider regex_provider;
  const bool use_regex_bulk = provider != nullptr && !provider->indexes_workspace_bulk();

  std::vector<std::string> files;
  if (file_indexer != nullptr) {
    for (;;) {
      if (stop_requested_) {
        running_ = false;
        return;
      }
      const auto file_snap = file_indexer->snapshot();
      if (file_snap && file_snap->workspace_root == workspace_root &&
          (!file_snap->files.empty() || !file_indexer->scanning())) {
        files = file_snap->files;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  auto snap = std::make_shared<SymbolIndexSnapshot>();
  snap->workspace_root = workspace_root;
  for (const auto& rel : files) {
    if (stop_requested_) {
      running_ = false;
      return;
    }
    if (!is_indexed_source_path(rel)) {
      continue;
    }
    std::vector<IndexedSymbol> entries;
    if (use_regex_bulk) {
      const auto absolute = (fs::path(workspace_root) / rel).string();
      for (const auto& sym : regex_provider.symbols_for_file(absolute)) {
        IndexedSymbol entry;
        entry.display_name = sym.name;
        entry.kind = sym.kind;
        entry.line = sym.line;
        entry.file = sym.file.empty() ? rel : sym.file;
        entries.push_back(std::move(entry));
      }
    } else {
      entries = symbols_for_relative_file(provider, workspace_root, rel);
    }
    snap->symbols.insert(snap->symbols.end(), entries.begin(), entries.end());
  }

  std::sort(snap->symbols.begin(), snap->symbols.end(),
            [](const IndexedSymbol& a, const IndexedSymbol& b) {
              if (a.file != b.file) {
                return a.file < b.file;
              }
              return a.display_name < b.display_name;
            });

  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = snap;
  }
  running_ = false;
}

}  // namespace tgdb
