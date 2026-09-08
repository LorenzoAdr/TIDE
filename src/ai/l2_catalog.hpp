#pragma once

#include <string>
#include <vector>

namespace tuide {

inline constexpr int kCatalogSchema = 3;
inline constexpr int kCatalogLegendBarrio = 6;
inline constexpr int kCatalogLegendStem = 4;
inline constexpr int kCatalogCardList = 6;
inline constexpr int kCatalogHablaCap = 4;
inline constexpr int kCatalogZoomBarrioStems = 8;
inline constexpr int kCatalogZoomStemCards = 12;
inline constexpr int kCatalogCapaLayers = 3;
inline constexpr int kCatalogCapaStems = 4;
inline constexpr int kCatalogPlanoTopStems = 2;
inline constexpr int kCatalogSearchK = 12;
inline constexpr int kCatalogMaxFnPerFile = 80;

struct CatalogFnCard {
  std::string id;
  std::string barrio;
  std::string stem;
  std::string path;
  std::string symbol;
  std::string kind;
  std::vector<std::string> roles;
  std::vector<std::string> writes;
  std::vector<std::string> reads;
  std::vector<std::string> calls;
  std::vector<std::string> hot;
  std::vector<std::string> preds;
  int refs_in = 0;
  std::string file_hash;
  float heat = 0.f;
};

struct CatalogStem {
  std::string id;
  std::string barrio;
  std::vector<std::string> paths;
  std::vector<std::string> legend;
  std::vector<std::string> habla;
  float heat = 0.f;
};

struct CatalogBarrio {
  std::string id;
  int n_stems = 0;
  std::vector<std::string> legend;
  std::vector<std::string> habla;
  float heat = 0.f;
};

struct Catalog {
  int schema = kCatalogSchema;
  std::string censo;
  std::vector<CatalogBarrio> barrios;
  std::vector<CatalogStem> stems;
  std::vector<CatalogFnCard> cards;
  bool cache_hit = false;
};

// Un parse TS por archivo. Sin seeds, sin query.
std::vector<CatalogFnCard> catalog_cards_from_file(const std::string& abs_path,
                                                   const std::string& rel_path,
                                                   const std::string& source);

std::string catalog_barrio_of_path(const std::string& path);
std::string catalog_dir(const std::string& workspace_root);

bool catalog_ensure(const std::string& workspace_root, Catalog* out, std::string* err,
                    bool force = false);
bool catalog_load(const std::string& workspace_root, Catalog* out, std::string* err);

void catalog_apply_heat(Catalog* c, const std::string& query);
void catalog_apply_heat(Catalog* c, const std::vector<std::string>& needles);

bool catalog_has_id(const Catalog& c, const std::string& id);
std::string catalog_id_kind(const Catalog& c, const std::string& id);  // barrio|stem|""

std::string catalog_render_plano(const Catalog& c);
std::string catalog_render_zoom(const Catalog& c, const std::string& id);
std::string catalog_render_zooms(const Catalog& c, const std::vector<std::string>& ids);
std::string catalog_render_search(const Catalog& c, int k = kCatalogSearchK);

}  // namespace tuide
