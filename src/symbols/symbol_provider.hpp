#pragma once

#include <string>
#include <vector>

namespace tgdb {

enum class SymbolKind { kNamespace, kClass, kStruct, kFunction, kMethod, kVariable };

struct SymbolInfo {
  std::string name;
  SymbolKind kind = SymbolKind::kFunction;
  int line = 0;
  int depth = 0;
  std::string file;  // ruta relativa al workspace (opcional)
};

class ISymbolProvider {
 public:
  virtual ~ISymbolProvider() = default;
  virtual std::vector<SymbolInfo> symbols_for_file(const std::string& path) = 0;

  virtual bool indexes_workspace_bulk() const { return true; }
  virtual std::vector<SymbolInfo> workspace_symbols(const std::string& workspace_root,
                                                      const std::string& query) {
    (void)workspace_root;
    (void)query;
    return {};
  }

  virtual void on_workspace_opened(const std::string& root) { (void)root; }
  virtual void on_workspace_closed() {}
  virtual void on_document_opened(const std::string& path, const std::string& text) {
    (void)path;
    (void)text;
  }
  virtual void on_document_changed(const std::string& path, const std::string& text) {
    (void)path;
    (void)text;
  }
  virtual void on_document_closed(const std::string& path) { (void)path; }
};

}  // namespace tgdb
