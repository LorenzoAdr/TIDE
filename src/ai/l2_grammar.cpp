#include "ai/l2_grammar.hpp"

#include <filesystem>

#include "ai/l2_feat.hpp"

namespace fs = std::filesystem;

namespace tuide {
namespace l2_grammar {

std::string resolve_for_phase(const std::string& workspace_root, const std::string& phase) {
  if (workspace_root.empty()) {
    return {};
  }
  const char* file = nullptr;
  if ((phase == "edit" || phase == "compile") && l2_feat::enabled("JSON_GRAMMAR")) {
    file = "l2_edit.gbnf";
  } else if ((phase == "admin" || phase == "admin_explore" || phase == "admin_verify" ||
              phase == "admin_verify_refute") &&
             l2_feat::enabled("ADMIN_JSON_GRAMMAR")) {
    // P1 (docs/plans/l2-admin-verify-round-reduction.md): solo garantiza JSON
    // bien formado (llaves/strings cerrados) para eliminar los reintentos
    // "JSON inválido, reemite" del piloto/explorador/verificador/refutador.
    // Las reglas de negocio (why/cubre/falta/…) siguen en admin_parse/admin_legal.
    file = "l2_json.gbnf";
  } else {
    return {};
  }
  const fs::path path = fs::path(workspace_root) / "tools/l2_battery/grammars" / file;
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    return {};
  }
  return fs::absolute(path).string();
}

}  // namespace l2_grammar
}  // namespace tuide
