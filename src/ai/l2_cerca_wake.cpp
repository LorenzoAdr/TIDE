#include "ai/l2_cerca_wake.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "ai/vector_math.hpp"
#include "parser/tree_sitter_language.hpp"
#include "parser/tree_sitter_symbols.hpp"
#include "symbols/symbol_kind.hpp"
#include "symbols/symbol_utils.hpp"

extern "C" {
#include <tree_sitter/api.h>
}

namespace tuide {
namespace {

namespace fs = std::filesystem;

void set_err(std::string* err, const std::string& m) {
  if (err) {
    *err = m;
  }
}

std::string ascii_lower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return s;
}

std::string trim_copy(std::string s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
    s.erase(s.begin());
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
    s.pop_back();
  }
  return s;
}

std::string strip_trailing_slash(std::string s) {
  while (s.size() > 1 && (s.back() == '/' || s.back() == '\\')) {
    s.pop_back();
  }
  return s;
}

bool is_source_ext(std::string ext) {
  ext = ascii_lower(std::move(ext));
  return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" || ext == ".mm" ||
         ext == ".hpp" || ext == ".h" || ext == ".hh" || ext == ".hxx";
}

bool is_impl_ext(std::string ext) {
  ext = ascii_lower(std::move(ext));
  return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" || ext == ".mm";
}

bool looks_like_symbol(const std::string& s) {
  if (s.empty() || s.size() > 80) {
    return false;
  }
  unsigned char c0 = static_cast<unsigned char>(s[0]);
  if (!(std::isalpha(c0) || s[0] == '_' || s[0] == '~')) {
    return false;
  }
  for (char ch : s) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (!(std::isalnum(c) || ch == '_' || ch == ':')) {
      return false;
    }
  }
  return true;
}

void split_path_symbol(const std::string& target, std::string* path, std::string* symbol) {
  if (path == nullptr || symbol == nullptr) {
    return;
  }
  const auto slash = target.find_last_of("/\\");
  const auto colon = target.rfind(':');
  if (colon != std::string::npos && (slash == std::string::npos || colon > slash)) {
    *path = target.substr(0, colon);
    *symbol = target.substr(colon + 1);
    return;
  }
  *path = target;
  *symbol = {};
}

std::string read_abs(const std::string& abs) {
  std::ifstream in(abs, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

std::string line_at(const std::string& source, int line) {
  if (line <= 0) {
    return {};
  }
  int cur = 1;
  std::size_t i = 0;
  while (i < source.size() && cur < line) {
    if (source[i] == '\n') {
      ++cur;
    }
    ++i;
  }
  if (cur != line) {
    return {};
  }
  const auto end = source.find('\n', i);
  std::string row =
      end == std::string::npos ? source.substr(i) : source.substr(i, end - i);
  return trim_copy(std::move(row));
}

std::string rel_of(const std::string& root, const fs::path& p) {
  std::error_code ec;
  fs::path rel = fs::relative(p, fs::path(root), ec);
  if (ec) {
    return p.generic_string();
  }
  return rel.generic_string();
}

bool fn_has_live_card(EffectRegistry* r, const std::string& path, const std::string& symbol) {
  if (r == nullptr || symbol.empty()) {
    return false;
  }
  const std::string id = registry_canonical_fn_id(r->workspace_root, path, symbol);
  if (id.empty()) {
    return false;
  }
  RegistryNodeRow row;
  std::string err;
  if (!registry_get(r, id, &row, &err)) {
    return false;
  }
  return row.tombstone_reason.empty() && !row.card_json.empty();
}

bool all_have_live_cards(EffectRegistry* r, const std::vector<CercaWakeFn>& fns) {
  if (r == nullptr || fns.empty()) {
    return false;
  }
  for (const auto& fn : fns) {
    if (!fn_has_live_card(r, fn.path, fn.symbol)) {
      return false;
    }
  }
  return true;
}

void collect_source_files(const std::string& root, const std::string& prefix, bool allow_fixtures,
                          std::vector<std::string>* out) {
  if (out == nullptr || root.empty() || prefix.empty()) {
    return;
  }
  std::error_code ec;
  const fs::path base = fs::path(root) / prefix;
  if (fs::is_regular_file(base, ec)) {
    if (is_source_ext(base.extension().string()) &&
        registry_admit_path(prefix, allow_fixtures)) {
      out->push_back(prefix);
    }
    return;
  }
  if (!fs::is_directory(base, ec)) {
    return;
  }
  std::vector<std::string> impl;
  std::vector<std::string> hdr;
  for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec),
       end;
       it != end && !ec; it.increment(ec)) {
    if (static_cast<int>(impl.size() + hdr.size()) >= kCercaWakeMaxFiles) {
      break;
    }
    const fs::path p = it->path();
    const std::string name = p.filename().string();
    if (name == ".tuide" || name == "third_party" || name == ".git") {
      it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file(ec)) {
      continue;
    }
    const std::string rel = rel_of(root, p);
    if (!registry_admit_path(rel, allow_fixtures)) {
      continue;
    }
    const std::string ext = p.extension().string();
    if (is_impl_ext(ext)) {
      impl.push_back(rel);
    } else if (is_source_ext(ext)) {
      hdr.push_back(rel);
    }
  }
  std::unordered_set<std::string> have_impl;
  for (const auto& p : impl) {
    have_impl.insert(registry_path_to_cpp(p));
    out->push_back(p);
  }
  for (const auto& p : hdr) {
    const std::string cpp = registry_path_to_cpp(p);
    if (have_impl.count(cpp)) {
      continue;
    }
    out->push_back(p);
    if (static_cast<int>(out->size()) >= kCercaWakeMaxFiles) {
      break;
    }
  }
}

std::vector<float> embed_one(bool is_query, const std::string& text, const RegistryEmbedFn& embed) {
  std::vector<float> v;
  if (!embed || text.empty()) {
    return v;
  }
  if (!embed(is_query, text, &v) || v.empty()) {
    return {};
  }
  return v;
}

}  // namespace

CercaScopeKind cerca_classify_scope(const std::string& workspace_root, const std::string& scope) {
  const std::string sc = strip_trailing_slash(trim_copy(scope));
  if (sc.empty()) {
    return CercaScopeKind::Skip;
  }
  if (sc.size() <= 4 && (sc[0] == 'M' || sc[0] == 'm') &&
      std::isdigit(static_cast<unsigned char>(sc.back()))) {
    return CercaScopeKind::Skip;
  }
  std::string path;
  std::string symbol;
  split_path_symbol(sc, &path, &symbol);
  if (!symbol.empty() && looks_like_symbol(symbol) && sc.find('/') != std::string::npos) {
    return CercaScopeKind::Skip;
  }
  path = strip_trailing_slash(path.empty() ? sc : path);
  std::error_code ec;
  if (!workspace_root.empty()) {
    const fs::path abs = fs::path(workspace_root) / path;
    if (fs::is_regular_file(abs, ec) && is_source_ext(abs.extension().string())) {
      return CercaScopeKind::File;
    }
    if (fs::is_directory(abs, ec)) {
      return CercaScopeKind::Directory;
    }
  }
  const auto dot = path.rfind('.');
  if (dot != std::string::npos && is_source_ext(path.substr(dot))) {
    return CercaScopeKind::File;
  }
  if (path.find('/') != std::string::npos) {
    return CercaScopeKind::Directory;
  }
  return CercaScopeKind::Skip;
}

std::string cerca_cheap_passage(const std::string& path, const std::string& name,
                                const std::string& signature) {
  std::string out = path;
  if (!name.empty()) {
    out.push_back(' ');
    out += name;
  }
  if (!signature.empty()) {
    out.push_back(' ');
    const std::size_t cap = 120;
    if (signature.size() <= cap) {
      out += signature;
    } else {
      out.append(signature.data(), cap);
    }
  }
  return out;
}

std::vector<CercaWakeFn> cerca_inventory_file(const std::string& workspace_root,
                                              const std::string& rel) {
  std::vector<CercaWakeFn> out;
  if (rel.empty()) {
    return out;
  }
  const std::string abs = (fs::path(workspace_root) / rel).lexically_normal().string();
  const std::string source = read_abs(abs);
  if (source.empty()) {
    return out;
  }
  const TSLanguage* language = tree_sitter_language_for_path(rel);
  if (language == nullptr) {
    language = tree_sitter_cpp_language();
  }
  TSParser* parser = ts_parser_new();
  ts_parser_set_language(parser, language);
  TSTree* tree =
      ts_parser_parse_string(parser, nullptr, source.c_str(), static_cast<uint32_t>(source.size()));
  ts_parser_delete(parser);
  if (tree == nullptr) {
    return out;
  }
  const auto syms = extract_symbols_from_tree(ts_tree_root_node(tree), source, rel);
  ts_tree_delete(tree);
  std::unordered_set<std::string> seen;
  for (const auto& sym : syms) {
    if (sym.kind != SymbolKind::kFunction && sym.kind != SymbolKind::kMethod) {
      continue;
    }
    std::string name = symbol_insert_name(sym.name);
    const auto col = name.rfind("::");
    if (col != std::string::npos) {
      name = name.substr(col + 2);
    }
    if (name.empty() || !seen.insert(name).second) {
      continue;
    }
    CercaWakeFn fn;
    fn.path = rel;
    fn.symbol = std::move(name);
    fn.line = sym.line;
    fn.passage = cerca_cheap_passage(rel, fn.symbol, line_at(source, sym.line));
    out.push_back(std::move(fn));
  }
  return out;
}

std::vector<CercaWakeFn> cerca_rank_cheap(const std::vector<CercaWakeFn>& cands,
                                          const std::string& query, const RegistryEmbedFn& embed,
                                          const RegistryEmbedManyFn& embed_many, int top_k,
                                          int max_per_file, std::string* err) {
  std::vector<CercaWakeFn> out;
  if (cands.empty() || query.empty() || !embed) {
    set_err(err, "cerca wake: rank barato sin query o embed");
    return out;
  }
  const std::vector<float> qvec = embed_one(true, query, embed);
  if (qvec.empty()) {
    set_err(err, "cerca wake: embed query falló");
    return out;
  }
  std::vector<std::vector<float>> pvecs(cands.size());
  bool batch_ok = false;
  if (embed_many) {
    std::vector<std::string> passages;
    passages.reserve(cands.size());
    for (const auto& c : cands) {
      passages.push_back(c.passage.empty() ? (c.path + " " + c.symbol) : c.passage);
    }
    std::vector<std::vector<float>> got;
    batch_ok = embed_many(passages, &got) && got.size() == passages.size();
    if (batch_ok) {
      pvecs = std::move(got);
    }
  }
  if (!batch_ok) {
    for (std::size_t i = 0; i < cands.size(); ++i) {
      const std::string& p =
          cands[i].passage.empty() ? (cands[i].path + " " + cands[i].symbol) : cands[i].passage;
      pvecs[i] = embed_one(false, p, embed);
    }
  }
  struct Scored {
    std::size_t i = 0;
    float cos = 0.f;
  };
  std::vector<Scored> scored;
  scored.reserve(cands.size());
  for (std::size_t i = 0; i < cands.size(); ++i) {
    if (pvecs[i].empty()) {
      continue;
    }
    Scored s;
    s.i = i;
    s.cos = cosine_similarity(qvec, pvecs[i]);
    scored.push_back(s);
  }
  std::sort(scored.begin(), scored.end(),
            [](const Scored& a, const Scored& b) { return a.cos > b.cos; });
  const int k = top_k <= 0 ? kCercaWakeDirTopK : top_k;
  std::unordered_map<std::string, int> file_n;
  for (const auto& s : scored) {
    if (static_cast<int>(out.size()) >= k) {
      break;
    }
    const auto& src = cands[s.i];
    if (max_per_file > 0 && !src.path.empty() && file_n[src.path] >= max_per_file) {
      continue;
    }
    CercaWakeFn fn = src;
    fn.cheap_cos = s.cos;
    if (!src.path.empty()) {
      ++file_n[src.path];
    }
    out.push_back(std::move(fn));
  }
  if (out.empty()) {
    set_err(err, "cerca wake: rank barato sin vectores");
  }
  return out;
}

bool registry_wake_cerca_scopes(EffectRegistry* r, const std::vector<std::string>& in_scopes,
                                const CercaWakeOpts& opts, CercaWakeReport* report,
                                std::string* err) {
  CercaWakeReport local;
  if (report == nullptr) {
    report = &local;
  }
  *report = {};
  if (r == nullptr || r->db == nullptr) {
    set_err(err, "cerca wake: registry cerrado");
    return false;
  }
  const std::string root =
      !r->workspace_root.empty() ? r->workspace_root : opts.deps.workspace_root;
  if (root.empty()) {
    set_err(err, "cerca wake: workspace vacío");
    return false;
  }
  if (opts.query.empty() || !opts.embed) {
    set_err(err, "cerca wake: falta query o embed");
    return false;
  }

  bool allow_fixtures = opts.allow_fixtures;
  std::vector<CercaWakeFn> file_seeds;
  std::vector<CercaWakeFn> dir_cands;
  std::vector<std::string> woken_scopes;
  std::unordered_set<std::string> seen_file;
  std::unordered_set<std::string> seen_fn;

  auto push_unique = [&](std::vector<CercaWakeFn>* dst, CercaWakeFn fn) {
    const std::string key = fn.path + "\t" + fn.symbol;
    if (fn.symbol.empty() || !seen_fn.insert(key).second) {
      return;
    }
    dst->push_back(std::move(fn));
  };

  for (const auto& raw : in_scopes) {
    const std::string sc = strip_trailing_slash(trim_copy(raw));
    if (sc.empty()) {
      continue;
    }
    if (sc.rfind("tests/fixtures/", 0) == 0) {
      allow_fixtures = true;
    }
    const CercaScopeKind kind = cerca_classify_scope(root, sc);
    if (kind == CercaScopeKind::Skip) {
      continue;
    }
    std::string path;
    std::string symbol;
    split_path_symbol(sc, &path, &symbol);
    path = strip_trailing_slash(path.empty() ? sc : path);
    if (kind == CercaScopeKind::File) {
      if (!seen_file.insert(path).second) {
        continue;
      }
      auto inv = cerca_inventory_file(root, path);
      report->files_scanned += 1;
      report->fns_inventoried += static_cast<int>(inv.size());
      if (inv.empty()) {
        continue;
      }
      woken_scopes.push_back(sc);
      if (static_cast<int>(inv.size()) <= kRegistryMaxInventoryPerFile) {
        for (auto& fn : inv) {
          push_unique(&file_seeds, std::move(fn));
        }
      } else {
        std::string rerr;
        auto ranked = cerca_rank_cheap(inv, opts.query, opts.embed, opts.embed_many,
                                       kRegistryMaxInventoryPerFile, 0, &rerr);
        if (ranked.empty() && !rerr.empty()) {
          set_err(err, rerr);
          return false;
        }
        report->cheap_ranked += static_cast<int>(ranked.size());
        for (auto& fn : ranked) {
          push_unique(&file_seeds, std::move(fn));
        }
      }
      continue;
    }
    // Directory / prefix.
    std::vector<std::string> files;
    collect_source_files(root, path, allow_fixtures, &files);
    if (files.empty()) {
      continue;
    }
    woken_scopes.push_back(sc);
    for (const auto& f : files) {
      if (!seen_file.insert(f).second) {
        continue;
      }
      auto inv = cerca_inventory_file(root, f);
      report->files_scanned += 1;
      report->fns_inventoried += static_cast<int>(inv.size());
      for (auto& fn : inv) {
        if (static_cast<int>(dir_cands.size()) >= kCercaWakeMaxFnsCheap) {
          break;
        }
        dir_cands.push_back(std::move(fn));
      }
      if (static_cast<int>(dir_cands.size()) >= kCercaWakeMaxFnsCheap) {
        break;
      }
    }
  }

  std::vector<CercaWakeFn> seeds = std::move(file_seeds);
  if (!dir_cands.empty()) {
    std::vector<CercaWakeFn> picked;
    if (static_cast<int>(dir_cands.size()) <= kCercaWakeDirTopK) {
      picked = dir_cands;
    } else {
      std::string rerr;
      picked = cerca_rank_cheap(dir_cands, opts.query, opts.embed, opts.embed_many,
                                kCercaWakeDirTopK, kCercaWakeDirMaxPerFile, &rerr);
      if (picked.empty() && !rerr.empty()) {
        set_err(err, rerr);
        return false;
      }
      report->cheap_ranked += static_cast<int>(picked.size());
    }
    for (auto& fn : picked) {
      push_unique(&seeds, std::move(fn));
    }
  }

  if (seeds.empty()) {
    report->note = "wake=0";
    return true;
  }
  if (all_have_live_cards(r, seeds)) {
    report->skipped_already_awake = true;
    report->seeded = static_cast<int>(seeds.size());
    report->note = "wake=cached seeds=" + std::to_string(seeds.size());
    return true;
  }

  EffectSliceSeedIn in;
  in.query = opts.query;
  in.add_siblings = !dir_cands.empty();
  in.window_n = std::max(static_cast<int>(seeds.size()), 1);
  std::unordered_set<std::string> inv_paths;
  for (const auto& fn : seeds) {
    EffectSliceSeedFn sf;
    sf.path = fn.path;
    sf.symbol = fn.symbol;
    sf.line = fn.line;
    sf.prior_sem = fn.cheap_cos >= 0.f ? std::max(0.1f, fn.cheap_cos) : 0.8f;
    in.map_window.push_back(std::move(sf));
    in.seeds.push_back(fn.path + ":" + fn.symbol);
    if (inv_paths.insert(fn.path).second) {
      in.inventory_paths.push_back(fn.path);
    }
  }

  EffectSlice sl;
  std::string serr;
  if (!effect_slice_seed(&sl, in, &serr)) {
    set_err(err, serr.empty() ? "cerca wake: seed vacío" : serr);
    return false;
  }
  EffectSliceDeps deps = opts.deps;
  if (deps.workspace_root.empty()) {
    deps.workspace_root = root;
  }
  if (!effect_slice_build(&sl, deps, &serr)) {
    set_err(err, serr.empty() ? "cerca wake: oleada falló" : serr);
    return false;
  }

  RegistryIngestMeta meta;
  meta.query = opts.query;
  meta.seeds = in.seeds;
  meta.allow_fixtures = allow_fixtures;
  if (!registry_ingest_slice(r, sl, meta, &serr)) {
    set_err(err, serr.empty() ? "cerca wake: ingest falló" : serr);
    return false;
  }
  report->ingested = static_cast<int>(sl.nodes.size());
  report->seeded = static_cast<int>(seeds.size());
  report->woken = std::move(woken_scopes);

  RegistryEmbedOpts eopts;
  eopts.model = opts.model.empty() ? kRegistryEmbedModelDefault : opts.model;
  eopts.match_surface = RegistryMatchSurface::CardFull;
  eopts.skip_glue = true;
  RegistryEmbedReport erep;
  if (!registry_embed_nodes(r, opts.embed, opts.embed_many, eopts, &erep, &serr)) {
    set_err(err, serr.empty() ? "cerca wake: embed CardFull falló" : serr);
    return false;
  }
  report->embedded = erep.embedded;
  std::ostringstream note;
  note << "wake=" << report->woken.size() << " files=" << report->files_scanned
       << " inv=" << report->fns_inventoried << " cheap=" << report->cheap_ranked
       << " seed=" << report->seeded << " embed=" << report->embedded;
  report->note = note.str();
  return true;
}

}  // namespace tuide
