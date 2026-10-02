#pragma once

#include <string>
#include <vector>

#include "ai/l2_effect_registry.hpp"
#include "ai/l2_effect_slice.hpp"

namespace tuide {

// Dos tiempos de cerca cuando `in` no es un símbolo inyectable:
//   File      → tanda cara: ficha ES + CardFull de (casi) todas las fns del archivo.
//   Directory → tanda barata (passage path+nombre+firma, como L1 fase A) y las
//               mejores se siembran en el grafo (oleada + CardFull).
// path:fn / stem / M* no despiertan: ya van al grafo o al filtro de barrio.

inline constexpr int kCercaWakeDirTopK = 16;
inline constexpr int kCercaWakeDirMaxPerFile = 4;
inline constexpr int kCercaWakeMaxFiles = 80;
inline constexpr int kCercaWakeMaxFnsCheap = 400;

enum class CercaScopeKind {
  Skip,       // vacío, M*, path:fn, stem suelto
  File,       // .cpp/.hpp existente
  Directory,  // directorio o prefijo src/foo
};

struct CercaWakeFn {
  std::string path;
  std::string symbol;
  int line = 0;
  std::string passage;
  float cheap_cos = -1.f;
};

struct CercaWakeReport {
  int files_scanned = 0;
  int fns_inventoried = 0;
  int cheap_ranked = 0;
  int seeded = 0;
  int ingested = 0;
  int embedded = 0;
  bool skipped_already_awake = false;
  std::vector<std::string> woken;  // scopes que pasaron por oleada
  std::string note;
};

struct CercaWakeOpts {
  std::string query;
  RegistryEmbedFn embed;
  RegistryEmbedManyFn embed_many;
  EffectSliceDeps deps;
  std::string model = kRegistryEmbedModelDefault;
  bool allow_fixtures = false;
};

CercaScopeKind cerca_classify_scope(const std::string& workspace_root, const std::string& scope);

// Passage corto = L1 fase A (coding_entry_passage): path + nombre + firma[:120].
std::string cerca_cheap_passage(const std::string& path, const std::string& name,
                                const std::string& signature);

// Inventario Tree-sitter (sin ficha ES). Vacío si el path no existe o no parsea.
std::vector<CercaWakeFn> cerca_inventory_file(const std::string& workspace_root,
                                              const std::string& rel);

// Rank barato: 1 embed de query + cosine contra passages. Diversidad por archivo.
std::vector<CercaWakeFn> cerca_rank_cheap(const std::vector<CercaWakeFn>& cands,
                                          const std::string& query, const RegistryEmbedFn& embed,
                                          const RegistryEmbedManyFn& embed_many, int top_k,
                                          int max_per_file, std::string* err);

// Despierta directorios/archivos fríos de `in` y embebe CardFull de lo admitido.
// No-op si el barrio ya tiene fichas, o si `in` es solo símbolos/stems.
bool registry_wake_cerca_scopes(EffectRegistry* r, const std::vector<std::string>& in_scopes,
                                const CercaWakeOpts& opts, CercaWakeReport* report,
                                std::string* err);

}  // namespace tuide
