#include "ai/l2_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "ai/l2_wave.hpp"

namespace fs = std::filesystem;

namespace tuide {
namespace {

std::string ascii_fold(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 128) {
      out.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
      ++i;
      continue;
    }
    char mapped = 0;
    if (i + 1 < s.size() && c == 0xc3) {
      const unsigned char d = static_cast<unsigned char>(s[i + 1]);
      if (d == 0xa1 || d == 0xa0) {
        mapped = 'a';
      } else if (d == 0xa9 || d == 0xa8) {
        mapped = 'e';
      } else if (d == 0xad || d == 0xac) {
        mapped = 'i';
      } else if (d == 0xb3 || d == 0xb2) {
        mapped = 'o';
      } else if (d == 0xba || d == 0xb9 || d == 0xbc) {
        mapped = 'u';
      } else if (d == 0xb1) {
        mapped = 'n';
      }
    }
    if (mapped != 0) {
      out.push_back(mapped);
      i += 2;
    } else {
      ++i;
    }
  }
  return out;
}

bool catalog_stopword(const std::string& t) {
  static const char* k[] = {"a",   "al",  "de",  "del", "el",  "la",  "los", "las", "un",
                            "una", "en",  "y",   "o",   "que", "con", "por", "para", "the",
                            "of",  "to",  "and", "or",  "for", "from"};
  for (const char* s : k) {
    if (t == s) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> ident_tokens(const std::string& s) {
  std::vector<std::string> toks;
  std::string cur;
  auto flush = [&]() {
    if (cur.size() >= 2 && !catalog_stopword(cur)) {
      toks.push_back(cur);
    }
    cur.clear();
  };
  for (std::size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == '_' ) {
      cur.push_back('_');
      continue;
    }
    if (c == '-' || c == ':' || c == '/' || c == '.') {
      flush();
      continue;
    }
    if (std::isupper(c) != 0 && !cur.empty() && cur.back() != '_') {
      flush();
    }
    if (std::isalnum(c) == 0) {
      flush();
      continue;
    }
    cur.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
  }
  flush();
  return toks;
}

std::vector<std::string> query_tokens(const std::string& q) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  for (const auto& t : ident_tokens(ascii_fold(q))) {
    if (seen.insert(t).second) {
      out.push_back(t);
    }
  }
  return out;
}

bool ident_looks_proper(const std::string& s) {
  return !s.empty() && std::isupper(static_cast<unsigned char>(s[0])) != 0;
}

bool field_has_token(const std::string& field, const std::string& tok, bool allow_prefix = true) {
  if (field.empty() || tok.size() < 2) {
    return false;
  }
  for (const auto& t : ident_tokens(field)) {
    if (t == tok) {
      return true;
    }
    // Prefijo de palabra simple (keys ← key), no a través de '_' (escape_html).
    if (allow_prefix && tok.size() >= 3 && t.size() > tok.size() &&
        t.compare(0, tok.size(), tok) == 0 && t[tok.size()] != '_') {
      return true;
    }
  }
  return false;
}

bool list_has_token(const std::vector<std::string>& list, const std::string& tok) {
  for (const auto& s : list) {
    if (field_has_token(s, tok)) {
      return true;
    }
  }
  return false;
}

std::string fnv1a_hex(const std::string& s) {
  std::uint64_t h = 14695981039346656037ull;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ull;
  }
  char buf[17];
  std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
  return buf;
}

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool write_atomic(const fs::path& dest, const std::string& body, std::string* err) {
  std::error_code ec;
  fs::create_directories(dest.parent_path(), ec);
  if (ec) {
    if (err) {
      *err = "catalog: no se pudo crear " + dest.parent_path().string();
    }
    return false;
  }
  const fs::path tmp = dest.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
      if (err) {
        *err = "catalog: no se pudo escribir " + tmp.string();
      }
      return false;
    }
    out << body;
  }
  fs::rename(tmp, dest, ec);
  if (ec) {
    if (err) {
      *err = "catalog: rename " + dest.string();
    }
    return false;
  }
  return true;
}

bool catalog_ext_ok(const std::string& rel) {
  auto dot = rel.rfind('.');
  if (dot == std::string::npos || dot + 1 >= rel.size()) {
    return false;
  }
  std::string ext = rel.substr(dot);
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" || ext == ".h" ||
         ext == ".hpp" || ext == ".hh" || ext == ".hxx";
}

bool catalog_admit_rel(const std::string& rel) {
  if (rel.rfind("src/", 0) != 0 && rel.rfind("src\\", 0) != 0) {
    return false;
  }
  if (rel.find("/._") != std::string::npos || rel.find("\\._") != std::string::npos) {
    return false;
  }
  const auto slash = rel.find_last_of("/\\");
  const std::string base = slash == std::string::npos ? rel : rel.substr(slash + 1);
  if (!base.empty() && base[0] == '.') {
    return false;
  }
  return catalog_ext_ok(rel);
}

std::string catalog_stem_of(const std::string& path) {
  std::string base = path;
  const auto slash = base.find_last_of("/\\");
  if (slash != std::string::npos) {
    base = base.substr(slash + 1);
  }
  const auto colon = base.find(':');
  if (colon != std::string::npos) {
    base = base.substr(0, colon);
  }
  const auto dot = base.rfind('.');
  if (dot != std::string::npos && dot > 0) {
    base = base.substr(0, dot);
  }
  return base;
}

std::string posix_rel(std::string rel) {
  for (char& c : rel) {
    if (c == '\\') {
      c = '/';
    }
  }
  return rel;
}

nlohmann::json card_to_json(const CatalogFnCard& c) {
  return {{"id", c.id},
          {"barrio", c.barrio},
          {"stem", c.stem},
          {"path", c.path},
          {"symbol", c.symbol},
          {"kind", c.kind},
          {"roles", c.roles},
          {"writes", c.writes},
          {"reads", c.reads},
          {"calls", c.calls},
          {"hot", c.hot},
          {"preds", c.preds},
          {"refs_in", c.refs_in},
          {"file_hash", c.file_hash}};
}

CatalogFnCard card_from_json(const nlohmann::json& j) {
  CatalogFnCard c;
  c.id = j.value("id", "");
  c.barrio = j.value("barrio", "");
  c.stem = j.value("stem", "");
  c.path = j.value("path", "");
  c.symbol = j.value("symbol", "");
  c.kind = j.value("kind", "");
  c.roles = j.value("roles", std::vector<std::string>{});
  c.writes = j.value("writes", std::vector<std::string>{});
  c.reads = j.value("reads", std::vector<std::string>{});
  c.calls = j.value("calls", std::vector<std::string>{});
  c.hot = j.value("hot", std::vector<std::string>{});
  c.preds = j.value("preds", std::vector<std::string>{});
  c.refs_in = j.value("refs_in", 0);
  c.file_hash = j.value("file_hash", "");
  return c;
}

void collect_includes(const std::string& source, std::vector<std::string>* out) {
  if (out == nullptr) {
    return;
  }
  const std::string key = "#include";
  std::size_t pos = 0;
  while ((pos = source.find(key, pos)) != std::string::npos) {
    std::size_t i = pos + key.size();
    while (i < source.size() && (source[i] == ' ' || source[i] == '\t')) {
      ++i;
    }
    if (i < source.size() && source[i] == '"') {
      const auto end = source.find('"', i + 1);
      if (end != std::string::npos && end > i + 1) {
        out->push_back(source.substr(i + 1, end - i - 1));
      }
    }
    ++pos;
  }
}

std::string resolve_include(const std::string& from_rel, const std::string& inc,
                            const std::unordered_set<std::string>& files) {
  std::string want = posix_rel(inc);
  const std::string via_src = "src/" + want;
  if (files.count(via_src)) {
    return via_src;
  }
  auto slash = from_rel.find_last_of('/');
  std::string dir = slash == std::string::npos ? std::string{} : from_rel.substr(0, slash + 1);
  const std::string sibling = posix_rel(dir + want);
  if (files.count(sibling)) {
    return sibling;
  }
  return {};
}

std::vector<std::string> distinctive_tokens(const std::vector<std::string>& local,
                                            const std::unordered_map<std::string, int>& global_df,
                                            int n_docs, int keep) {
  std::map<std::string, int> tf;
  for (const auto& tok : local) {
    ++tf[tok];
  }
  std::vector<std::pair<float, std::string>> ranked;
  for (const auto& kv : tf) {
    if (kv.first.size() < 3) {
      continue;
    }
    const int df = std::max(1, global_df.count(kv.first) ? global_df.at(kv.first) : 1);
    const float idf = 1.f + static_cast<float>(n_docs) / static_cast<float>(df);
    ranked.push_back({static_cast<float>(kv.second) * idf, kv.first});
  }
  std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
    if (a.first != b.first) {
      return a.first > b.first;
    }
    return a.second < b.second;
  });
  std::vector<std::string> out;
  for (const auto& r : ranked) {
    if (static_cast<int>(out.size()) >= keep) {
      break;
    }
    out.push_back(r.second);
  }
  return out;
}

const char* heat_dots(float heat, float max_heat) {
  if (max_heat <= 0.f || heat <= 0.f) {
    return "·";
  }
  const float r = heat / max_heat;
  if (r >= 0.66f) {
    return "●●●";
  }
  if (r >= 0.33f) {
    return "●●";
  }
  return "●";
}

std::string join_words(const std::vector<std::string>& v, int cap) {
  std::ostringstream out;
  int n = 0;
  for (const auto& s : v) {
    if (n >= cap) {
      break;
    }
    if (n) {
      out << ' ';
    }
    out << s;
    ++n;
  }
  return out.str();
}

std::string join_habla(const std::vector<std::string>& v, int cap = kCatalogHablaCap) {
  std::ostringstream out;
  int n = 0;
  for (const auto& s : v) {
    if (n >= cap) {
      break;
    }
    if (n) {
      out << ", ";
    }
    out << s;
    ++n;
  }
  return out.str();
}

std::vector<std::string> habla_by_heat(const Catalog& c, const std::vector<std::string>& habla) {
  std::unordered_map<std::string, float> heat;
  for (const auto& s : c.stems) {
    heat[s.id] = s.heat;
  }
  for (const auto& b : c.barrios) {
    heat[b.id] = std::max(heat[b.id], b.heat);
  }
  std::vector<std::string> out = habla;
  std::sort(out.begin(), out.end(), [&](const std::string& a, const std::string& b) {
    const float ha = heat[a];
    const float hb = heat[b];
    if (ha != hb) {
      return ha > hb;
    }
    return a < b;
  });
  return out;
}

float pred_token_heat(const std::vector<std::string>& preds, const std::string& tok) {
  float h = 0.f;
  for (const auto& p : preds) {
    if (!field_has_token(p, tok, false)) {
      continue;
    }
    h = std::max(h, ident_looks_proper(p) ? 4.f : 2.f);
  }
  return h;
}

float write_token_heat(const std::vector<std::string>& writes, const std::string& tok) {
  float h = 0.f;
  for (const auto& w : writes) {
    if (!field_has_token(w, tok, false)) {
      continue;
    }
    const bool strong =
        ident_looks_proper(w) || w.find('.') != std::string::npos || w.find('_') != std::string::npos;
    h = std::max(h, strong ? 4.f : 1.f);
  }
  return h;
}

float card_heat(const CatalogFnCard& c, const std::vector<std::string>& toks) {
  float s = 0.f;
  for (const auto& tok : toks) {
    if (field_has_token(c.symbol, tok)) {
      s += 8.f;
    }
    if (list_has_token(c.hot, tok)) {
      s += 4.f;
    }
    s += write_token_heat(c.writes, tok);
    s += pred_token_heat(c.preds, tok);
    if (list_has_token(c.roles, tok)) {
      s += 2.f;
    }
    if (list_has_token(c.calls, tok) || list_has_token(c.reads, tok)) {
      s += 1.f;
    }
    if (field_has_token(c.stem, tok)) {
      s += 1.f;
    }
  }
  return s;
}

void append_barrio_capas(std::ostringstream& out, const Catalog& c, const std::string& barrio) {
  std::unordered_map<std::string, float> stem_heat;
  int n_stems = 0;
  for (const auto& s : c.stems) {
    if (s.barrio != barrio) {
      continue;
    }
    stem_heat[s.id] = s.heat;
    ++n_stems;
  }
  if (n_stems < 2) {
    return;
  }
  std::unordered_map<std::string, std::unordered_set<std::string>> pred_stems;
  std::unordered_map<std::string, std::string> pred_label;
  std::unordered_map<std::string, std::unordered_map<std::string, int>> pred_stem_n;
  for (const auto& card : c.cards) {
    if (card.barrio != barrio) {
      continue;
    }
    for (const auto& p : card.preds) {
      const std::string key = ascii_fold(p);
      if (key.size() < 3) {
        continue;
      }
      pred_stems[key].insert(card.stem);
      pred_stem_n[key][card.stem] += 1;
      auto lit = pred_label.find(key);
      if (lit == pred_label.end() ||
          (ident_looks_proper(p) && !ident_looks_proper(lit->second))) {
        pred_label[key] = p;
      }
    }
  }
  struct Layer {
    std::string pred;
    std::vector<std::string> ids;
    float score = 0.f;
  };
  std::vector<Layer> layers;
  for (const auto& kv : pred_stems) {
    const int df = static_cast<int>(kv.second.size());
    if (df < 2) {
      continue;
    }
    if (n_stems >= 3 && df * 3 > n_stems * 2) {
      continue;
    }
    Layer layer;
    auto lit = pred_label.find(kv.first);
    layer.pred = lit == pred_label.end() ? kv.first : lit->second;
    layer.ids.assign(kv.second.begin(), kv.second.end());
    std::sort(layer.ids.begin(), layer.ids.end(), [&](const std::string& a, const std::string& b) {
      if (stem_heat[a] != stem_heat[b]) {
        return stem_heat[a] > stem_heat[b];
      }
      const int na = pred_stem_n[kv.first][a];
      const int nb = pred_stem_n[kv.first][b];
      if (na != nb) {
        return na > nb;
      }
      return a < b;
    });
    if (static_cast<int>(layer.ids.size()) > kCatalogCapaStems) {
      layer.ids.resize(static_cast<std::size_t>(kCatalogCapaStems));
    }
    const float idf = static_cast<float>(n_stems) / static_cast<float>(std::max(1, df));
    float heat_sum = 0.f;
    for (const auto& id : kv.second) {
      heat_sum += stem_heat[id];
    }
    layer.score = idf + heat_sum;
    layers.push_back(std::move(layer));
  }
  std::sort(layers.begin(), layers.end(), [](const Layer& a, const Layer& b) {
    if (a.score != b.score) {
      return a.score > b.score;
    }
    return a.pred < b.pred;
  });
  int shown = 0;
  for (const auto& layer : layers) {
    if (shown >= kCatalogCapaLayers) {
      break;
    }
    out << "  " << layer.pred << ":";
    for (std::size_t i = 0; i < layer.ids.size(); ++i) {
      out << (i ? "," : "") << " " << layer.ids[i];
    }
    out << "\n";
    ++shown;
  }
}

}  // namespace

std::string catalog_barrio_of_path(const std::string& path) {
  std::string b = wave_control_barrio_of_path(path);
  if (!b.empty()) {
    return b;
  }
  auto p = path.find("src/");
  if (p == std::string::npos) {
    p = path.find("src\\");
  }
  if (p == std::string::npos) {
    return {};
  }
  std::string rest = path.substr(p + 4);
  if (rest.empty()) {
    return {};
  }
  const auto slash = rest.find_first_of("/\\");
  if (slash == std::string::npos) {
    return "src";
  }
  if (slash == 0) {
    return {};
  }
  return rest.substr(0, slash);
}

std::string catalog_dir(const std::string& workspace_root) {
  return (fs::path(workspace_root) / ".tuide" / "catalog").string();
}

bool catalog_has_id(const Catalog& c, const std::string& id) {
  return !catalog_id_kind(c, id).empty();
}

std::string catalog_id_kind(const Catalog& c, const std::string& id) {
  if (id.empty()) {
    return {};
  }
  for (const auto& b : c.barrios) {
    if (b.id == id) {
      return "barrio";
    }
  }
  for (const auto& s : c.stems) {
    if (s.id == id) {
      return "stem";
    }
  }
  return {};
}

void catalog_apply_heat(Catalog* c, const std::string& query) {
  if (c == nullptr) {
    return;
  }
  for (auto& card : c->cards) {
    card.heat = 0.f;
  }
  for (auto& s : c->stems) {
    s.heat = 0.f;
  }
  for (auto& b : c->barrios) {
    b.heat = 0.f;
  }
  const auto toks = query_tokens(query);
  if (toks.empty()) {
    return;
  }
  std::unordered_map<std::string, float> stem_heat;
  for (auto& card : c->cards) {
    card.heat = card_heat(card, toks);
    stem_heat[card.stem] = std::max(stem_heat[card.stem], card.heat);
  }
  std::unordered_map<std::string, float> barrio_heat;
  for (auto& st : c->stems) {
    st.heat = stem_heat[st.id];
    for (const auto& tok : toks) {
      if (list_has_token(st.legend, tok) || field_has_token(st.id, tok)) {
        st.heat += 2.f;
      }
    }
    barrio_heat[st.barrio] += st.heat;
  }
  for (auto& b : c->barrios) {
    b.heat = barrio_heat[b.id];
    for (const auto& tok : toks) {
      if (list_has_token(b.legend, tok) || field_has_token(b.id, tok)) {
        b.heat += 2.f;
      }
    }
  }
}

void catalog_apply_heat(Catalog* c, const std::vector<std::string>& needles) {
  std::string q;
  for (const auto& n : needles) {
    if (n.empty()) {
      continue;
    }
    if (!q.empty()) {
      q.push_back(' ');
    }
    q += n;
  }
  catalog_apply_heat(c, q);
}

static std::unordered_map<std::string, int> stem_hot_n(const Catalog& c) {
  std::unordered_map<std::string, int> n;
  for (const auto& card : c.cards) {
    if (card.heat > 0.f) {
      ++n[card.stem];
    }
  }
  return n;
}

static void sort_stems_by_heat(std::vector<CatalogStem>* stems, const Catalog& c) {
  if (stems == nullptr) {
    return;
  }
  const auto hot_n = stem_hot_n(c);
  std::sort(stems->begin(), stems->end(), [&](const CatalogStem& a, const CatalogStem& b) {
    if (a.heat != b.heat) {
      return a.heat > b.heat;
    }
    const int na = hot_n.count(a.id) ? hot_n.at(a.id) : 0;
    const int nb = hot_n.count(b.id) ? hot_n.at(b.id) : 0;
    if (na != nb) {
      return na > nb;
    }
    return a.id < b.id;
  });
}

std::string catalog_render_plano(const Catalog& c) {
  std::vector<CatalogBarrio> bars = c.barrios;
  std::sort(bars.begin(), bars.end(), [](const CatalogBarrio& a, const CatalogBarrio& b) {
    if (a.heat != b.heat) {
      return a.heat > b.heat;
    }
    return a.id < b.id;
  });
  float max_h = 0.f;
  for (const auto& b : bars) {
    max_h = std::max(max_h, b.heat);
  }
  std::ostringstream out;
  out << "plano (calor del ancla; no copies nombres ni ids):\n";
  if (bars.empty()) {
    out << "  (vacío)\n";
    return out.str();
  }
  for (const auto& b : bars) {
    out << "  " << b.id << " " << heat_dots(b.heat, max_h);
    const std::string leg = join_words(b.legend, kCatalogLegendBarrio);
    if (!leg.empty()) {
      out << "  " << leg;
    }
    if (!b.habla.empty()) {
      out << "  habla: " << join_habla(habla_by_heat(c, b.habla));
    }
    out << "\n";
  }
  if (!bars.empty() && !c.stems.empty()) {
    const std::string top_b = bars.front().id;
    std::vector<CatalogStem> tops;
    for (const auto& s : c.stems) {
      if (s.barrio == top_b) {
        tops.push_back(s);
      }
    }
    sort_stems_by_heat(&tops, c);
    if (!tops.empty()) {
      out << "  top " << top_b << ":";
      for (int i = 0; i < static_cast<int>(tops.size()) && i < kCatalogPlanoTopStems; ++i) {
        out << (i ? "," : "") << " " << tops[static_cast<std::size_t>(i)].id;
      }
      out << "\n";
    }
  }
  return out.str();
}

std::string catalog_render_zoom(const Catalog& c, const std::string& id) {
  const std::string kind = catalog_id_kind(c, id);
  std::ostringstream out;
  if (kind == "barrio") {
    out << "zoom " << id << " (stems; no copies ids):\n";
    std::vector<CatalogStem> tops;
    for (const auto& s : c.stems) {
      if (s.barrio == id) {
        tops.push_back(s);
      }
    }
    sort_stems_by_heat(&tops, c);
    const int cap = kCatalogZoomBarrioStems;
    int shown = 0;
    for (const auto& s : tops) {
      if (shown >= cap) {
        break;
      }
      out << "  " << s.id;
      const std::string leg = join_words(s.legend, kCatalogLegendStem);
      if (!leg.empty()) {
        out << "  " << leg;
      }
      if (!s.habla.empty()) {
        out << "  habla: " << join_habla(habla_by_heat(c, s.habla));
      }
      out << "\n";
      ++shown;
    }
    if (shown == 0) {
      out << "  (sin stems)\n";
    } else if (static_cast<int>(tops.size()) > shown) {
      out << "  … +" << (static_cast<int>(tops.size()) - shown) << " stems\n";
    }
    append_barrio_capas(out, c, id);
    return out.str();
  }
  if (kind == "stem") {
    std::string barrio;
    for (const auto& s : c.stems) {
      if (s.id == id) {
        barrio = s.barrio;
        break;
      }
    }
    out << "zoom " << id;
    if (!barrio.empty()) {
      out << " (" << barrio << ")";
    }
    out << " (fichas slim; no es el código):\n";
    std::vector<CatalogFnCard> cards;
    for (const auto& card : c.cards) {
      if (card.stem == id) {
        cards.push_back(card);
      }
    }
    std::sort(cards.begin(), cards.end(), [](const CatalogFnCard& a, const CatalogFnCard& b) {
      if (a.heat != b.heat) {
        return a.heat > b.heat;
      }
      if (a.refs_in != b.refs_in) {
        return a.refs_in > b.refs_in;
      }
      return a.symbol < b.symbol;
    });
    int n = 0;
    for (const auto& card : cards) {
      if (n >= kCatalogZoomStemCards) {
        break;
      }
      out << "  " << card.symbol;
      if (!card.roles.empty()) {
        out << "  roles=" << join_habla(card.roles, kCatalogCardList);
      }
      if (!card.writes.empty()) {
        out << "  writes=" << join_habla(card.writes, kCatalogCardList);
      }
      if (!card.calls.empty()) {
        out << "  calls=" << join_habla(card.calls, kCatalogCardList);
      }
      if (!card.preds.empty()) {
        out << "  preds=" << join_habla(card.preds, kCatalogCardList);
      }
      out << "\n";
      ++n;
    }
    if (n == 0) {
      out << "  (sin fichas)\n";
    }
    return out.str();
  }
  return {};
}

std::string catalog_render_zooms(const Catalog& c, const std::vector<std::string>& ids) {
  std::ostringstream out;
  for (const auto& id : ids) {
    const std::string block = catalog_render_zoom(c, id);
    if (block.empty()) {
      continue;
    }
    out << block;
    if (block.back() != '\n') {
      out << "\n";
    }
  }
  return out.str();
}

std::string catalog_render_search(const Catalog& c, int k) {
  std::vector<CatalogFnCard> cards = c.cards;
  std::sort(cards.begin(), cards.end(), [](const CatalogFnCard& a, const CatalogFnCard& b) {
    if (a.heat != b.heat) {
      return a.heat > b.heat;
    }
    return a.id < b.id;
  });
  std::ostringstream out;
  out << "search (top fichas por calor; no es el código):\n";
  int n = 0;
  const int cap = k > 0 ? k : kCatalogSearchK;
  for (const auto& card : cards) {
    if (n >= cap) {
      break;
    }
    if (card.heat <= 0.f && n > 0) {
      break;
    }
    out << "  " << card.barrio << "/" << card.stem << " " << card.symbol;
    if (!card.roles.empty()) {
      out << "  " << join_habla(card.roles);
    }
    out << "\n";
    ++n;
  }
  if (n == 0) {
    out << "  (sin hits)\n";
  }
  return out.str();
}

bool catalog_load(const std::string& workspace_root, Catalog* out, std::string* err) {
  if (out == nullptr) {
    if (err) {
      *err = "catalog: args";
    }
    return false;
  }
  const fs::path dir = catalog_dir(workspace_root);
  const std::string man = read_file((dir / "manifest.json").string());
  const std::string cat = read_file((dir / "catalog.json").string());
  const std::string cards_body = read_file((dir / "cards.jsonl").string());
  if (man.empty() || cat.empty()) {
    if (err) {
      *err = "catalog: no hay censo";
    }
    return false;
  }
  try {
    const auto mj = nlohmann::json::parse(man);
    const auto cj = nlohmann::json::parse(cat);
    *out = {};
    out->schema = cj.value("schema", kCatalogSchema);
    out->censo = cj.value("censo", mj.value("censo", ""));
    for (const auto& b : cj.value("barrios", nlohmann::json::array())) {
      CatalogBarrio row;
      row.id = b.value("id", "");
      row.n_stems = b.value("n_stems", 0);
      row.legend = b.value("legend", std::vector<std::string>{});
      row.habla = b.value("habla", std::vector<std::string>{});
      if (!row.id.empty()) {
        out->barrios.push_back(std::move(row));
      }
    }
    for (const auto& s : cj.value("stems", nlohmann::json::array())) {
      CatalogStem row;
      row.id = s.value("id", "");
      row.barrio = s.value("barrio", "");
      row.paths = s.value("paths", std::vector<std::string>{});
      row.legend = s.value("legend", std::vector<std::string>{});
      row.habla = s.value("habla", std::vector<std::string>{});
      if (!row.id.empty()) {
        out->stems.push_back(std::move(row));
      }
    }
    std::istringstream in(cards_body);
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty()) {
        continue;
      }
      out->cards.push_back(card_from_json(nlohmann::json::parse(line)));
    }
  } catch (const std::exception& e) {
    if (err) {
      *err = std::string("catalog: json ") + e.what();
    }
    return false;
  }
  return true;
}

bool catalog_ensure(const std::string& workspace_root, Catalog* out, std::string* err, bool force) {
  if (out == nullptr || workspace_root.empty()) {
    if (err) {
      *err = "catalog: args";
    }
    return false;
  }
  std::error_code ec;
  const fs::path root(workspace_root);
  const fs::path src = root / "src";
  struct FileStamp {
    std::string rel;
    long long mt = 0;
    std::uintmax_t sz = 0;
  };
  std::vector<FileStamp> stamps;
  if (fs::is_directory(src, ec)) {
    for (auto it = fs::recursive_directory_iterator(src, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec)) {
        continue;
      }
      std::string rel = fs::relative(it->path(), root, ec).generic_string();
      if (ec || !catalog_admit_rel(rel)) {
        continue;
      }
      rel = posix_rel(rel);
      std::error_code tec;
      std::error_code sec;
      const auto mt = fs::last_write_time(it->path(), tec);
      const auto sz = fs::file_size(it->path(), sec);
      if (tec || sec) {
        continue;
      }
      stamps.push_back(
          {rel, static_cast<long long>(mt.time_since_epoch().count()), sz});
    }
  }
  std::sort(stamps.begin(), stamps.end(),
            [](const FileStamp& a, const FileStamp& b) { return a.rel < b.rel; });
  std::ostringstream hash_src;
  std::vector<std::string> rels;
  rels.reserve(stamps.size());
  for (const auto& s : stamps) {
    hash_src << s.rel << '\t' << s.mt << '\t' << s.sz << '\n';
    rels.push_back(s.rel);
  }
  const std::string censo = fnv1a_hex(hash_src.str());
  if (!force) {
    Catalog loaded;
    std::string lerr;
    if (catalog_load(workspace_root, &loaded, &lerr) && loaded.schema == kCatalogSchema &&
        loaded.censo == censo) {
      *out = std::move(loaded);
      out->cache_hit = true;
      return true;
    }
  }

  std::unordered_set<std::string> file_set(rels.begin(), rels.end());
  std::vector<CatalogFnCard> cards;
  std::unordered_map<std::string, std::vector<std::string>> stem_paths;
  std::unordered_map<std::string, std::string> stem_barrio;
  std::unordered_map<std::string, std::vector<std::string>> stem_tokens;
  std::vector<std::pair<std::string, std::string>> include_edges;
  for (const auto& rel : rels) {
    const std::string abs = (root / rel).string();
    const std::string source = read_file(abs);
    const std::string fh = fnv1a_hex(source);
    auto file_cards = catalog_cards_from_file(abs, rel, source);
    const std::string barrio = catalog_barrio_of_path(rel);
    const std::string stem = catalog_stem_of(rel);
    if (!stem.empty()) {
      auto& paths = stem_paths[stem];
      if (std::find(paths.begin(), paths.end(), rel) == paths.end()) {
        paths.push_back(rel);
      }
      if (stem_barrio[stem].empty() && !barrio.empty()) {
        stem_barrio[stem] = barrio;
      }
    }
    for (auto& card : file_cards) {
      card.barrio = barrio;
      card.stem = stem;
      card.file_hash = fh;
      for (const auto& tok : ident_tokens(card.symbol)) {
        stem_tokens[stem].push_back(tok);
      }
      for (const auto& p : card.preds) {
        const std::string folded = ascii_fold(p);
        if (!folded.empty()) {
          stem_tokens[stem].push_back(folded);
        }
      }
      cards.push_back(std::move(card));
    }
    for (const auto& tok : ident_tokens(stem)) {
      stem_tokens[stem].push_back(tok);
    }
    std::vector<std::string> incs;
    collect_includes(source, &incs);
    for (const auto& inc : incs) {
      const std::string to_rel = resolve_include(rel, inc, file_set);
      if (to_rel.empty()) {
        continue;
      }
      const std::string to_stem = catalog_stem_of(to_rel);
      if (!stem.empty() && !to_stem.empty() && stem != to_stem) {
        include_edges.push_back({stem, to_stem});
      }
    }
  }

  std::unordered_map<std::string, int> symbol_owners;
  std::unordered_map<std::string, std::string> unique_sym_stem;
  for (const auto& card : cards) {
    ++symbol_owners[card.symbol];
    unique_sym_stem[card.symbol] = card.stem;
  }
  for (auto it = unique_sym_stem.begin(); it != unique_sym_stem.end();) {
    if (symbol_owners[it->first] != 1) {
      it = unique_sym_stem.erase(it);
    } else {
      ++it;
    }
  }
  std::unordered_map<std::string, int> refs_in;
  std::vector<std::pair<std::string, std::string>> call_edges;
  for (const auto& card : cards) {
    for (const auto& cal : card.calls) {
      auto uit = unique_sym_stem.find(cal);
      if (uit == unique_sym_stem.end()) {
        continue;
      }
      ++refs_in[cal];
      if (uit->second != card.stem) {
        call_edges.push_back({card.stem, uit->second});
      }
    }
  }
  for (auto& card : cards) {
    auto it = refs_in.find(card.symbol);
    if (it != refs_in.end()) {
      card.refs_in = it->second;
    }
  }

  std::unordered_map<std::string, int> global_df;
  for (const auto& kv : stem_tokens) {
    std::unordered_set<std::string> uniq(kv.second.begin(), kv.second.end());
    for (const auto& t : uniq) {
      ++global_df[t];
    }
  }
  const int n_stems = std::max(1, static_cast<int>(stem_paths.size()));

  std::unordered_map<std::string, std::unordered_set<std::string>> stem_habla;
  auto add_habla = [&](const std::string& a, const std::string& b) {
    if (a.empty() || b.empty() || a == b) {
      return;
    }
    stem_habla[a].insert(b);
  };
  for (const auto& e : include_edges) {
    add_habla(e.first, e.second);
  }
  for (const auto& e : call_edges) {
    add_habla(e.first, e.second);
  }

  Catalog built;
  built.schema = kCatalogSchema;
  built.censo = censo;
  built.cache_hit = false;
  built.cards = std::move(cards);

  std::map<std::string, std::vector<std::string>> barrio_stems;
  std::map<std::string, std::vector<std::string>> barrio_tokens;
  for (const auto& kv : stem_paths) {
    CatalogStem st;
    st.id = kv.first;
    st.barrio = stem_barrio[kv.first];
    if (st.barrio.empty()) {
      st.barrio = "src";
    }
    st.paths = kv.second;
    st.legend = distinctive_tokens(stem_tokens[kv.first], global_df, n_stems, kCatalogLegendStem);
    auto hit = stem_habla.find(st.id);
    if (hit != stem_habla.end()) {
      st.habla.assign(hit->second.begin(), hit->second.end());
      std::sort(st.habla.begin(), st.habla.end());
    }
    barrio_stems[st.barrio].push_back(st.id);
    barrio_tokens[st.barrio].insert(barrio_tokens[st.barrio].end(), st.legend.begin(),
                                    st.legend.end());
    for (const auto& tok : ident_tokens(st.id)) {
      barrio_tokens[st.barrio].push_back(tok);
    }
    built.stems.push_back(std::move(st));
  }
  std::sort(built.stems.begin(), built.stems.end(),
            [](const CatalogStem& a, const CatalogStem& b) { return a.id < b.id; });

  std::unordered_map<std::string, int> barrio_df;
  for (const auto& kv : barrio_tokens) {
    std::unordered_set<std::string> uniq(kv.second.begin(), kv.second.end());
    for (const auto& t : uniq) {
      ++barrio_df[t];
    }
  }
  const int n_barrios = std::max(1, static_cast<int>(barrio_stems.size()));
  std::unordered_map<std::string, std::string> stem_to_barrio;
  for (const auto& s : built.stems) {
    stem_to_barrio[s.id] = s.barrio;
  }
  std::unordered_map<std::string, std::unordered_set<std::string>> barrio_habla;
  for (const auto& s : built.stems) {
    for (const auto& to : s.habla) {
      const std::string tb = stem_to_barrio[to];
      if (!tb.empty() && tb != s.barrio) {
        barrio_habla[s.barrio].insert(tb);
      }
    }
  }
  for (const auto& kv : barrio_stems) {
    CatalogBarrio b;
    b.id = kv.first;
    b.n_stems = static_cast<int>(kv.second.size());
    b.legend = distinctive_tokens(barrio_tokens[kv.first], barrio_df, n_barrios, kCatalogLegendBarrio);
    auto hit = barrio_habla.find(b.id);
    if (hit != barrio_habla.end()) {
      b.habla.assign(hit->second.begin(), hit->second.end());
      std::sort(b.habla.begin(), b.habla.end());
    }
    built.barrios.push_back(std::move(b));
  }
  std::sort(built.barrios.begin(), built.barrios.end(),
            [](const CatalogBarrio& a, const CatalogBarrio& b) { return a.id < b.id; });

  nlohmann::json cj;
  cj["schema"] = kCatalogSchema;
  cj["censo"] = censo;
  cj["barrios"] = nlohmann::json::array();
  for (const auto& b : built.barrios) {
    cj["barrios"].push_back({{"id", b.id},
                             {"n_stems", b.n_stems},
                             {"legend", b.legend},
                             {"habla", b.habla}});
  }
  cj["stems"] = nlohmann::json::array();
  for (const auto& s : built.stems) {
    cj["stems"].push_back({{"id", s.id},
                           {"barrio", s.barrio},
                           {"paths", s.paths},
                           {"legend", s.legend},
                           {"habla", s.habla}});
  }
  std::ostringstream cards_out;
  for (const auto& card : built.cards) {
    cards_out << card_to_json(card).dump() << "\n";
  }
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto ts = std::chrono::duration_cast<std::chrono::seconds>(now).count();
  nlohmann::json mj = {{"schema", kCatalogSchema},
                       {"censo", censo},
                       {"n_cards", built.cards.size()},
                       {"n_stems", built.stems.size()},
                       {"n_barrios", built.barrios.size()},
                       {"ts", ts}};
  const fs::path dir = catalog_dir(workspace_root);
  if (!write_atomic(dir / "manifest.json", mj.dump(2) + "\n", err)) {
    return false;
  }
  if (!write_atomic(dir / "catalog.json", cj.dump(2) + "\n", err)) {
    return false;
  }
  if (!write_atomic(dir / "cards.jsonl", cards_out.str(), err)) {
    return false;
  }
  const std::string plano = catalog_render_plano(built);
  write_atomic(dir / "plano.md", plano, nullptr);
  *out = std::move(built);
  return true;
}

}  // namespace tuide
