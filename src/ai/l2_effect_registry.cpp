#include "ai/l2_effect_registry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sqlite3.h>

#include "ai/l2_effect_summary.hpp"
#include "ai/l2_explore_a.hpp"
#include "ai/vector_math.hpp"
#include "parser/tree_sitter_language.hpp"
#include "parser/tree_sitter_symbols.hpp"
#include "symbols/symbol_utils.hpp"

extern "C" {
#include <tree_sitter/api.h>
}

namespace tuide {
namespace {

namespace fs = std::filesystem;

std::string stem_of_node_id(const std::string& id) {
  if (id.rfind("latch:", 0) == 0) {
    std::string rest = id.substr(6);
    const auto c = rest.find(':');
    return c == std::string::npos ? rest : rest.substr(0, c);
  }
  std::string path;
  if (id.rfind("fn:", 0) == 0) {
    const std::string rest = id.substr(3);
    const auto c = rest.rfind(':');
    path = c == std::string::npos ? rest : rest.substr(0, c);
  } else if (id.rfind("ctrl:", 0) == 0) {
    const std::string rest = id.substr(5);
    std::vector<std::string> parts;
    std::string cur;
    for (char ch : rest) {
      if (ch == ':') {
        parts.push_back(cur);
        cur.clear();
      } else {
        cur.push_back(ch);
      }
    }
    if (!cur.empty()) {
      parts.push_back(cur);
    }
    if (parts.size() >= 3) {
      path.clear();
      for (std::size_t i = 0; i + 2 < parts.size(); ++i) {
        if (i) {
          path += ":";
        }
        path += parts[i];
      }
    } else if (!parts.empty()) {
      path = parts.front();
    }
  }
  return registry_stem_of(path);
}

std::string fnv1a_hex(const std::string& s) {
  std::uint32_t h = 2166136261u;
  for (unsigned char c : s) {
    h ^= c;
    h *= 16777619u;
  }
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%08x", h);
  return buf;
}

bool noise_member(const std::string& m) {
  if (m.size() < 3) {
    return true;
  }
  if (m[0] >= 'A' && m[0] <= 'Z') {
    return true;
  }
  static const char* kSkip[] = {"this", "self",  "tmp", "end",  "begin", "empty", "size",
                                "load", "store", "lock", "unlock", "count", "data", "get",
                                "set",  "layout", "state", "mutex", "order", "acquire"};
  for (const char* x : kSkip) {
    if (m == x) {
      return true;
    }
  }
  return m.find("memory_order") != std::string::npos;
}

bool skip_call_symbol(const std::string& name) {
  if (name.size() < 3) {
    return true;
  }
  static const char* kSkip[] = {
      "sizeof", "push_back", "emplace_back", "insert", "erase",  "find",
      "begin",  "end",       "size",         "empty",  "move",   "forward",
      "make_shared", "make_unique", "to_string", "substr", "append"};
  for (const char* x : kSkip) {
    if (name == x) {
      return true;
    }
  }
  return false;
}

bool likely_lifecycle_symbol(const std::string& name) {
  if (name.empty()) {
    return false;
  }
  auto has = [&](const char* p) { return name.find(p) != std::string::npos; };
  return name == "start" || name == "stop" || name == "open" || name == "close" ||
         has("restart") || has("spawn") || has("shutdown") || has("crash") || has("fail");
}

std::string bare_member(const std::string& path) {
  std::string s = path;
  const auto arrow = s.rfind("->");
  if (arrow != std::string::npos) {
    s = s.substr(arrow + 2);
  }
  const auto dot = s.rfind('.');
  if (dot != std::string::npos) {
    s = s.substr(dot + 1);
  }
  return s;
}

std::string read_abs(const std::string& abs) {
  std::ifstream in(abs);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void set_err(std::string* err, const std::string& m) {
  if (err) {
    *err = m;
  }
}

bool sql_exec(sqlite3* db, const char* sql, std::string* err) {
  char* msg = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &msg) != SQLITE_OK) {
    set_err(err, msg ? msg : "sql");
    sqlite3_free(msg);
    return false;
  }
  return true;
}

int64_t sql_int(sqlite3* db, const char* sql) {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
    return 0;
  }
  int64_t v = 0;
  if (sqlite3_step(st) == SQLITE_ROW) {
    v = sqlite3_column_int64(st, 0);
  }
  sqlite3_finalize(st);
  return v;
}

const char kSchema[] = R"SQL(
PRAGMA journal_mode=WAL;
PRAGMA busy_timeout=5000;
CREATE TABLE IF NOT EXISTS meta (
  k TEXT PRIMARY KEY,
  v TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS files (
  path TEXT PRIMARY KEY,
  mtime INTEGER NOT NULL DEFAULT 0,
  content_hash TEXT NOT NULL DEFAULT '',
  pending_inventory INTEGER NOT NULL DEFAULT 0,
  last_query_id INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS nodes (
  id TEXT PRIMARY KEY,
  kind TEXT NOT NULL,
  path TEXT NOT NULL DEFAULT '',
  symbol TEXT NOT NULL DEFAULT '',
  stem TEXT NOT NULL DEFAULT '',
  line INTEGER NOT NULL DEFAULT 0,
  parent_fn TEXT NOT NULL DEFAULT '',
  ctrl_kind TEXT NOT NULL DEFAULT '',
  cond TEXT NOT NULL DEFAULT '',
  origin TEXT NOT NULL DEFAULT '',
  cold INTEGER NOT NULL DEFAULT 0,
  card_json TEXT NOT NULL DEFAULT '',
  card_hash TEXT NOT NULL DEFAULT '',
  first_query_id INTEGER NOT NULL DEFAULT 0,
  last_query_id INTEGER NOT NULL DEFAULT 0,
  seen_n INTEGER NOT NULL DEFAULT 0,
  tombstone_reason TEXT NOT NULL DEFAULT '',
  tombstone_ts INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS aliases (
  alias_id TEXT PRIMARY KEY,
  canonical_id TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS facts (
  from_id TEXT NOT NULL,
  to_id TEXT NOT NULL,
  kind TEXT NOT NULL,
  member TEXT NOT NULL DEFAULT '',
  UNIQUE(from_id, to_id, kind, member)
);
CREATE TABLE IF NOT EXISTS queries (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts INTEGER NOT NULL,
  text TEXT NOT NULL DEFAULT '',
  seeds TEXT NOT NULL DEFAULT '',
  map_path TEXT NOT NULL DEFAULT '',
  nodes_in INTEGER NOT NULL DEFAULT 0,
  facts_in INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_nodes_path ON nodes(path);
CREATE INDEX IF NOT EXISTS idx_nodes_stem ON nodes(stem);
CREATE INDEX IF NOT EXISTS idx_facts_to ON facts(to_id);
CREATE TABLE IF NOT EXISTS embeddings (
  node_id TEXT NOT NULL,
  model TEXT NOT NULL,
  dim INTEGER NOT NULL,
  card_hash TEXT NOT NULL DEFAULT '',
  blob BLOB NOT NULL,
  PRIMARY KEY(node_id, model)
);
)SQL";

bool file_exists_rel(const std::string& root, const std::string& rel) {
  if (rel.empty()) {
    return false;
  }
  std::error_code ec;
  return fs::exists(fs::path(root) / rel, ec);
}

std::vector<std::pair<std::string, int>> list_file_fns(const std::string& workspace_root,
                                                       const std::string& rel) {
  std::vector<std::pair<std::string, int>> out;
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
    out.push_back({name, sym.line});
  }
  return out;
}

std::string origin_of(const EffectNode& n, const EffectSlice& slice) {
  if (n.seed) {
    return "seed_map";
  }
  if (!n.path.empty() && slice.inventory_paths.count(n.path)) {
    return "inventory";
  }
  for (const auto& f : slice.facts) {
    if (f.kind != EffectFactKind::Call) {
      continue;
    }
    if (f.from == n.id) {
      const EffectNode* to = effect_slice_find_node(slice, f.to);
      if (to && to->seed) {
        return "hop_up";
      }
    }
    if (f.to == n.id) {
      const EffectNode* from = effect_slice_find_node(slice, f.from);
      if (from && from->seed) {
        return "hop_down";
      }
    }
  }
  return "hop_down";
}

RegistryNodeRow row_from_stmt(sqlite3_stmt* st) {
  RegistryNodeRow n;
  auto col = [&](int i) -> std::string {
    const unsigned char* p = sqlite3_column_text(st, i);
    return p ? reinterpret_cast<const char*>(p) : "";
  };
  n.id = col(0);
  n.kind = col(1);
  n.path = col(2);
  n.symbol = col(3);
  n.stem = col(4);
  n.line = sqlite3_column_int(st, 5);
  n.parent_fn = col(6);
  n.ctrl_kind = col(7);
  n.cond = col(8);
  n.origin = col(9);
  n.cold = sqlite3_column_int(st, 10) != 0;
  n.card_json = col(11);
  n.card_hash = col(12);
  n.seen_n = sqlite3_column_int(st, 13);
  n.tombstone_reason = col(14);
  return n;
}

const char kNodeSelect[] =
    "SELECT id,kind,path,symbol,stem,line,parent_fn,ctrl_kind,cond,origin,cold,"
    "card_json,card_hash,seen_n,tombstone_reason FROM nodes WHERE id=?1";

bool load_node(sqlite3* db, const std::string& id, RegistryNodeRow* out) {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, kNodeSelect, -1, &st, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  const bool ok = sqlite3_step(st) == SQLITE_ROW;
  if (ok && out) {
    *out = row_from_stmt(st);
  }
  sqlite3_finalize(st);
  return ok;
}

std::string resolve_id(sqlite3* db, const std::string& id) {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT canonical_id FROM aliases WHERE alias_id=?1", -1, &st,
                         nullptr) != SQLITE_OK) {
    return id;
  }
  sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  std::string out = id;
  if (sqlite3_step(st) == SQLITE_ROW) {
    const unsigned char* p = sqlite3_column_text(st, 0);
    if (p) {
      out = reinterpret_cast<const char*>(p);
    }
  }
  sqlite3_finalize(st);
  return out;
}

}  // namespace

std::string registry_db_path(const std::string& workspace_root) {
  return (fs::path(workspace_root) / ".tuide" / "effect" / "registry.sqlite").string();
}

bool registry_path_is_header(const std::string& path) {
  if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".hpp") == 0) {
    return true;
  }
  return path.size() >= 2 && path.compare(path.size() - 2, 2, ".h") == 0;
}

std::string registry_path_to_cpp(const std::string& path) {
  if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".hpp") == 0) {
    return path.substr(0, path.size() - 4) + ".cpp";
  }
  if (path.size() >= 2 && path.compare(path.size() - 2, 2, ".h") == 0) {
    return path.substr(0, path.size() - 2) + ".cpp";
  }
  return path;
}

std::string registry_stem_of(const std::string& path) {
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

std::string registry_canonical_fn_id(const std::string& workspace_root, const std::string& path,
                                     const std::string& symbol) {
  if (symbol.empty()) {
    return {};
  }
  std::string rel = path;
  if (registry_path_is_header(rel)) {
    const std::string cpp = registry_path_to_cpp(rel);
    if (file_exists_rel(workspace_root, cpp)) {
      rel = cpp;
    }
  }
  return "fn:" + rel + ":" + symbol;
}

std::string registry_canonical_latch_id(const std::string& stem, const std::string& member) {
  if (stem.empty() || member.empty() || noise_member(member)) {
    return {};
  }
  return "latch:" + stem + ":" + member;
}

std::string registry_canonical_node_id(const std::string& workspace_root, const EffectNode& n) {
  if (n.kind == EffectNodeKind::Fn) {
    return registry_canonical_fn_id(workspace_root, n.path, n.symbol);
  }
  if (n.kind == EffectNodeKind::Latch) {
    std::string stem = n.stem;
    std::string member = n.symbol;
    if (member.rfind("latch:", 0) == 0) {
      member = member.substr(6);
    }
    const auto colon = member.rfind(':');
    if (colon != std::string::npos && member.find('/') == std::string::npos) {
      // already stem:member
      stem = member.substr(0, colon);
      member = member.substr(colon + 1);
    }
    return registry_canonical_latch_id(stem, member);
  }
  if (n.kind == EffectNodeKind::Ctrl) {
    return "ctrl:" + n.path + ":" + std::to_string(n.line) + ":" +
           effect_ctrl_kind_name(n.ctrl_kind);
  }
  return "handoff:" + n.path + ":" + std::to_string(n.line);
}

bool registry_admit_path(const std::string& rel, bool allow_fixtures) {
  if (rel.empty()) {
    return true;
  }
  if (rel.find("third_party/") != std::string::npos) {
    return false;
  }
  if (rel.rfind("tools/", 0) == 0 || rel.rfind("examples/", 0) == 0 || rel.rfind("docs/", 0) == 0) {
    return false;
  }
  if (rel.rfind("tests/fixtures/", 0) == 0) {
    return allow_fixtures;
  }
  if (rel.rfind("tests/", 0) == 0) {
    return false;
  }
  return rel.rfind("src/", 0) == 0;
}

bool registry_open(const std::string& workspace_root, EffectRegistry* out, std::string* err) {
  if (out == nullptr || workspace_root.empty()) {
    set_err(err, "registry_open: args");
    return false;
  }
  registry_close(out);
  const fs::path dir = fs::path(workspace_root) / ".tuide" / "effect";
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    set_err(err, "no se pudo crear " + dir.string());
    return false;
  }
  out->workspace_root = workspace_root;
  out->db_path = registry_db_path(workspace_root);
  sqlite3* db = nullptr;
  if (sqlite3_open(out->db_path.c_str(), &db) != SQLITE_OK) {
    set_err(err, sqlite3_errmsg(db));
    sqlite3_close(db);
    return false;
  }
  out->db = db;
  if (!sql_exec(db, kSchema, err)) {
    registry_close(out);
    return false;
  }
  const std::int64_t ver = sql_int(db, "PRAGMA user_version");
  if (ver < 2) {
    if (!sql_exec(db,
                  "CREATE TABLE IF NOT EXISTS embeddings ("
                  "node_id TEXT NOT NULL, model TEXT NOT NULL, dim INTEGER NOT NULL, "
                  "card_hash TEXT NOT NULL DEFAULT '', blob BLOB NOT NULL, "
                  "PRIMARY KEY(node_id, model));",
                  err)) {
      registry_close(out);
      return false;
    }
  }
  sqlite3_exec(db, ("PRAGMA user_version=" + std::to_string(kRegistrySchemaVersion) + ";").c_str(),
               nullptr, nullptr, nullptr);
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO meta(k,v) VALUES('schema_version',?1)", -1, &st,
                         nullptr) == SQLITE_OK) {
    sqlite3_bind_text(st, 1, std::to_string(kRegistrySchemaVersion).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
  return true;
}

void registry_close(EffectRegistry* r) {
  if (r == nullptr) {
    return;
  }
  if (r->db != nullptr) {
    sqlite3_close(r->db);
    r->db = nullptr;
  }
}

bool registry_ingest_slice(EffectRegistry* r, const EffectSlice& slice, const RegistryIngestMeta& meta,
                           std::string* err) {
  if (r == nullptr || r->db == nullptr) {
    set_err(err, "registry cerrado");
    return false;
  }

  std::unordered_map<std::string, std::string> id_map;  // old slice id → canonical
  std::unordered_map<std::string, std::string> fn_stem;  // canonical fn id → stem
  std::unordered_map<std::string, int> inv_count;
  for (const auto& n : slice.nodes) {
    if (n.kind == EffectNodeKind::Fn && !n.seed &&
        slice.inventory_paths.count(n.path)) {
      ++inv_count[n.path];
    }
  }
  std::unordered_set<std::string> pending_files;
  for (const auto& [p, c] : inv_count) {
    if (c > kRegistryMaxInventoryPerFile) {
      pending_files.insert(p);
    }
  }

  std::unordered_set<std::string> linked_fn;
  for (const auto& f : slice.facts) {
    linked_fn.insert(f.from);
    linked_fn.insert(f.to);
  }
  for (const auto& n : slice.nodes) {
    if ((n.kind == EffectNodeKind::Ctrl || n.kind == EffectNodeKind::Handoff) &&
        !n.parent_fn.empty()) {
      linked_fn.insert(n.parent_fn);
    }
  }
  auto fn_priority = [&](const EffectNode& n) {
    int p = 0;
    if (n.seed) {
      p += 3000;
    }
    if (linked_fn.count(n.id) || linked_fn.count("fn:" + n.path + ":" + n.symbol)) {
      p += 1000;
    }
    if (likely_lifecycle_symbol(n.symbol)) {
      p += 100;
    }
    return p;
  };
  std::unordered_set<std::string> admit_inventory_id;
  for (const auto& path : pending_files) {
    std::vector<const EffectNode*> cands;
    for (const auto& n : slice.nodes) {
      if (n.kind == EffectNodeKind::Fn && !n.seed && n.path == path &&
          slice.inventory_paths.count(n.path)) {
        cands.push_back(&n);
      }
    }
    std::sort(cands.begin(), cands.end(), [&](const EffectNode* a, const EffectNode* b) {
      const int pa = fn_priority(*a);
      const int pb = fn_priority(*b);
      if (pa != pb) {
        return pa > pb;
      }
      if (a->line != b->line) {
        return a->line < b->line;
      }
      return a->id < b->id;
    });
    const int take = std::min(kRegistryMaxInventoryPerFile, static_cast<int>(cands.size()));
    for (int i = 0; i < take; ++i) {
      admit_inventory_id.insert(cands[static_cast<std::size_t>(i)]->id);
    }
  }

  int new_fn_budget = kRegistryMaxNewFnPerWave;
  struct AdmitNode {
    EffectNode n;
    std::string cid;
    std::string origin;
    std::string card_json;
    std::string card_hash;
    std::string alias_from;
    int priority = 0;
  };
  std::vector<AdmitNode> nodes;
  nodes.reserve(slice.nodes.size());

  auto admit_fn = [&](const EffectNode& n) -> bool {
    if (!registry_admit_path(n.path, meta.allow_fixtures)) {
      return false;
    }
    if (n.seed) {
      return true;
    }
    if (pending_files.count(n.path) && slice.inventory_paths.count(n.path)) {
      return admit_inventory_id.count(n.id) > 0;
    }
    return true;
  };

  for (const auto& n : slice.nodes) {
    if (n.kind == EffectNodeKind::Fn) {
      if (!admit_fn(n)) {
        continue;
      }
      const std::string cid = registry_canonical_fn_id(r->workspace_root, n.path, n.symbol);
      if (cid.empty()) {
        continue;
      }
      const std::string orig_id = n.id.empty() ? cid : n.id;
      id_map[orig_id] = cid;
      if (orig_id != cid) {
        id_map[n.id] = cid;
      }
      fn_stem[cid] = n.stem.empty() ? registry_stem_of(n.path) : n.stem;
      AdmitNode a;
      a.n = n;
      a.cid = cid;
      a.origin = origin_of(n, slice);
      a.priority = fn_priority(n);
      if (registry_path_is_header(n.path)) {
        const std::string cpp = registry_path_to_cpp(n.path);
        if (file_exists_rel(r->workspace_root, cpp)) {
          a.alias_from = "fn:" + n.path + ":" + n.symbol;
          a.n.path = cpp;
        }
      }
      nodes.push_back(std::move(a));
    }
  }
  std::sort(nodes.begin(), nodes.end(), [](const AdmitNode& a, const AdmitNode& b) {
    if (a.priority != b.priority) {
      return a.priority > b.priority;
    }
    return a.cid < b.cid;
  });
  std::unordered_set<std::string> admitted_fn_cids;
  for (const auto& a : nodes) {
    if (a.n.kind == EffectNodeKind::Fn) {
      admitted_fn_cids.insert(a.cid);
    }
  }

  // Latch: namespace by writer/reader stem (split global slice latches).
  std::unordered_map<std::string, std::unordered_set<std::string>> latch_stems;  // member → stems
  auto latch_member = [](const EffectNode& n) {
    std::string m = n.symbol;
    if (m.rfind("latch:", 0) == 0) {
      m = m.substr(6);
    }
    const auto c = m.rfind(':');
    if (c != std::string::npos && m.find('/') == std::string::npos) {
      m = m.substr(c + 1);
    }
    return bare_member(m);
  };
  for (const auto& f : slice.facts) {
    if (f.kind != EffectFactKind::Write && f.kind != EffectFactKind::Read) {
      continue;
    }
    const EffectNode* from = effect_slice_find_node(slice, f.from);
    const EffectNode* to = effect_slice_find_node(slice, f.to);
    const EffectNode* latch = nullptr;
    const EffectNode* fn = nullptr;
    if (to && to->kind == EffectNodeKind::Latch) {
      latch = to;
      fn = from;
    } else if (from && from->kind == EffectNodeKind::Latch) {
      latch = from;
      fn = to;
    }
    if (latch == nullptr || fn == nullptr || fn->kind != EffectNodeKind::Fn) {
      continue;
    }
    const std::string mem = f.member.empty() ? latch_member(*latch) : bare_member(f.member);
    const std::string st = fn->stem.empty() ? registry_stem_of(fn->path) : fn->stem;
    if (!mem.empty() && !st.empty() && !noise_member(mem)) {
      latch_stems[mem].insert(st);
    }
  }
  for (const auto& n : slice.nodes) {
    if (n.kind != EffectNodeKind::Latch) {
      continue;
    }
    const std::string mem = latch_member(n);
    auto it = latch_stems.find(mem);
    if (it == latch_stems.end()) {
      continue;
    }
    for (const auto& st : it->second) {
      const std::string cid = registry_canonical_latch_id(st, mem);
      if (cid.empty()) {
        continue;
      }
      AdmitNode a;
      a.n = n;
      a.n.stem = st;
      a.n.symbol = mem;
      a.cid = cid;
      a.origin = "inventory";
      nodes.push_back(std::move(a));
    }
  }

  for (const auto& n : slice.nodes) {
    if (n.kind != EffectNodeKind::Ctrl && n.kind != EffectNodeKind::Handoff) {
      continue;
    }
    if (!registry_admit_path(n.path, meta.allow_fixtures)) {
      continue;
    }
    std::string cid = registry_canonical_node_id(r->workspace_root, n);
    if (cid.empty()) {
      continue;
    }
    AdmitNode a;
    a.n = n;
    a.cid = cid;
    a.origin = origin_of(n, slice);
    if (!n.parent_fn.empty()) {
      auto pit = id_map.find(n.parent_fn);
      if (pit != id_map.end()) {
        a.n.parent_fn = pit->second;
      } else {
        const EffectNode* p = effect_slice_find_node(slice, n.parent_fn);
        if (p) {
          a.n.parent_fn = registry_canonical_fn_id(r->workspace_root, p->path, p->symbol);
        }
      }
    }
    if (n.kind == EffectNodeKind::Ctrl &&
        (a.n.parent_fn.empty() || admitted_fn_cids.count(a.n.parent_fn) == 0)) {
      continue;
    }
    id_map[n.id] = cid;
    nodes.push_back(std::move(a));
  }

  auto map_id = [&](const std::string& old) -> std::string {
    auto it = id_map.find(old);
    if (it != id_map.end()) {
      return it->second;
    }
    const EffectNode* n = effect_slice_find_node(slice, old);
    if (n == nullptr) {
      return {};
    }
    if (n->kind == EffectNodeKind::Latch) {
      return {};  // facts rewrite latches below
    }
    return registry_canonical_node_id(r->workspace_root, *n);
  };

  struct AdmitFact {
    std::string from;
    std::string to;
    std::string kind;
    std::string member;
  };
  std::vector<AdmitFact> facts;
  for (const auto& f : slice.facts) {
    const EffectNode* from = effect_slice_find_node(slice, f.from);
    const EffectNode* to = effect_slice_find_node(slice, f.to);
    std::string a = map_id(f.from);
    std::string b = map_id(f.to);
    std::string member = f.member;
    if (to && to->kind == EffectNodeKind::Latch) {
      const std::string mem = member.empty() ? latch_member(*to) : bare_member(member);
      std::string st;
      if (from && from->kind == EffectNodeKind::Fn) {
        st = from->stem.empty() ? registry_stem_of(from->path) : from->stem;
      }
      b = registry_canonical_latch_id(st, mem);
      member = mem;
    }
    if (from && from->kind == EffectNodeKind::Latch) {
      const std::string mem = member.empty() ? latch_member(*from) : bare_member(member);
      std::string st;
      if (to && to->kind == EffectNodeKind::Fn) {
        st = to->stem.empty() ? registry_stem_of(to->path) : to->stem;
      } else if (to) {
        st = to->stem.empty() ? registry_stem_of(to->path) : to->stem;
      }
      a = registry_canonical_latch_id(st, mem);
      member = mem;
    }
    if (a.empty() || b.empty() || a == b) {
      continue;
    }
    if (f.kind == EffectFactKind::Call) {
      const EffectNode* callee = to;
      if (callee && skip_call_symbol(callee->symbol)) {
        continue;
      }
    }
    AdmitFact af;
    af.from = a;
    af.to = b;
    af.kind = effect_fact_kind_name(f.kind);
    af.member = member;
    facts.push_back(std::move(af));
  }

  // Cards for canonical fns (cap new fns).
  sqlite3* db = r->db;
  if (!sql_exec(db, "BEGIN IMMEDIATE;", err)) {
    return false;
  }

  const std::int64_t ts = static_cast<std::int64_t>(std::time(nullptr));
  sqlite3_stmt* qst = nullptr;
  if (sqlite3_prepare_v2(db,
                         "INSERT INTO queries(ts,text,seeds,map_path,nodes_in,facts_in) "
                         "VALUES(?1,?2,?3,?4,?5,?6)",
                         -1, &qst, nullptr) != SQLITE_OK) {
    sql_exec(db, "ROLLBACK;", nullptr);
    set_err(err, sqlite3_errmsg(db));
    return false;
  }
  std::string seeds_csv;
  for (std::size_t i = 0; i < meta.seeds.size(); ++i) {
    if (i) {
      seeds_csv += ",";
    }
    seeds_csv += meta.seeds[i];
  }
  sqlite3_bind_int64(qst, 1, ts);
  sqlite3_bind_text(qst, 2, meta.query.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(qst, 3, seeds_csv.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(qst, 4, meta.map_path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(qst, 5, static_cast<int>(nodes.size()));
  sqlite3_bind_int(qst, 6, static_cast<int>(facts.size()));
  if (sqlite3_step(qst) != SQLITE_DONE) {
    sqlite3_finalize(qst);
    sql_exec(db, "ROLLBACK;", nullptr);
    set_err(err, sqlite3_errmsg(db));
    return false;
  }
  sqlite3_finalize(qst);
  const std::int64_t qid = sqlite3_last_insert_rowid(db);

  auto upsert_file = [&](const std::string& path, bool pending) {
    if (path.empty() || !registry_admit_path(path, meta.allow_fixtures)) {
      return;
    }
    const fs::path abs = fs::path(r->workspace_root) / path;
    std::int64_t mtime = 0;
    std::string chash;
    std::error_code ec;
    if (fs::exists(abs, ec)) {
      mtime = static_cast<std::int64_t>(
          fs::last_write_time(abs, ec).time_since_epoch().count());
      chash = fnv1a_hex(read_abs(abs.string()));
    }
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db,
                       "INSERT INTO files(path,mtime,content_hash,pending_inventory,last_query_id) "
                       "VALUES(?1,?2,?3,?4,?5) ON CONFLICT(path) DO UPDATE SET "
                       "mtime=excluded.mtime, content_hash=excluded.content_hash, "
                       "pending_inventory=excluded.pending_inventory, "
                       "last_query_id=excluded.last_query_id",
                       -1, &st, nullptr);
    sqlite3_bind_text(st, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, mtime);
    sqlite3_bind_text(st, 3, chash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 4, pending ? 1 : 0);
    sqlite3_bind_int64(st, 5, qid);
    sqlite3_step(st);
    sqlite3_finalize(st);
  };

  for (const auto& p : pending_files) {
    upsert_file(p, true);
  }
  for (const auto& n : slice.nodes) {
    if (!n.path.empty()) {
      upsert_file(n.path, pending_files.count(n.path) > 0);
    }
  }

  sqlite3_stmt* nst = nullptr;
  sqlite3_prepare_v2(
      db,
      "INSERT INTO nodes(id,kind,path,symbol,stem,line,parent_fn,ctrl_kind,cond,origin,cold,"
      "card_json,card_hash,first_query_id,last_query_id,seen_n,tombstone_reason,tombstone_ts) "
      "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,0,?11,?12,?13,?13,1,'',0) "
      "ON CONFLICT(id) DO UPDATE SET "
      "line=excluded.line, "
      "parent_fn=CASE WHEN excluded.parent_fn!='' THEN excluded.parent_fn ELSE nodes.parent_fn END, "
      "card_json=CASE WHEN excluded.card_hash!='' AND excluded.card_hash!=nodes.card_hash "
      "THEN excluded.card_json ELSE nodes.card_json END, "
      "card_hash=CASE WHEN excluded.card_hash!='' THEN excluded.card_hash ELSE nodes.card_hash END, "
      "last_query_id=excluded.last_query_id, seen_n=nodes.seen_n+1, "
      "tombstone_reason='', tombstone_ts=0, "
      "path=CASE WHEN excluded.path!='' THEN excluded.path ELSE nodes.path END, "
      "stem=CASE WHEN excluded.stem!='' THEN excluded.stem ELSE nodes.stem END",
      -1, &nst, nullptr);

  sqlite3_stmt* exists = nullptr;
  sqlite3_prepare_v2(db, "SELECT 1 FROM nodes WHERE id=?1", -1, &exists, nullptr);

  int inserted_fn = 0;
  for (auto& a : nodes) {
    if (a.n.kind == EffectNodeKind::Fn) {
      sqlite3_reset(exists);
      sqlite3_bind_text(exists, 1, a.cid.c_str(), -1, SQLITE_TRANSIENT);
      const bool already = sqlite3_step(exists) == SQLITE_ROW;
      if (!already) {
        if (inserted_fn >= new_fn_budget) {
          admitted_fn_cids.erase(a.cid);
          continue;
        }
        ++inserted_fn;
      }
      if (a.card_json.empty()) {
        const std::string abs =
            (fs::path(r->workspace_root) / (a.n.path.empty() ? "" : a.n.path)).string();
        EffectSummaryOpts opts;
        opts.seeds = meta.seeds;
        opts.query = meta.query;
        opts.hint_line = a.n.line;
        const EffectSummary es =
            effect_summary_build(abs, a.n.path, a.n.symbol, "", opts);
        a.card_json = effect_summary_to_json(es).dump();
        a.card_hash = fnv1a_hex(a.card_json);
      }
    }
    if (a.n.kind == EffectNodeKind::Ctrl) {
      if (a.n.parent_fn.empty()) {
        continue;
      }
      sqlite3_reset(exists);
      sqlite3_bind_text(exists, 1, a.n.parent_fn.c_str(), -1, SQLITE_TRANSIENT);
      if (sqlite3_step(exists) != SQLITE_ROW) {
        continue;
      }
    }
    sqlite3_reset(nst);
    sqlite3_bind_text(nst, 1, a.cid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(nst, 2, effect_node_kind_name(a.n.kind), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(nst, 3, a.n.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(nst, 4, a.n.symbol.c_str(), -1, SQLITE_TRANSIENT);
    const std::string stem = a.n.stem.empty() ? registry_stem_of(a.n.path) : a.n.stem;
    sqlite3_bind_text(nst, 5, stem.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(nst, 6, a.n.line);
    sqlite3_bind_text(nst, 7, a.n.parent_fn.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(nst, 8, effect_ctrl_kind_name(a.n.ctrl_kind), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(nst, 9, a.n.cond.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(nst, 10, a.origin.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(nst, 11, a.card_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(nst, 12, a.card_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(nst, 13, qid);
    sqlite3_step(nst);
    if (!a.alias_from.empty() && a.alias_from != a.cid) {
      sqlite3_stmt* al = nullptr;
      sqlite3_prepare_v2(db,
                         "INSERT OR REPLACE INTO aliases(alias_id,canonical_id) VALUES(?1,?2)", -1,
                         &al, nullptr);
      sqlite3_bind_text(al, 1, a.alias_from.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(al, 2, a.cid.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_step(al);
      sqlite3_finalize(al);
    }
  }
  sqlite3_finalize(nst);
  sqlite3_finalize(exists);

  sqlite3_stmt* fst = nullptr;
  sqlite3_prepare_v2(db,
                     "INSERT OR IGNORE INTO facts(from_id,to_id,kind,member) VALUES(?1,?2,?3,?4)",
                     -1, &fst, nullptr);
  sqlite3_stmt* alive = nullptr;
  sqlite3_prepare_v2(db, "SELECT 1 FROM nodes WHERE id=?1 AND tombstone_reason=''", -1, &alive,
                     nullptr);
  auto node_alive = [&](const std::string& id) {
    sqlite3_reset(alive);
    sqlite3_bind_text(alive, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    return sqlite3_step(alive) == SQLITE_ROW;
  };
  for (const auto& f : facts) {
    if (!node_alive(f.from) || !node_alive(f.to)) {
      continue;
    }
    sqlite3_reset(fst);
    sqlite3_bind_text(fst, 1, f.from.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(fst, 2, f.to.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(fst, 3, f.kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(fst, 4, f.member.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(fst);
  }
  sqlite3_finalize(fst);
  sqlite3_finalize(alive);

  if (!sql_exec(db, "COMMIT;", err)) {
    sql_exec(db, "ROLLBACK;", nullptr);
    return false;
  }
  return true;
}

bool registry_refresh_path(EffectRegistry* r, const std::string& rel, std::string* err) {
  if (r == nullptr || r->db == nullptr) {
    set_err(err, "registry cerrado");
    return false;
  }
  if (rel.empty()) {
    set_err(err, "path vacío");
    return false;
  }
  const fs::path abs = fs::path(r->workspace_root) / rel;
  std::error_code ec;
  const bool gone_file = !fs::exists(abs, ec);

  sqlite3* db = r->db;
  if (!sql_exec(db, "BEGIN IMMEDIATE;", err)) {
    return false;
  }
  const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));

  std::vector<std::pair<std::string, int>> live;
  if (!gone_file) {
    live = list_file_fns(r->workspace_root, rel);
  }
  std::unordered_set<std::string> live_ids;
  for (const auto& [name, line] : live) {
    live_ids.insert(registry_canonical_fn_id(r->workspace_root, rel, name));
  }

  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db, "SELECT id,symbol FROM nodes WHERE path=?1 AND kind='fn'", -1, &st,
                     nullptr);
  sqlite3_bind_text(st, 1, rel.c_str(), -1, SQLITE_TRANSIENT);
  std::vector<std::string> existing;
  while (sqlite3_step(st) == SQLITE_ROW) {
    const unsigned char* p = sqlite3_column_text(st, 0);
    if (p) {
      existing.push_back(reinterpret_cast<const char*>(p));
    }
  }
  sqlite3_finalize(st);

  sqlite3_stmt* tomb = nullptr;
  sqlite3_prepare_v2(db,
                     "UPDATE nodes SET tombstone_reason='gone', tombstone_ts=?1 "
                     "WHERE id=?2",
                     -1, &tomb, nullptr);
  sqlite3_stmt* delf = nullptr;
  sqlite3_prepare_v2(db, "DELETE FROM facts WHERE from_id=?1 OR to_id=?1", -1, &delf, nullptr);
  for (const auto& id : existing) {
    if (live_ids.count(id)) {
      continue;
    }
    sqlite3_reset(tomb);
    sqlite3_bind_int64(tomb, 1, now);
    sqlite3_bind_text(tomb, 2, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(tomb);
    sqlite3_reset(delf);
    sqlite3_bind_text(delf, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(delf);
  }
  sqlite3_finalize(tomb);
  sqlite3_finalize(delf);

  sqlite3_stmt* ins = nullptr;
  sqlite3_prepare_v2(
      db,
      "INSERT INTO nodes(id,kind,path,symbol,stem,line,origin,first_query_id,last_query_id,seen_n) "
      "VALUES(?1,'fn',?2,?3,?4,?5,'inventory',0,0,1) "
      "ON CONFLICT(id) DO UPDATE SET line=excluded.line, tombstone_reason='', tombstone_ts=0, "
      "seen_n=nodes.seen_n+1",
      -1, &ins, nullptr);
  const std::string stem = registry_stem_of(rel);
  for (const auto& [name, line] : live) {
    const std::string id = registry_canonical_fn_id(r->workspace_root, rel, name);
    sqlite3_reset(ins);
    sqlite3_bind_text(ins, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 2, rel.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 3, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 4, stem.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(ins, 5, line);
    sqlite3_step(ins);
  }
  sqlite3_finalize(ins);

  sqlite3_stmt* uf = nullptr;
  sqlite3_prepare_v2(db,
                     "INSERT INTO files(path,mtime,content_hash,pending_inventory) VALUES(?1,0,'',0) "
                     "ON CONFLICT(path) DO UPDATE SET pending_inventory=0",
                     -1, &uf, nullptr);
  sqlite3_bind_text(uf, 1, rel.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(uf);
  sqlite3_finalize(uf);

  if (!sql_exec(db, "COMMIT;", err)) {
    sql_exec(db, "ROLLBACK;", nullptr);
    return false;
  }
  return true;
}

bool registry_gc(EffectRegistry* r, const RegistryGcOpts& opts, RegistryGcReport* report,
                 std::string* err) {
  if (r == nullptr || r->db == nullptr) {
    set_err(err, "registry cerrado");
    return false;
  }
  RegistryGcReport local;
  if (report == nullptr) {
    report = &local;
  }
  *report = {};
  const std::int64_t qmax = sql_int(r->db, "SELECT COALESCE(MAX(id),0) FROM queries");
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(r->db,
                     "SELECT id FROM nodes WHERE tombstone_reason!='' AND "
                     "(?1 - last_query_id) >= ?2",
                     -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, qmax);
  sqlite3_bind_int(st, 2, opts.min_age_queries);
  std::vector<std::string> ids;
  while (sqlite3_step(st) == SQLITE_ROW) {
    const unsigned char* p = sqlite3_column_text(st, 0);
    if (p) {
      ids.push_back(reinterpret_cast<const char*>(p));
    }
  }
  sqlite3_finalize(st);
  report->tombstones = static_cast<int>(ids.size());
  if (opts.dry_run) {
    return true;
  }
  if (!sql_exec(r->db, "BEGIN IMMEDIATE;", err)) {
    return false;
  }
  sqlite3_stmt* df = nullptr;
  sqlite3_prepare_v2(r->db, "DELETE FROM facts WHERE from_id=?1 OR to_id=?1", -1, &df, nullptr);
  sqlite3_stmt* dn = nullptr;
  sqlite3_prepare_v2(r->db, "DELETE FROM nodes WHERE id=?1", -1, &dn, nullptr);
  sqlite3_stmt* da = nullptr;
  sqlite3_prepare_v2(r->db, "DELETE FROM aliases WHERE alias_id=?1 OR canonical_id=?1", -1, &da,
                     nullptr);
  sqlite3_stmt* de = nullptr;
  sqlite3_prepare_v2(r->db, "DELETE FROM embeddings WHERE node_id=?1", -1, &de, nullptr);
  for (const auto& id : ids) {
    sqlite3_reset(df);
    sqlite3_bind_text(df, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(df);
    report->facts_dropped += sqlite3_changes(r->db);
    sqlite3_reset(da);
    sqlite3_bind_text(da, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(da);
    sqlite3_reset(de);
    sqlite3_bind_text(de, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(de);
    sqlite3_reset(dn);
    sqlite3_bind_text(dn, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(dn);
    ++report->applied;
  }
  sqlite3_finalize(df);
  sqlite3_finalize(dn);
  sqlite3_finalize(da);
  sqlite3_finalize(de);
  if (!sql_exec(r->db, "COMMIT;", err)) {
    sql_exec(r->db, "ROLLBACK;", nullptr);
    return false;
  }
  return true;
}

bool registry_stats(EffectRegistry* r, RegistryStats* out, std::string* err) {
  if (r == nullptr || r->db == nullptr || out == nullptr) {
    set_err(err, "stats args");
    return false;
  }
  *out = {};
  out->queries = static_cast<int>(sql_int(r->db, "SELECT COUNT(*) FROM queries"));
  out->files = static_cast<int>(sql_int(r->db, "SELECT COUNT(*) FROM files"));
  out->pending_inventory =
      static_cast<int>(sql_int(r->db, "SELECT COUNT(*) FROM files WHERE pending_inventory=1"));
  out->nodes =
      static_cast<int>(sql_int(r->db, "SELECT COUNT(*) FROM nodes WHERE tombstone_reason=''"));
  out->fns = static_cast<int>(
      sql_int(r->db, "SELECT COUNT(*) FROM nodes WHERE kind='fn' AND tombstone_reason=''"));
  out->ctrls = static_cast<int>(
      sql_int(r->db, "SELECT COUNT(*) FROM nodes WHERE kind='ctrl' AND tombstone_reason=''"));
  out->latches = static_cast<int>(
      sql_int(r->db, "SELECT COUNT(*) FROM nodes WHERE kind='latch' AND tombstone_reason=''"));
  out->handoffs = static_cast<int>(
      sql_int(r->db, "SELECT COUNT(*) FROM nodes WHERE kind='handoff' AND tombstone_reason=''"));
  out->facts = static_cast<int>(sql_int(r->db, "SELECT COUNT(*) FROM facts"));
  out->tombstones =
      static_cast<int>(sql_int(r->db, "SELECT COUNT(*) FROM nodes WHERE tombstone_reason!=''"));
  out->embeddings = static_cast<int>(sql_int(r->db, "SELECT COUNT(*) FROM embeddings"));
  return true;
}

bool registry_get(EffectRegistry* r, const std::string& id, RegistryNodeRow* out, std::string* err) {
  if (r == nullptr || r->db == nullptr || out == nullptr) {
    set_err(err, "get args");
    return false;
  }
  const std::string cid = resolve_id(r->db, id);
  if (!load_node(r->db, cid, out)) {
    set_err(err, "nodo no encontrado: " + id);
    return false;
  }
  return true;
}

bool registry_neighbors(EffectRegistry* r, const std::string& id,
                        const std::vector<std::string>& kinds, const std::string& dir,
                        std::vector<RegistryNeighbor>* out, std::string* err) {
  if (r == nullptr || r->db == nullptr || out == nullptr) {
    set_err(err, "neighbors args");
    return false;
  }
  out->clear();
  const std::string cid = resolve_id(r->db, id);
  const bool want_out = dir.empty() || dir == "out" || dir == "both";
  const bool want_in = dir == "in" || dir == "both" || dir.empty();
  auto kind_ok = [&](const std::string& k) {
    if (kinds.empty()) {
      return true;
    }
    return std::find(kinds.begin(), kinds.end(), k) != kinds.end();
  };
  auto pull = [&](bool outbound) {
    const char* sql = outbound ? "SELECT from_id,to_id,kind,member FROM facts WHERE from_id=?1"
                               : "SELECT from_id,to_id,kind,member FROM facts WHERE to_id=?1";
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(r->db, sql, -1, &st, nullptr);
    sqlite3_bind_text(st, 1, cid.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
      RegistryNeighbor nb;
      auto col = [&](int i) -> std::string {
        const unsigned char* p = sqlite3_column_text(st, i);
        return p ? reinterpret_cast<const char*>(p) : "";
      };
      nb.fact.from_id = col(0);
      nb.fact.to_id = col(1);
      nb.fact.kind = col(2);
      nb.fact.member = col(3);
      if (!kind_ok(nb.fact.kind)) {
        continue;
      }
      nb.outbound = outbound;
      const std::string other = outbound ? nb.fact.to_id : nb.fact.from_id;
      if (!load_node(r->db, other, &nb.node) || !nb.node.tombstone_reason.empty()) {
        continue;
      }
      out->push_back(std::move(nb));
    }
    sqlite3_finalize(st);
  };
  if (want_out) {
    pull(true);
  }
  if (want_in) {
    pull(false);
  }
  return true;
}

bool registry_path_between(EffectRegistry* r, const std::string& from, const std::string& to,
                           std::vector<std::string>* node_ids, std::string* err) {
  if (r == nullptr || r->db == nullptr || node_ids == nullptr) {
    set_err(err, "path args");
    return false;
  }
  node_ids->clear();
  const std::string src = resolve_id(r->db, from);
  const std::string dst = resolve_id(r->db, to);
  if (src == dst) {
    node_ids->push_back(src);
    return true;
  }
  std::queue<std::string> q;
  std::unordered_map<std::string, std::string> prev;
  q.push(src);
  prev[src] = "";
  bool found = false;
  while (!q.empty()) {
    const std::string cur = q.front();
    q.pop();
    int depth = 0;
    for (std::string x = cur; !x.empty() && prev.count(x); x = prev[x]) {
      ++depth;
    }
    if (depth > kRegistryPathMaxDepth) {
      continue;
    }
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(r->db, "SELECT to_id FROM facts WHERE from_id=?1", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, cur.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
      const unsigned char* p = sqlite3_column_text(st, 0);
      if (!p) {
        continue;
      }
      const std::string nxt(reinterpret_cast<const char*>(p));
      if (prev.count(nxt)) {
        continue;
      }
      prev[nxt] = cur;
      if (nxt == dst) {
        found = true;
        break;
      }
      q.push(nxt);
    }
    sqlite3_finalize(st);
    if (found) {
      break;
    }
  }
  if (!found) {
    set_err(err, "sin camino");
    return false;
  }
  std::vector<std::string> rev;
  for (std::string x = dst; !x.empty(); x = prev[x]) {
    rev.push_back(x);
    if (x == src) {
      break;
    }
  }
  std::reverse(rev.begin(), rev.end());
  *node_ids = std::move(rev);
  return true;
}

bool registry_pending_files(EffectRegistry* r, std::vector<std::string>* out, std::string* err) {
  if (r == nullptr || r->db == nullptr || out == nullptr) {
    set_err(err, "pending args");
    return false;
  }
  out->clear();
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(r->db, "SELECT path FROM files WHERE pending_inventory=1", -1, &st, nullptr);
  while (sqlite3_step(st) == SQLITE_ROW) {
    const unsigned char* p = sqlite3_column_text(st, 0);
    if (p) {
      out->push_back(reinterpret_cast<const char*>(p));
    }
  }
  sqlite3_finalize(st);
  return true;
}

bool registry_list_files(EffectRegistry* r, std::vector<std::pair<std::string, bool>>* out,
                         std::string* err) {
  if (r == nullptr || r->db == nullptr || out == nullptr) {
    set_err(err, "files args");
    return false;
  }
  out->clear();
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(r->db, "SELECT path, pending_inventory FROM files ORDER BY path", -1, &st,
                     nullptr);
  while (sqlite3_step(st) == SQLITE_ROW) {
    const unsigned char* p = sqlite3_column_text(st, 0);
    if (p) {
      out->push_back({reinterpret_cast<const char*>(p), sqlite3_column_int(st, 1) != 0});
    }
  }
  sqlite3_finalize(st);
  return true;
}

namespace {

bool card_is_glue(const std::string& card_json) {
  if (card_json.empty()) {
    return false;
  }
  try {
    const auto j = nlohmann::json::parse(card_json);
    std::string nudge;
    if (j.contains("nudge") && j["nudge"].is_string()) {
      nudge = j["nudge"].get<std::string>();
    }
    if (nudge == "likely_glue" || nudge == "likely_lsp_trap" || nudge == "likely_noise") {
      return true;
    }
    if (j.contains("roles") && j["roles"].is_array()) {
      for (const auto& r : j["roles"]) {
        if (r.is_string() && r.get<std::string>() == "glue") {
          return nudge.empty() || nudge == "likely_glue";
        }
      }
    }
  } catch (...) {
  }
  return false;
}

std::vector<float> blob_to_vec(const void* p, int nbytes) {
  std::vector<float> v;
  if (p == nullptr || nbytes < static_cast<int>(sizeof(float))) {
    return v;
  }
  const std::size_t n = static_cast<std::size_t>(nbytes) / sizeof(float);
  v.resize(n);
  std::memcpy(v.data(), p, n * sizeof(float));
  return v;
}

bool cached_hash(sqlite3* db, const std::string& id, const std::string& model, std::string* hash) {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT card_hash FROM embeddings WHERE node_id=?1 AND model=?2", -1,
                         &st, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, model.c_str(), -1, SQLITE_TRANSIENT);
  bool ok = false;
  if (sqlite3_step(st) == SQLITE_ROW) {
    const unsigned char* p = sqlite3_column_text(st, 0);
    if (hash) {
      *hash = p ? reinterpret_cast<const char*>(p) : "";
    }
    ok = true;
  }
  sqlite3_finalize(st);
  return ok;
}

}  // namespace

// registry_match_surface_* implemented in l2_graph_query_profile.cpp (light TU).

std::string registry_card_passage(const RegistryNodeRow& n) {
  return registry_card_passage(n, RegistryMatchSurface::CardFull);
}

std::string registry_card_passage(const RegistryNodeRow& n, RegistryMatchSurface surface) {
  if (surface == RegistryMatchSurface::Latch) {
    std::string p = "latch";
    if (!n.stem.empty()) {
      p += " " + n.stem;
    }
    if (!n.symbol.empty()) {
      p += " " + n.symbol;
    } else if (!n.id.empty()) {
      p += " " + n.id;
    }
    return p;
  }
  if (surface == RegistryMatchSurface::CardAttrs) {
    std::ostringstream oss;
    if (!n.symbol.empty()) {
      oss << n.symbol << ' ';
    }
    if (!n.stem.empty()) {
      oss << n.stem << ' ';
    }
    if (!n.card_json.empty()) {
      try {
        const auto j = nlohmann::json::parse(n.card_json);
        auto dump_list = [&](const char* key) {
          if (!j.contains(key) || !j[key].is_array()) {
            return;
          }
          for (const auto& it : j[key]) {
            if (it.is_string()) {
              oss << it.get<std::string>() << ' ';
            }
          }
        };
        dump_list("writes");
        dump_list("reads");
        dump_list("hot");
      } catch (...) {
      }
    }
    std::string p = oss.str();
    while (!p.empty() && p.back() == ' ') {
      p.pop_back();
    }
    if (!p.empty()) {
      return p;
    }
  }
  if (surface == RegistryMatchSurface::NodeId) {
    std::string p = n.kind + " " + n.id;
    if (!n.symbol.empty()) {
      p += " " + n.symbol;
    }
    if (!n.path.empty()) {
      p += " " + n.path;
    }
    return p;
  }
  // CardFull
  if (!n.card_json.empty()) {
    try {
      const auto j = nlohmann::json::parse(n.card_json);
      if (j.contains("card") && j["card"].is_string()) {
        const std::string c = j["card"].get<std::string>();
        if (!c.empty()) {
          return c;
        }
      }
    } catch (...) {
    }
  }
  std::string p = n.kind + " " + n.id;
  if (!n.symbol.empty()) {
    p += " " + n.symbol;
  }
  if (!n.path.empty()) {
    p += " " + n.path;
  }
  if (!n.cond.empty()) {
    p += " " + n.cond;
  }
  return p;
}

bool registry_embed_nodes(EffectRegistry* r, const RegistryEmbedFn& embed,
                          const RegistryEmbedManyFn& embed_passages, const RegistryEmbedOpts& opts,
                          RegistryEmbedReport* report, std::string* err) {
  if (r == nullptr || r->db == nullptr) {
    set_err(err, "registry cerrado");
    return false;
  }
  if (!embed && !embed_passages) {
    set_err(err, "falta embedder");
    return false;
  }
  RegistryEmbedReport local;
  if (report == nullptr) {
    report = &local;
  }
  *report = {};

  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(r->db,
                     "SELECT id,kind,path,symbol,stem,line,parent_fn,ctrl_kind,cond,origin,cold,"
                     "card_json,card_hash,seen_n,tombstone_reason FROM nodes WHERE tombstone_reason=''",
                     -1, &st, nullptr);
  struct Job {
    RegistryNodeRow n;
    std::string passage;
  };
  std::vector<Job> jobs;
  while (sqlite3_step(st) == SQLITE_ROW) {
    RegistryNodeRow n = row_from_stmt(st);
    if (opts.skip_glue && card_is_glue(n.card_json)) {
      ++report->skipped_glue;
      continue;
    }
    if (opts.skip_glue && n.kind == "ctrl") {
      ++report->skipped_ctrl;
      continue;
    }
    if (opts.match_surface == RegistryMatchSurface::Latch && n.kind != "latch") {
      continue;
    }
    if ((opts.match_surface == RegistryMatchSurface::CardAttrs ||
         opts.match_surface == RegistryMatchSurface::CardFull) &&
        n.kind != "fn" && n.kind != "latch" && n.kind != "handoff") {
      continue;
    }
    const std::string passage = registry_card_passage(n, opts.match_surface);
    if (passage.empty()) {
      continue;
    }
    ++report->considered;
    const std::string model_key = registry_embed_model_key(opts.model, opts.match_surface);
    if (!opts.force) {
      std::string cached;
      // Effective hash includes surface so CardAttrs ≠ CardFull for same card_hash.
      const std::string want_hash =
          n.card_hash.empty() ? passage.substr(0, 64)
                              : (n.card_hash + "#" + registry_match_surface_name(opts.match_surface));
      if (cached_hash(r->db, n.id, model_key, &cached) && cached == want_hash && !want_hash.empty()) {
        ++report->skipped_cached;
        continue;
      }
    }
    Job j;
    j.n = std::move(n);
    j.passage = passage;
    if (j.passage.size() > 700) {
      j.passage.resize(700);
    }
    jobs.push_back(std::move(j));
  }
  sqlite3_finalize(st);
  auto kind_rank = [](const std::string& k) {
    if (k == "fn") {
      return 0;
    }
    if (k == "latch") {
      return 1;
    }
    if (k == "handoff") {
      return 2;
    }
    return 3;
  };
  std::sort(jobs.begin(), jobs.end(), [&](const Job& a, const Job& b) {
    const int ra = kind_rank(a.n.kind);
    const int rb = kind_rank(b.n.kind);
    if (ra != rb) {
      return ra < rb;
    }
    return a.n.id < b.n.id;
  });
  if (static_cast<int>(jobs.size()) > opts.max_nodes) {
    jobs.resize(static_cast<std::size_t>(opts.max_nodes));
  }

  std::vector<std::string> texts;
  texts.reserve(jobs.size());
  for (const auto& j : jobs) {
    texts.push_back(j.passage);
  }
  std::vector<std::vector<float>> vecs(texts.size());
  if (embed_passages && !texts.empty()) {
    const std::size_t chunk = 32;
    for (std::size_t off = 0; off < texts.size(); off += chunk) {
      const std::size_t n = std::min(chunk, texts.size() - off);
      std::vector<std::string> part(texts.begin() + static_cast<std::ptrdiff_t>(off),
                                    texts.begin() + static_cast<std::ptrdiff_t>(off + n));
      std::vector<std::vector<float>> pv;
      if (!embed_passages(part, &pv) || pv.size() != part.size()) {
        if (embed) {
          for (std::size_t i = 0; i < part.size(); ++i) {
            if (!embed(false, part[i], &vecs[off + i]) || vecs[off + i].empty()) {
              ++report->failed;
              vecs[off + i].clear();
            }
          }
        } else {
          if (err && err->empty()) {
            set_err(err, "embed_passages falló");
          }
          return false;
        }
      } else {
        for (std::size_t i = 0; i < pv.size(); ++i) {
          vecs[off + i] = std::move(pv[i]);
        }
      }
    }
  } else if (embed) {
    vecs.resize(texts.size());
    for (std::size_t i = 0; i < texts.size(); ++i) {
      if (!embed(false, texts[i], &vecs[i]) || vecs[i].empty()) {
        ++report->failed;
        vecs[i].clear();
      }
    }
  }

  const std::string model_key = registry_embed_model_key(opts.model, opts.match_surface);
  sqlite3_stmt* ins = nullptr;
  sqlite3_prepare_v2(r->db,
                     "INSERT INTO embeddings(node_id,model,dim,card_hash,blob) VALUES(?1,?2,?3,?4,?5) "
                     "ON CONFLICT(node_id,model) DO UPDATE SET dim=excluded.dim, "
                     "card_hash=excluded.card_hash, blob=excluded.blob",
                     -1, &ins, nullptr);
  if (!sql_exec(r->db, "BEGIN IMMEDIATE;", err)) {
    sqlite3_finalize(ins);
    return false;
  }
  for (std::size_t i = 0; i < jobs.size(); ++i) {
    if (i >= vecs.size() || vecs[i].empty()) {
      continue;
    }
    const std::string store_hash =
        jobs[i].n.card_hash.empty()
            ? jobs[i].passage.substr(0, 64)
            : (jobs[i].n.card_hash + "#" + registry_match_surface_name(opts.match_surface));
    sqlite3_reset(ins);
    sqlite3_bind_text(ins, 1, jobs[i].n.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 2, model_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(ins, 3, static_cast<int>(vecs[i].size()));
    sqlite3_bind_text(ins, 4, store_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(ins, 5, vecs[i].data(), static_cast<int>(vecs[i].size() * sizeof(float)),
                      SQLITE_TRANSIENT);
    if (sqlite3_step(ins) != SQLITE_DONE) {
      ++report->failed;
      continue;
    }
    ++report->embedded;
  }
  sqlite3_finalize(ins);
  if (!sql_exec(r->db, "COMMIT;", err)) {
    sql_exec(r->db, "ROLLBACK;", nullptr);
    return false;
  }
  return true;
}

bool registry_query(EffectRegistry* r, const std::string& query, const RegistryEmbedFn& embed,
                    const RegistryQueryOpts& opts, RegistryQueryResult* out, std::string* err) {
  if (r == nullptr || r->db == nullptr || out == nullptr) {
    set_err(err, "query args");
    return false;
  }
  if (query.empty()) {
    set_err(err, "query vacía");
    return false;
  }
  *out = {};
  std::vector<float> qvec;  // used for hop expansion cosine when available

  std::vector<std::string> seed_kinds = opts.seed_kinds;
  if (seed_kinds.empty()) {
    if (opts.match_surface == RegistryMatchSurface::Latch) {
      seed_kinds = {"latch"};
    } else if (opts.match_surface == RegistryMatchSurface::CardAttrs) {
      seed_kinds = {"fn"};
    } else if (opts.match_surface == RegistryMatchSurface::NodeId) {
      seed_kinds = {"fn", "latch", "handoff"};
    } else {
      seed_kinds = {"fn", "latch", "handoff"};
    }
  }
  auto seed_kind_ok = [&](const std::string& kind) {
    return std::find(seed_kinds.begin(), seed_kinds.end(), kind) != seed_kinds.end();
  };
  auto id_prefix_ok = [&](const std::string& id) {
    if (opts.match_surface == RegistryMatchSurface::Latch) {
      return id.rfind("latch:", 0) == 0;
    }
    if (opts.match_surface == RegistryMatchSurface::CardAttrs ||
        opts.match_surface == RegistryMatchSurface::CardFull) {
      return id.rfind("fn:", 0) == 0;
    }
    // NodeId: any seeded kind
    return id.rfind("fn:", 0) == 0 || id.rfind("latch:", 0) == 0 || id.rfind("handoff:", 0) == 0;
  };

  struct Scored {
    std::string id;
    float cos = 0.f;
  };
  std::vector<Scored> scored;

  if (opts.match_surface == RegistryMatchSurface::NodeId) {
    auto lower = [](std::string s) {
      for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      return s;
    };
    const std::string qlow = lower(query);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(r->db,
                       "SELECT id,kind,path,symbol,stem FROM nodes WHERE tombstone_reason=''", -1,
                       &st, nullptr);
    while (sqlite3_step(st) == SQLITE_ROW) {
      const unsigned char* idp = sqlite3_column_text(st, 0);
      const unsigned char* kp = sqlite3_column_text(st, 1);
      if (!idp || !kp) {
        continue;
      }
      const std::string kind = reinterpret_cast<const char*>(kp);
      if (!seed_kind_ok(kind)) {
        continue;
      }
      const std::string id = reinterpret_cast<const char*>(idp);
      if (!id_prefix_ok(id)) {
        continue;
      }
      auto col = [&](int i) -> std::string {
        const unsigned char* p = sqlite3_column_text(st, i);
        return p ? reinterpret_cast<const char*>(p) : "";
      };
      const std::string hay = lower(id + " " + col(2) + " " + col(3) + " " + col(4));
      if (qlow.empty() || hay.find(qlow) == std::string::npos) {
        continue;
      }
      const std::string sym = lower(col(3));
      Scored s;
      s.id = id;
      if (sym == qlow) {
        s.cos = 1.0f;
      } else if (sym.size() >= qlow.size() && sym.compare(0, qlow.size(), qlow) == 0) {
        s.cos = 0.96f;
      } else {
        s.cos = 0.88f;
      }
      scored.push_back(std::move(s));
    }
    sqlite3_finalize(st);
  } else {
    if (!embed) {
      set_err(err, "falta embedder");
      return false;
    }
    if (!embed(true, query, &qvec) || qvec.empty()) {
      set_err(err, "embed query falló");
      return false;
    }
    const std::string model_key = registry_embed_model_key(opts.model, opts.match_surface);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(r->db,
                       "SELECT e.node_id, e.blob, n.kind FROM embeddings e "
                       "JOIN nodes n ON n.id=e.node_id WHERE e.model=?1 AND n.tombstone_reason=''",
                       -1, &st, nullptr);
    sqlite3_bind_text(st, 1, model_key.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
      const unsigned char* idp = sqlite3_column_text(st, 0);
      if (!idp) {
        continue;
      }
      const unsigned char* kp = sqlite3_column_text(st, 2);
      const std::string kind = kp ? reinterpret_cast<const char*>(kp) : "";
      if (!seed_kind_ok(kind)) {
        continue;
      }
      const std::vector<float> v =
          blob_to_vec(sqlite3_column_blob(st, 1), sqlite3_column_bytes(st, 1));
      Scored s;
      s.id = reinterpret_cast<const char*>(idp);
      if (!id_prefix_ok(s.id)) {
        continue;
      }
      s.cos = cosine_similarity(qvec, v);
      scored.push_back(std::move(s));
    }
    sqlite3_finalize(st);
  }
  std::sort(scored.begin(), scored.end(),
            [](const Scored& a, const Scored& b) { return a.cos > b.cos; });
  const int k = std::max(1, opts.top_k);
  const int per_stem = std::max(1, opts.max_per_stem);
  const int map_cap = kRegistryQueryMapStems;
  std::vector<Scored> picked;
  std::unordered_set<std::string> taken;
  std::unordered_map<std::string, int> stem_n;
  auto consider = [&](const Scored& s, bool ignore_stem_cap, bool ignore_k) {
    if (!ignore_k && static_cast<int>(picked.size()) >= k) {
      return;
    }
    if (taken.count(s.id) || !id_prefix_ok(s.id)) {
      return;
    }
    const std::string st = stem_of_node_id(s.id);
    if (!ignore_stem_cap && !st.empty() && stem_n[st] >= per_stem) {
      return;
    }
    picked.push_back(s);
    taken.insert(s.id);
    if (!st.empty()) {
      ++stem_n[st];
    }
  };
  auto scored_by_id = [&](const std::string& id) -> const Scored* {
    for (const auto& s : scored) {
      if (s.id == id) {
        return &s;
      }
    }
    return nullptr;
  };
  int map_used = 0;
  for (const auto& bf : opts.boost_fns) {
    if (bf.symbol.empty() || map_used >= map_cap || static_cast<int>(picked.size()) >= k) {
      break;
    }
    ++map_used;
    const std::string id = registry_canonical_fn_id(r->workspace_root, bf.path, bf.symbol);
    bool got = false;
    if (const Scored* s = scored_by_id(id)) {
      consider(*s, true, false);
      got = taken.count(id) != 0;
    }
    if (!got && !bf.symbol.empty()) {
      const std::string tail = ":" + bf.symbol;
      for (const auto& s : scored) {
        if (s.id.size() > tail.size() &&
            s.id.compare(s.id.size() - tail.size(), tail.size(), tail) == 0) {
          consider(s, true, false);
          got = true;
          break;
        }
      }
    }
    if (!got) {
      RegistryNodeRow row;
      if (load_node(r->db, id, &row) && row.kind == "fn" && row.tombstone_reason.empty()) {
        Scored syn;
        syn.id = id;
        syn.cos = 0.f;
        consider(syn, true, false);
        got = taken.count(id) != 0;
      }
    }
    if (!got) {
      const std::string st = registry_stem_of(bf.path);
      if (st.empty()) {
        continue;
      }
      for (const auto& s : scored) {
        if (!id_prefix_ok(s.id) || stem_of_node_id(s.id) != st) {
          continue;
        }
        consider(s, true, false);
        break;
      }
    }
  }
  for (const auto& stem : opts.boost_stems) {
    if (stem.empty()) {
      continue;
    }
    if (map_used >= map_cap || static_cast<int>(picked.size()) >= k) {
      break;
    }
    ++map_used;
    for (const auto& s : scored) {
      if (!id_prefix_ok(s.id) || stem_of_node_id(s.id) != stem) {
        continue;
      }
      consider(s, true, false);
      break;
    }
  }
  for (const auto& s : scored) {
    consider(s, false, false);
    if (static_cast<int>(picked.size()) >= k) {
      break;
    }
  }
  std::unordered_map<std::string, float> hit_cos;
  for (const auto& s : picked) {
    RegistryQueryHit h;
    if (!load_node(r->db, s.id, &h.node)) {
      continue;
    }
    h.cosine = s.cos;
    h.hop = 0;
    hit_cos[s.id] = s.cos;
    out->hits.push_back(std::move(h));
  }

  const int max_hops = std::max(0, opts.hops);
  if (max_hops <= 0 || out->hits.empty()) {
    return true;
  }
  auto kind_ok = [&](const std::string& knd) {
    if (opts.hop_kinds.empty()) {
      return true;
    }
    return std::find(opts.hop_kinds.begin(), opts.hop_kinds.end(), knd) != opts.hop_kinds.end();
  };
  std::unordered_map<std::string, int> dist;
  std::queue<std::string> q;
  for (const auto& h : out->hits) {
    dist[h.node.id] = 0;
    q.push(h.node.id);
  }
  auto visit_edge = [&](const std::string& cur, const std::string& nxt, const std::string& kind) {
    if (!kind_ok(kind) || nxt.empty() || dist.count(nxt)) {
      return;
    }
    const int d = dist[cur] + 1;
    if (d > max_hops) {
      return;
    }
    dist[nxt] = d;
    q.push(nxt);
  };
  while (!q.empty()) {
    const std::string cur = q.front();
    q.pop();
    if (dist[cur] >= max_hops) {
      continue;
    }
    sqlite3_stmt* fs = nullptr;
    sqlite3_prepare_v2(r->db, "SELECT from_id,to_id,kind FROM facts WHERE from_id=?1 OR to_id=?1",
                       -1, &fs, nullptr);
    sqlite3_bind_text(fs, 1, cur.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(fs) == SQLITE_ROW) {
      auto col = [&](int i) -> std::string {
        const unsigned char* p = sqlite3_column_text(fs, i);
        return p ? reinterpret_cast<const char*>(p) : "";
      };
      const std::string from = col(0);
      const std::string to = col(1);
      const std::string kind = col(2);
      if (from == cur) {
        visit_edge(cur, to, kind);
      }
      if (to == cur) {
        visit_edge(cur, from, kind);
      }
    }
    sqlite3_finalize(fs);
  }
  for (const auto& [id, d] : dist) {
    if (d == 0) {
      continue;
    }
    RegistryQueryHit h;
    if (!load_node(r->db, id, &h.node) || !h.node.tombstone_reason.empty()) {
      continue;
    }
    h.hop = d;
    sqlite3_stmt* es = nullptr;
    sqlite3_prepare_v2(r->db, "SELECT blob FROM embeddings WHERE node_id=?1 AND model=?2", -1, &es,
                       nullptr);
    sqlite3_bind_text(es, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    const std::string model_key = registry_embed_model_key(opts.model, opts.match_surface);
    sqlite3_bind_text(es, 2, model_key.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(es) == SQLITE_ROW && !qvec.empty()) {
      const std::vector<float> v =
          blob_to_vec(sqlite3_column_blob(es, 0), sqlite3_column_bytes(es, 0));
      h.cosine = cosine_similarity(qvec, v);
    }
    sqlite3_finalize(es);
    out->expanded.push_back(std::move(h));
  }
  std::sort(out->expanded.begin(), out->expanded.end(),
            [](const RegistryQueryHit& a, const RegistryQueryHit& b) {
              if (a.hop != b.hop) {
                return a.hop < b.hop;
              }
              return a.cosine > b.cosine;
            });
  return true;
}

bool registry_query_trails(EffectRegistry* r, const std::string& query, const RegistryEmbedFn& embed,
                           const RegistryQueryOpts& opts, RegistryTrailResult* out, std::string* err) {
  if (out == nullptr) {
    set_err(err, "trails args");
    return false;
  }
  *out = {};
  RegistryQueryOpts qopts = opts;
  if (qopts.hops < 2) {
    qopts.hops = 2;
  }
  if (!registry_query(r, query, embed, qopts, &out->query, err)) {
    return false;
  }
  if (out->query.hits.empty()) {
    return true;
  }

  std::unordered_map<std::string, RegistryNodeRow> by_id;
  std::unordered_map<std::string, float> cosine;
  auto take = [&](const RegistryQueryHit& h) {
    if (h.node.id.empty() || !h.node.tombstone_reason.empty()) {
      return;
    }
    if (static_cast<int>(by_id.size()) >= kEffectSliceMaxNodes) {
      return;
    }
    if (by_id.insert({h.node.id, h.node}).second) {
      cosine[h.node.id] = h.cosine;
    }
  };
  for (const auto& h : out->query.hits) {
    take(h);
  }
  for (const auto& h : out->query.expanded) {
    take(h);
  }

  EffectSlice sl;
  sl.query = query;
  std::unordered_set<std::string> ids;
  for (const auto& [id, row] : by_id) {
    ids.insert(id);
    EffectNode n;
    n.id = row.id;
    n.kind = parse_effect_node_kind(row.kind);
    n.ctrl_kind = parse_effect_ctrl_kind(row.ctrl_kind);
    n.path = row.path;
    n.symbol = row.symbol;
    n.stem = row.stem;
    n.line = row.line;
    n.parent_fn = row.parent_fn;
    n.cond = row.cond;
    n.cold = row.cold;
    auto cit = cosine.find(id);
    if (cit != cosine.end() && cit->second > 0.f) {
      n.prior_sem = cit->second;
    }
    sl.nodes.push_back(std::move(n));
  }
  for (std::size_t hit_rank = 0; hit_rank < out->query.hits.size(); ++hit_rank) {
    const auto& h = out->query.hits[hit_rank];
    for (auto& n : sl.nodes) {
      if (n.id != h.node.id) {
        continue;
      }
      if (h.cosine > n.prior_sem) {
        n.prior_sem = h.cosine;
      }
      n.query_hit = true;
      n.query_rank = static_cast<int>(hit_rank);
      bool map_item = false;
      for (const auto& bf : opts.boost_fns) {
        if (bf.symbol == n.symbol) {
          map_item = true;
          break;
        }
      }
      for (const auto& st : opts.boost_stems) {
        if (st == n.stem) {
          map_item = true;
          break;
        }
      }
      if (map_item && n.prior_sem < 0.55f) {
        n.prior_sem = 0.55f;
      }
      break;
    }
  }
  std::unordered_set<std::string> seed_ids;
  auto try_anchor = [&](const RegistryQueryHit& h) {
    if (static_cast<int>(seed_ids.size()) >= kRegistryQueryAnchorSeeds) {
      return;
    }
    if (h.node.kind != "fn" || h.node.id.empty() || seed_ids.count(h.node.id)) {
      return;
    }
    seed_ids.insert(h.node.id);
  };
  for (const auto& h : out->query.hits) {
    try_anchor(h);
  }
  for (auto& n : sl.nodes) {
    if (seed_ids.count(n.id)) {
      n.seed = true;
      sl.seeds.push_back(n.symbol.empty() ? n.id : n.symbol);
    }
  }

  sqlite3_stmt* fs = nullptr;
  sqlite3_prepare_v2(r->db, "SELECT from_id,to_id,kind,member FROM facts", -1, &fs, nullptr);
  while (sqlite3_step(fs) == SQLITE_ROW) {
    auto col = [&](int i) -> std::string {
      const unsigned char* p = sqlite3_column_text(fs, i);
      return p ? reinterpret_cast<const char*>(p) : "";
    };
    const std::string from = col(0);
    const std::string to = col(1);
    if (!ids.count(from) || !ids.count(to) || from == to) {
      continue;
    }
    EffectFact f;
    f.from = from;
    f.to = to;
    f.kind = parse_effect_fact_kind(col(2));
    f.member = col(3);
    f.id = from + "|" + col(2) + "|" + to + "|" + f.member;
    f.w_edge = 1.f;
    sl.facts.push_back(std::move(f));
  }
  sqlite3_finalize(fs);
  out->subgraph_nodes = static_cast<int>(sl.nodes.size());
  out->subgraph_facts = static_cast<int>(sl.facts.size());

  std::vector<float> qvec;
  if (embed(true, query, &qvec) && !qvec.empty()) {
    std::unordered_set<std::string> tried;
    for (const auto& n : sl.nodes) {
      if (n.kind != EffectNodeKind::Latch) {
        continue;
      }
      const std::string mem = effect_latch_member_key(n.symbol.empty() ? n.id : n.symbol);
      if (!effect_hub_member(mem) || !tried.insert(mem).second) {
        continue;
      }
      if (effect_query_unlocks_member(query, mem)) {
        sl.unlocked_members.insert(mem);
        continue;
      }
      if (mem.size() < 6) {
        continue;
      }
      bool query_supported = false;
      for (const auto& fact : sl.facts) {
        if (fact.from != n.id && fact.to != n.id) {
          continue;
        }
        const std::string other = fact.from == n.id ? fact.to : fact.from;
        auto oit = std::find_if(sl.nodes.begin(), sl.nodes.end(),
                                [&](const EffectNode& candidate) {
                                  return candidate.id == other;
                                });
        if (oit != sl.nodes.end() && oit->kind == EffectNodeKind::Fn &&
            (oit->query_hit || oit->prior_sem >= 0.58f)) {
          query_supported = true;
          break;
        }
      }
      if (!query_supported) {
        continue;
      }
      std::vector<float> mv;
      const std::string passage = std::string("code field ") + mem;
      if (embed(false, passage, &mv) && mv.size() == qvec.size()) {
        if (cosine_similarity(qvec, mv) >= 0.66f) {
          sl.unlocked_members.insert(mem);
        }
      }
    }
  }

  effect_slice_rank(&sl, opts.threads > 0 ? opts.threads : kRegistryQueryThreads);
  out->holes = sl.holes;

  std::unordered_map<std::string, const EffectNode*> en;
  for (const auto& n : sl.nodes) {
    en[n.id] = &n;
  }
  for (const auto& hit : out->query.hits) {
    RegistryTrailHop seed;
    seed.node = hit.node;
    seed.cosine = hit.cosine;
    auto eit = en.find(hit.node.id);
    if (eit != en.end()) {
      seed.mass = eit->second->mass;
    }
    out->seeds.push_back(std::move(seed));
  }
  for (const auto& th : sl.threads) {
    RegistryTrail t;
    t.id = th.id;
    t.score = th.score;
    t.why = th.why;
    t.latches = th.latches;
    for (const auto& nid : th.node_ids) {
      RegistryTrailHop hop;
      auto rit = by_id.find(nid);
      if (rit != by_id.end()) {
        hop.node = rit->second;
      } else {
        hop.node.id = nid;
      }
      auto eit = en.find(nid);
      if (eit != en.end()) {
        hop.mass = eit->second->mass;
      }
      auto cit = cosine.find(nid);
      if (cit != cosine.end()) {
        hop.cosine = cit->second;
      }
      t.hops.push_back(std::move(hop));
    }
    out->trails.push_back(std::move(t));
  }
  for (const auto& sc : sl.constellations) {
    RegistryConstellation c;
    c.id = sc.id;
    c.center_id = sc.center_id;
    c.member = sc.member;
    c.score = sc.score;
    c.mass_coverage = sc.mass_coverage;
    c.why = sc.why;
    c.core_stems = sc.core_stems;
    c.context_stems = sc.context_stems;
    c.primary_stems = sc.primary_stems;
    c.peripheral_stems = sc.peripheral_stems;
    c.writers = sc.writer_ids;
    c.readers = sc.reader_ids;
    c.controls = sc.control_ids;
    c.handoffs = sc.handoff_ids;
    for (const auto& nid : sc.node_ids) {
      RegistryTrailHop hop;
      auto rit = by_id.find(nid);
      if (rit != by_id.end()) {
        hop.node = rit->second;
      } else {
        hop.node.id = nid;
      }
      auto eit = en.find(nid);
      if (eit != en.end()) {
        hop.mass = eit->second->mass;
      }
      auto cit = cosine.find(nid);
      if (cit != cosine.end()) {
        hop.cosine = cit->second;
      }
      c.nodes.push_back(std::move(hop));
    }
    out->constellations.push_back(std::move(c));
  }
  for (const auto& sm : sl.macro_constellations) {
    RegistryMacroConstellation m;
    m.id = sm.id;
    m.score = sm.score;
    m.mass_coverage = sm.mass_coverage;
    m.why = sm.why;
    m.nuclei = sm.nucleus_ids;
    m.anchor_groups = sm.anchor_groups;
    m.primary_stems = sm.primary_stems;
    m.merge_witnesses = sm.merge_witnesses;
    m.merge_strength = sm.merge_strength;
    for (const auto& nid : sm.node_ids) {
      RegistryTrailHop hop;
      auto rit = by_id.find(nid);
      if (rit != by_id.end()) {
        hop.node = rit->second;
      } else {
        hop.node.id = nid;
      }
      auto eit = en.find(nid);
      if (eit != en.end()) {
        hop.mass = eit->second->mass;
      }
      auto cit = cosine.find(nid);
      if (cit != cosine.end()) {
        hop.cosine = cit->second;
      }
      m.nodes.push_back(std::move(hop));
    }
    out->macro_constellations.push_back(std::move(m));
  }
  out->max_cosine = 0.f;
  out->map_boosted = 0;
  std::unordered_set<std::string> boost(opts.boost_stems.begin(), opts.boost_stems.end());
  std::unordered_set<std::string> boost_sym;
  for (const auto& bf : opts.boost_fns) {
    if (!bf.symbol.empty()) {
      boost_sym.insert(bf.symbol);
    }
    const std::string st = registry_stem_of(bf.path);
    if (!st.empty()) {
      boost.insert(st);
    }
  }
  for (const auto& h : out->query.hits) {
    if (h.cosine > out->max_cosine) {
      out->max_cosine = h.cosine;
    }
    if (boost.count(h.node.stem) || boost_sym.count(h.node.symbol)) {
      ++out->map_boosted;
    }
  }
  out->weak_gate = out->max_cosine < 0.58f && out->map_boosted == 0;
  return true;
}

bool registry_causal_judge_opts_apply_json(RegistryCausalJudgeOpts* opts, const nlohmann::json& j,
                                           std::string* err) {
  if (opts == nullptr) {
    set_err(err, "opts null");
    return false;
  }
  if (!j.is_object()) {
    set_err(err, "judge knobs must be object");
    return false;
  }
  auto take_bool = [&](const char* key, bool* dest) {
    if (!j.contains(key)) {
      return true;
    }
    if (!j[key].is_boolean()) {
      set_err(err, std::string("knob ") + key + " must be bool");
      return false;
    }
    *dest = j[key].get<bool>();
    return true;
  };
  auto take_int = [&](const char* key, int* dest) {
    if (!j.contains(key)) {
      return true;
    }
    if (!j[key].is_number_integer() && !j[key].is_number_unsigned()) {
      set_err(err, std::string("knob ") + key + " must be int");
      return false;
    }
    *dest = j[key].get<int>();
    return true;
  };
  auto take_float = [&](const char* key, float* dest) {
    if (!j.contains(key)) {
      return true;
    }
    if (!j[key].is_number()) {
      set_err(err, std::string("knob ") + key + " must be number");
      return false;
    }
    *dest = j[key].get<float>();
    return true;
  };
  if (!take_bool("mechanism_pack", &opts->mechanism_pack) ||
      !take_int("max_zones", &opts->max_zones) ||
      !take_int("max_representatives", &opts->max_representatives) ||
      !take_int("max_edges", &opts->max_edges) || !take_int("max_trails", &opts->max_trails) ||
      !take_int("max_uncovered_seeds", &opts->max_uncovered_seeds) ||
      !take_int("expand_hops", &opts->expand_hops) ||
      !take_bool("outline_all_representatives", &opts->outline_all_representatives) ||
      !take_bool("promote_uncovered", &opts->promote_uncovered) ||
      !take_int("skel_trigger_cup", &opts->skel_trigger_cup) ||
      !take_int("skel_state_cup", &opts->skel_state_cup) ||
      !take_int("skel_effect_cup", &opts->skel_effect_cup) ||
      !take_int("port_cup", &opts->port_cup) || !take_float("w_kind_write", &opts->w_kind_write) ||
      !take_float("w_kind_read", &opts->w_kind_read) ||
      !take_float("w_kind_handoff", &opts->w_kind_handoff) ||
      !take_float("w_kind_ctrl", &opts->w_kind_ctrl) ||
      !take_float("w_kind_call", &opts->w_kind_call) ||
      !take_float("w_kind_enter_ctrl", &opts->w_kind_enter_ctrl) ||
      !take_float("w_cos", &opts->w_cos) || !take_float("w_ppr", &opts->w_ppr) ||
      !take_float("w_anchor", &opts->w_anchor) || !take_float("w_direct", &opts->w_direct) ||
      !take_float("w_hub", &opts->w_hub) || !take_float("w_redundancy", &opts->w_redundancy) ||
      !take_float("semantic_hard_floor", &opts->semantic_hard_floor)) {
    return false;
  }
  return true;
}

bool registry_causal_judge_payload(EffectRegistry* r, const std::string& query,
                                   const RegistryTrailResult& result,
                                   const RegistryCausalJudgeOpts& opts, nlohmann::json* out,
                                   std::string* err) {
  if (r == nullptr || r->db == nullptr || out == nullptr) {
    set_err(err, "causal judge args");
    return false;
  }
  struct JudgeFact {
    std::string from;
    std::string to;
    std::string kind;
    std::string member;
  };
  std::vector<JudgeFact> facts;
  std::unordered_map<std::string, int> call_degree;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(r->db, "SELECT from_id,to_id,kind,member FROM facts", -1, &st,
                         nullptr) != SQLITE_OK) {
    set_err(err, sqlite3_errmsg(r->db));
    return false;
  }
  while (sqlite3_step(st) == SQLITE_ROW) {
    auto col = [&](int i) -> std::string {
      const unsigned char* p = sqlite3_column_text(st, i);
      return p ? reinterpret_cast<const char*>(p) : "";
    };
    JudgeFact fact{col(0), col(1), col(2), col(3)};
    if (fact.kind == "call" || fact.kind == "handoff") {
      ++call_degree[fact.from];
      ++call_degree[fact.to];
    }
    facts.push_back(std::move(fact));
  }
  sqlite3_finalize(st);

  std::unordered_map<std::string, int> query_rank;
  std::unordered_map<std::string, const RegistryTrailHop*> seed_by_id;
  for (std::size_t i = 0; i < result.seeds.size(); ++i) {
    query_rank[result.seeds[i].node.id] = static_cast<int>(i);
    seed_by_id[result.seeds[i].node.id] = &result.seeds[i];
  }
  std::unordered_map<std::string, const RegistryConstellation*> nucleus_by_id;
  for (const auto& c : result.constellations) {
    nucleus_by_id[c.id] = &c;
  }

  auto node_target = [](const RegistryNodeRow& node) {
    if (!node.path.empty() && !node.symbol.empty()) {
      return node.path + ":" + node.symbol;
    }
    if (!node.symbol.empty()) {
      return node.symbol;
    }
    return node.id;
  };
  auto compact_ref = [&](const RegistryTrailHop& hop) {
    nlohmann::json j = {{"id", hop.node.id},
                        {"target", node_target(hop.node)},
                        {"kind", hop.node.kind},
                        {"stem", hop.node.stem}};
    auto qit = query_rank.find(hop.node.id);
    if (qit != query_rank.end()) {
      j["qrank"] = qit->second;
      j["cos"] = hop.cosine;
      j["mass"] = hop.mass;
    }
    if (!hop.node.cond.empty()) {
      j["cond"] = hop.node.cond.size() > 120 ? hop.node.cond.substr(0, 120) + "…"
                                             : hop.node.cond;
    }
    return j;
  };
  auto compact_card = [&](const RegistryNodeRow& node, bool include_outline) {
    nlohmann::json out_card = {{"id", node.id}, {"target", node_target(node)}};
    if (node.card_json.empty()) {
      return out_card;
    }
    try {
      const auto card = nlohmann::json::parse(node.card_json);
      auto copy_string = [&](const char* key, std::size_t cap) {
        if (!card.contains(key) || !card[key].is_string()) {
          return;
        }
        std::string value = card[key].get<std::string>();
        if (value.empty()) {
          return;
        }
        if (value.size() > cap) {
          value = value.substr(0, cap) + "…";
        }
        out_card[key] = value;
      };
      auto copy_list = [&](const char* key, std::size_t cap) {
        if (!card.contains(key) || !card[key].is_array()) {
          return;
        }
        nlohmann::json values = nlohmann::json::array();
        for (const auto& value : card[key]) {
          if (values.size() >= cap || !value.is_string()) {
            break;
          }
          const std::string text = value.get<std::string>();
          if (!text.empty()) {
            values.push_back(text);
          }
        }
        if (!values.empty()) {
          out_card[key] = std::move(values);
        }
      };
      copy_string("sig", 140);
      copy_string("ctrl", 100);
      copy_string("guard", 100);
      copy_list("roles", 3);
      copy_list("writes", 3);
      copy_list("reads", 3);
      copy_list("calls_seed", 3);
      copy_list("hot", 3);
      if (include_outline && card.contains("start_line") && card.contains("end_line") &&
          card["start_line"].is_number_integer() && card["end_line"].is_number_integer() &&
          !node.path.empty()) {
        const int start_line = card["start_line"].get<int>();
        const int end_line = card["end_line"].get<int>();
        if (start_line > 0 && end_line >= start_line && end_line - start_line <= 80) {
          std::ifstream source(fs::path(r->workspace_root) / node.path);
          struct OutlineLine {
            int line = 0;
            int score = 0;
            std::string text;
          };
          std::vector<OutlineLine> candidates;
          std::string line;
          int line_no = 0;
          while (std::getline(source, line)) {
            ++line_no;
            if (line_no < start_line) {
              continue;
            }
            if (line_no > end_line) {
              break;
            }
            const auto first = line.find_first_not_of(" \t");
            if (first == std::string::npos) {
              continue;
            }
            std::string text = line.substr(first);
            if (text == "{" || text == "}" || text.rfind("//", 0) == 0 ||
                (line_no <= start_line + 1 && text.find(node.symbol + "(") != std::string::npos)) {
              continue;
            }
            int score = 0;
            const bool branch = text.rfind("if ", 0) == 0 || text.rfind("if(", 0) == 0;
            const bool call_stmt =
                !branch && text.find('(') != std::string::npos &&
                text.find(';') != std::string::npos && text.rfind("return ", 0) != 0;
            if (call_stmt) {
              score = 100;
            } else if (text.find(".store(") != std::string::npos ||
                       text.find(".exchange(") != std::string::npos ||
                       text.find(" = ") != std::string::npos) {
              score = 85;
            } else if (branch) {
              score = 65;
            } else if (text.rfind("return", 0) == 0) {
              score = 40;
            }
            if (text.find("lock_guard") != std::string::npos ||
                text.find("steady_now_ms") != std::string::npos ||
                text.find("sleep_for") != std::string::npos ||
                text.find("append(") != std::string::npos) {
              score = std::min(score, 25);
            }
            if (score == 0) {
              continue;
            }
            if (text.size() > 120) {
              text = text.substr(0, 120) + "…";
            }
            candidates.push_back({line_no, score, std::move(text)});
          }
          std::stable_sort(candidates.begin(), candidates.end(),
                           [](const OutlineLine& a, const OutlineLine& b) {
                             return a.score > b.score;
                           });
          if (candidates.size() > 4) {
            candidates.resize(4);
          }
          std::sort(candidates.begin(), candidates.end(),
                    [](const OutlineLine& a, const OutlineLine& b) {
                      return a.line < b.line;
                    });
          nlohmann::json outline = nlohmann::json::array();
          for (const auto& candidate : candidates) {
            outline.push_back(std::to_string(candidate.line) + ": " + candidate.text);
          }
          if (!outline.empty()) {
            out_card["outline"] = std::move(outline);
          }
        }
      }
    } catch (...) {
    }
    return out_card;
  };

  std::unordered_set<std::string> zone_filter(opts.zone_filter.begin(), opts.zone_filter.end());
  std::vector<const RegistryMacroConstellation*> selected_macros;
  const int macro_limit =
      opts.promote_uncovered ? std::max(0, opts.max_zones - 2) : std::max(0, opts.max_zones);
  for (const auto& macro : result.macro_constellations) {
    if (!zone_filter.empty() && !zone_filter.count(macro.id)) {
      continue;
    }
    if (static_cast<int>(selected_macros.size()) >= macro_limit) {
      break;
    }
    selected_macros.push_back(&macro);
  }
  const int zone_cap = static_cast<int>(selected_macros.size());
  const std::unordered_set<std::string> requested_targets(opts.expand_targets.begin(),
                                                          opts.expand_targets.end());
  std::unordered_map<std::string, std::string> preliminary_macro_of;
  std::unordered_map<std::string, const RegistryTrailHop*> global_hops;
  for (const auto* macro_ptr : selected_macros) {
    for (const auto& hop : macro_ptr->nodes) {
      preliminary_macro_of[hop.node.id] = macro_ptr->id;
      global_hops[hop.node.id] = &hop;
    }
  }
  nlohmann::json zones = nlohmann::json::array();
  std::unordered_set<std::string> explained_anchors;
  std::vector<std::unordered_set<std::string>> emitted_zone_nodes;
  std::unordered_map<std::string, std::string> node_to_macro_zone;
  int emitted_representatives = 0;
  int emitted_edges = 0;
  for (int zi = 0; zi < zone_cap; ++zi) {
    const RegistryMacroConstellation& macro = *selected_macros[static_cast<std::size_t>(zi)];
    std::unordered_map<std::string, const RegistryTrailHop*> nodes;
    std::deque<RegistryTrailHop> expanded_nodes;
    std::unordered_set<std::string> zone_ids;
    for (const auto& hop : macro.nodes) {
      nodes[hop.node.id] = &hop;
      zone_ids.insert(hop.node.id);
    }
    if (opts.expand_hops > 0 && !requested_targets.empty()) {
      std::vector<std::pair<std::string, int>> pending;
      std::unordered_set<std::string> seen;
      for (const auto& hop : macro.nodes) {
        if (requested_targets.count(node_target(hop.node))) {
          pending.push_back({hop.node.id, 0});
          seen.insert(hop.node.id);
        }
      }
      for (std::size_t pi = 0; pi < pending.size(); ++pi) {
        const auto [id, depth] = pending[pi];
        if (depth >= std::min(2, opts.expand_hops)) {
          continue;
        }
        std::vector<RegistryNeighbor> neighbors;
        std::string ignored;
        if (!registry_neighbors(r, id,
                                {"write", "read", "handoff", "call", "enter_ctrl", "then",
                                 "else", "case"},
                                "both", &neighbors, &ignored)) {
          continue;
        }
        for (const auto& neighbor : neighbors) {
          if (call_degree[neighbor.node.id] > 12 && depth > 0) {
            continue;
          }
          if (seen.insert(neighbor.node.id).second) {
            expanded_nodes.push_back({neighbor.node, 0.f, 0.f});
            nodes[neighbor.node.id] = &expanded_nodes.back();
            zone_ids.insert(neighbor.node.id);
            pending.push_back({neighbor.node.id, depth + 1});
          }
        }
      }
    }
    std::unordered_set<std::string> anchor_ids;
    nlohmann::json anchor_groups = nlohmann::json::array();
    for (const auto& group : macro.anchor_groups) {
      nlohmann::json anchors = nlohmann::json::array();
      for (const auto& id : group) {
        auto nit = nodes.find(id);
        if (nit == nodes.end()) {
          continue;
        }
        anchors.push_back(compact_ref(*nit->second));
        anchor_ids.insert(id);
        explained_anchors.insert(id);
      }
      if (!anchors.empty()) {
        anchor_groups.push_back(std::move(anchors));
      }
    }

    std::unordered_set<std::string> direct_ids;
    std::unordered_set<std::string> state_ids;
    std::unordered_set<std::string> core_stems;
    std::unordered_set<std::string> context_stems;
    nlohmann::json nuclei = nlohmann::json::array();
    nlohmann::json roles = {{"writers", nlohmann::json::array()},
                            {"readers", nlohmann::json::array()},
                            {"controls", nlohmann::json::array()},
                            {"handoffs", nlohmann::json::array()}};
    std::unordered_set<std::string> role_seen;
    auto add_role = [&](const char* role, const std::string& id) {
      if (!role_seen.insert(std::string(role) + "|" + id).second) {
        return;
      }
      auto nit = nodes.find(id);
      if (nit != nodes.end() && roles[role].size() < 4) {
        roles[role].push_back(compact_ref(*nit->second));
      }
    };
    for (const auto& nucleus_id : macro.nuclei) {
      auto cit = nucleus_by_id.find(nucleus_id);
      if (cit == nucleus_by_id.end()) {
        continue;
      }
      const RegistryConstellation& c = *cit->second;
      core_stems.insert(c.core_stems.begin(), c.core_stems.end());
      context_stems.insert(c.context_stems.begin(), c.context_stems.end());
      state_ids.insert(c.center_id);
      nuclei.push_back({{"id", c.id},
                         {"state", c.member},
                         {"score", c.score},
                         {"coverage", c.mass_coverage}});
      for (const auto& id : c.writers) {
        direct_ids.insert(id);
        add_role("writers", id);
      }
      for (const auto& id : c.readers) {
        direct_ids.insert(id);
        add_role("readers", id);
      }
      for (const auto& id : c.controls) {
        add_role("controls", id);
      }
      for (const auto& id : c.handoffs) {
        add_role("handoffs", id);
      }
    }
    for (const auto& stem : core_stems) {
      context_stems.erase(stem);
    }

    std::vector<std::string> representative_ids;
    std::unordered_set<std::string> representative_seen;
    auto add_representative = [&](const std::string& id) {
      if (static_cast<int>(representative_ids.size()) >=
              std::max(0, opts.max_representatives) ||
          !nodes.count(id) || !representative_seen.insert(id).second) {
        return;
      }
      representative_ids.push_back(id);
    };
    for (const auto& [id, hop] : nodes) {
      if (requested_targets.count(node_target(hop->node))) {
        add_representative(id);
      }
    }
    for (const auto& group : macro.anchor_groups) {
      for (const auto& id : group) {
        add_representative(id);
      }
    }
    for (const auto& id : direct_ids) {
      add_representative(id);
    }
    std::vector<const RegistryTrailHop*> ranked_nodes;
    for (const auto& hop : macro.nodes) {
      if (hop.node.kind == "fn") {
        ranked_nodes.push_back(&hop);
      }
    }
    std::stable_sort(ranked_nodes.begin(), ranked_nodes.end(),
                     [&](const RegistryTrailHop* a, const RegistryTrailHop* b) {
                       const bool aq = query_rank.count(a->node.id) > 0;
                       const bool bq = query_rank.count(b->node.id) > 0;
                       if (aq != bq) {
                         return aq;
                       }
                       const float as = a->cosine + 2.f * a->mass;
                       const float bs = b->cosine + 2.f * b->mass;
                       return as > bs;
                     });
    for (const auto* hop : ranked_nodes) {
      add_representative(hop->node.id);
    }
    nlohmann::json representatives = nlohmann::json::array();
    for (const auto& id : representative_ids) {
      representatives.push_back(
          compact_card(nodes.at(id)->node, opts.outline_all_representatives ||
                                               anchor_ids.count(id) > 0 ||
                                               requested_targets.count(node_target(nodes.at(id)->node))));
    }
    emitted_representatives += static_cast<int>(representatives.size());

    struct RankedFact {
      const JudgeFact* fact = nullptr;
      float score = 0.f;
      std::string slot;  // trigger|state|effect|port|support
    };
    auto kind_weight = [&](const std::string& kind) -> float {
      if (kind == "write") {
        return opts.w_kind_write;
      }
      if (kind == "read") {
        return opts.w_kind_read;
      }
      if (kind == "handoff") {
        return opts.w_kind_handoff;
      }
      if (kind == "then" || kind == "else" || kind == "case") {
        return opts.w_kind_ctrl;
      }
      if (kind == "call") {
        return opts.w_kind_call;
      }
      if (kind == "enter_ctrl") {
        return opts.w_kind_enter_ctrl;
      }
      return 10.f;
    };
    auto hop_cos = [&](const std::string& id) -> float {
      auto nit = nodes.find(id);
      if (nit != nodes.end()) {
        return std::max(0.f, nit->second->cosine);
      }
      auto git = global_hops.find(id);
      if (git != global_hops.end()) {
        return std::max(0.f, git->second->cosine);
      }
      return 0.f;
    };
    auto hop_mass = [&](const std::string& id) -> float {
      auto nit = nodes.find(id);
      if (nit != nodes.end()) {
        return std::max(0.f, nit->second->mass);
      }
      auto git = global_hops.find(id);
      if (git != global_hops.end()) {
        return std::max(0.f, git->second->mass);
      }
      return 0.f;
    };
    auto resolve_target = [&](const std::string& id) -> std::string {
      auto nit = nodes.find(id);
      if (nit != nodes.end()) {
        return node_target(nit->second->node);
      }
      auto git = global_hops.find(id);
      if (git != global_hops.end()) {
        return node_target(git->second->node);
      }
      RegistryNodeRow row;
      std::string ignored;
      if (registry_get(r, id, &row, &ignored)) {
        return node_target(row);
      }
      return id;
    };
    auto resolve_cond = [&](const std::string& id) -> std::string {
      auto nit = nodes.find(id);
      if (nit != nodes.end() && !nit->second->node.cond.empty()) {
        return nit->second->node.cond.size() > 100
                   ? nit->second->node.cond.substr(0, 100) + "…"
                   : nit->second->node.cond;
      }
      auto git = global_hops.find(id);
      if (git != global_hops.end() && !git->second->node.cond.empty()) {
        return git->second->node.cond.size() > 100
                   ? git->second->node.cond.substr(0, 100) + "…"
                   : git->second->node.cond;
      }
      return {};
    };
    auto base_edge_score = [&](const JudgeFact& fact, bool for_port) -> float {
      float score = kind_weight(fact.kind);
      const float cos =
          std::max(hop_cos(fact.from), std::max(hop_cos(fact.to), 0.f));
      score += opts.w_cos * cos;
      score += opts.w_ppr * std::max(hop_mass(fact.from), hop_mass(fact.to));
      if (anchor_ids.count(fact.from) || anchor_ids.count(fact.to)) {
        score += opts.w_anchor;
      }
      if (direct_ids.count(fact.from) || direct_ids.count(fact.to)) {
        score += opts.w_direct;
      }
      if (call_degree[fact.from] > 12 || call_degree[fact.to] > 12) {
        score -= opts.w_hub;
      }
      if (for_port) {
        score += 15.f;
      }
      return score;
    };
    auto make_edge_json = [&](const JudgeFact& fact, const std::string& slot,
                              float score) -> nlohmann::json {
      nlohmann::json edge = {{"from", resolve_target(fact.from)},
                             {"kind", fact.kind},
                             {"to", resolve_target(fact.to)},
                             {"slot", slot},
                             {"score", score}};
      if (!fact.member.empty()) {
        edge["member"] = fact.member;
      }
      const std::string cond = resolve_cond(fact.to);
      if (!cond.empty()) {
        edge["cond"] = cond;
      }
      return edge;
    };
    auto fact_key = [](const JudgeFact& fact) {
      return fact.from + "|" + fact.kind + "|" + fact.to + "|" + fact.member;
    };

    std::vector<RankedFact> ranked_facts;
    std::vector<RankedFact> port_candidates;
    for (const auto& fact : facts) {
      const bool in_from = zone_ids.count(fact.from) > 0;
      const bool in_to = zone_ids.count(fact.to) > 0;
      if (in_from && in_to) {
        ranked_facts.push_back({&fact, base_edge_score(fact, false), {}});
        continue;
      }
      if (!opts.mechanism_pack || opts.port_cup <= 0) {
        continue;
      }
      if (!(in_from ^ in_to)) {
        continue;
      }
      const std::string outside = in_from ? fact.to : fact.from;
      auto mit = preliminary_macro_of.find(outside);
      if (mit == preliminary_macro_of.end() || mit->second == macro.id) {
        continue;
      }
      if (fact.kind != "write" && fact.kind != "read" && fact.kind != "handoff" &&
          fact.kind != "call" && fact.kind != "enter_ctrl" && fact.kind != "then" &&
          fact.kind != "else" && fact.kind != "case") {
        continue;
      }
      port_candidates.push_back({&fact, base_edge_score(fact, true), "port"});
    }
    std::stable_sort(ranked_facts.begin(), ranked_facts.end(),
                     [](const RankedFact& a, const RankedFact& b) {
                       if (a.score != b.score) {
                         return a.score > b.score;
                       }
                       if (a.fact->kind != b.fact->kind) {
                         return a.fact->kind < b.fact->kind;
                       }
                       return a.fact->from < b.fact->from;
                     });
    std::stable_sort(port_candidates.begin(), port_candidates.end(),
                     [](const RankedFact& a, const RankedFact& b) {
                       return a.score > b.score;
                     });

    nlohmann::json mechanism = nlohmann::json::object();
    nlohmann::json ports = nlohmann::json::array();
    nlohmann::json support_edges = nlohmann::json::array();
    nlohmann::json edges = nlohmann::json::array();
    nlohmann::json skeleton_missing = nlohmann::json::array();
    std::unordered_set<std::string> picked_keys;
    std::unordered_set<std::string> picked_members;
    int redundancy_hits = 0;
    auto try_pick = [&](const RankedFact& ranked, const std::string& slot,
                        bool enforce_floor) -> bool {
      if (ranked.fact == nullptr) {
        return false;
      }
      const JudgeFact& fact = *ranked.fact;
      const std::string key = fact_key(fact);
      if (picked_keys.count(key)) {
        return false;
      }
      float score = ranked.score;
      if (!fact.member.empty() && picked_members.count(fact.member)) {
        score -= opts.w_redundancy;
        ++redundancy_hits;
      }
      if (enforce_floor && score < opts.semantic_hard_floor) {
        return false;
      }
      if (static_cast<int>(edges.size()) >= std::max(0, opts.max_edges)) {
        return false;
      }
      nlohmann::json edge = make_edge_json(fact, slot, score);
      if (slot == "trigger" || slot == "state" || slot == "effect") {
        mechanism[slot] = edge;
      } else if (slot == "port") {
        const std::string outside = zone_ids.count(fact.from) ? fact.to : fact.from;
        auto mit = preliminary_macro_of.find(outside);
        edge["from_zone"] = macro.id;
        edge["to_zone"] = mit != preliminary_macro_of.end() ? mit->second : "";
        edge["why"] = fact.kind + " frontier";
        ports.push_back(edge);
      } else {
        support_edges.push_back(edge);
      }
      edges.push_back(edge);
      picked_keys.insert(key);
      if (!fact.member.empty()) {
        picked_members.insert(fact.member);
      }
      return true;
    };

    if (opts.mechanism_pack) {
      auto pick_best_scored =
          [&](const char* slot, int cup,
              const std::function<float(const RankedFact&)>& score_fn) {
            if (cup <= 0) {
              skeleton_missing.push_back(slot);
              return;
            }
            std::vector<RankedFact> cands;
            for (const auto& ranked : ranked_facts) {
              const float adj = score_fn(ranked);
              if (adj < 0.f) {
                continue;
              }
              RankedFact copy = ranked;
              copy.score = adj;
              cands.push_back(copy);
            }
            std::stable_sort(cands.begin(), cands.end(),
                             [](const RankedFact& a, const RankedFact& b) {
                               return a.score > b.score;
                             });
            int taken = 0;
            for (const auto& ranked : cands) {
              if (taken >= cup) {
                break;
              }
              if (try_pick(ranked, slot, false)) {
                ++taken;
              }
            }
            if (taken == 0) {
              skeleton_missing.push_back(slot);
            }
          };
      pick_best_scored("state", opts.skel_state_cup, [&](const RankedFact& ranked) {
        const JudgeFact& fact = *ranked.fact;
        if (fact.kind != "write" && fact.kind != "read") {
          return -1.f;
        }
        if (!(state_ids.count(fact.to) || state_ids.count(fact.from) ||
              (!fact.member.empty() &&
               std::any_of(nuclei.begin(), nuclei.end(), [&](const nlohmann::json& n) {
                 return n.value("state", "") == fact.member;
               })))) {
          return -1.f;
        }
        float score = ranked.score;
        if (fact.kind == "write") {
          score += 10.f;
        }
        return score;
      });
      pick_best_scored("trigger", opts.skel_trigger_cup, [&](const RankedFact& ranked) {
        const JudgeFact& fact = *ranked.fact;
        const bool touches_anchor =
            anchor_ids.count(fact.from) || anchor_ids.count(fact.to);
        const bool query_tied =
            (direct_ids.count(fact.from) || direct_ids.count(fact.to)) &&
            (query_rank.count(fact.from) || query_rank.count(fact.to));
        if (!(touches_anchor || query_tied)) {
          return -1.f;
        }
        if (fact.kind != "enter_ctrl" && fact.kind != "then" && fact.kind != "else" &&
            fact.kind != "case" && fact.kind != "call" && fact.kind != "handoff") {
          return -1.f;
        }
        float score = ranked.score;
        if (fact.kind == "enter_ctrl" || fact.kind == "then" || fact.kind == "else" ||
            fact.kind == "case") {
          score += 25.f;
        } else if (fact.kind == "handoff") {
          score += 15.f;
        }
        return score;
      });
      pick_best_scored("effect", opts.skel_effect_cup, [&](const RankedFact& ranked) {
        const JudgeFact& fact = *ranked.fact;
        if (fact.kind != "call" && fact.kind != "handoff" && fact.kind != "read") {
          return -1.f;
        }
        if (!(direct_ids.count(fact.from) > 0 && !state_ids.count(fact.to))) {
          return -1.f;
        }
        float score = ranked.score;
        if (mechanism.contains("state") && mechanism["state"].contains("member") &&
            !fact.member.empty() &&
            fact.member == mechanism["state"].value("member", "")) {
          score += 40.f;
          if (fact.kind == "read") {
            score += 20.f;
          }
        }
        if (fact.kind == "handoff") {
          score += 12.f;
        } else if (fact.kind == "read") {
          score += 8.f;
        }
        // Mild penalty for call into high-degree nodes (generic sinks).
        if (fact.kind == "call" && call_degree[fact.to] > 12) {
          score -= 20.f;
        }
        return score;
      });

      int ports_taken = 0;
      for (const auto& ranked : port_candidates) {
        if (ports_taken >= opts.port_cup) {
          break;
        }
        if (try_pick(ranked, "port", true)) {
          ++ports_taken;
        }
      }

      const int skel_used = static_cast<int>(mechanism.size());
      const int support_budget =
          std::max(0, opts.max_edges - skel_used - static_cast<int>(ports.size()));
      int support_taken = 0;
      for (const auto& ranked : ranked_facts) {
        if (support_taken >= support_budget) {
          break;
        }
        if (try_pick(ranked, "support", true)) {
          ++support_taken;
        }
      }
    } else {
      for (const auto& ranked : ranked_facts) {
        if (static_cast<int>(edges.size()) >= std::max(0, opts.max_edges)) {
          break;
        }
        try_pick(ranked, "support", false);
      }
    }
    emitted_edges += static_cast<int>(edges.size());
    const int skeleton_filled = static_cast<int>(mechanism.size());
    nlohmann::json pack_meta = {{"skeleton_filled", skeleton_filled},
                                {"skeleton_missing", skeleton_missing},
                                {"budget_used", edges.size()},
                                {"redundancy", redundancy_hits},
                                {"mechanism_pack", opts.mechanism_pack}};

    // Legacy adjacency for closure still uses ranked intra-zone facts.

    nlohmann::json trails = nlohmann::json::array();
    for (const auto& trail : result.trails) {
      if (static_cast<int>(trails.size()) >= std::max(0, opts.max_trails)) {
        break;
      }
      int overlap = 0;
      for (const auto& hop : trail.hops) {
        overlap += zone_ids.count(hop.node.id) ? 1 : 0;
      }
      if (overlap == 0) {
        continue;
      }
      nlohmann::json path = nlohmann::json::array();
      for (const auto& hop : trail.hops) {
        if (path.size() >= 8) {
          break;
        }
        path.push_back(node_target(hop.node));
      }
      trails.push_back({{"id", trail.id},
                        {"score", trail.score},
                        {"why", trail.why},
                        {"overlap", overlap},
                        {"path", path}});
    }

    nlohmann::json risks = nlohmann::json::array();
    if (macro.nuclei.empty()) {
      risks.push_back("no_state_nucleus");
    }
    if (macro.nodes.size() > 48) {
      risks.push_back("large_zone");
    }
    if (macro.anchor_groups.empty()) {
      risks.push_back("no_query_branch");
    }
    if (result.weak_gate) {
      risks.push_back("weak_query_gate");
    }
    nlohmann::json hubs = nlohmann::json::array();
    for (const auto& [id, degree] : call_degree) {
      if (degree <= 12 || !zone_ids.count(id)) {
        continue;
      }
      auto nit = nodes.find(id);
      if (nit != nodes.end() && hubs.size() < 4) {
        hubs.push_back({{"target", node_target(nit->second->node)}, {"degree", degree}});
      }
    }
    std::unordered_map<std::string, std::vector<std::string>> zone_adj;
    for (const auto& ranked : ranked_facts) {
      zone_adj[ranked.fact->from].push_back(ranked.fact->to);
      zone_adj[ranked.fact->to].push_back(ranked.fact->from);
    }
    auto group_reaches_state = [&](const std::vector<std::string>& group, bool skip_hubs) {
      if (state_ids.empty()) {
        return false;
      }
      std::queue<std::pair<std::string, int>> pending;
      std::unordered_set<std::string> seen;
      for (const auto& id : group) {
        if (zone_ids.count(id) && seen.insert(id).second) {
          pending.push({id, 0});
        }
      }
      while (!pending.empty()) {
        const auto [id, depth] = pending.front();
        pending.pop();
        if (state_ids.count(id)) {
          return true;
        }
        if (depth >= 4) {
          continue;
        }
        auto ait = zone_adj.find(id);
        if (ait == zone_adj.end()) {
          continue;
        }
        for (const auto& next : ait->second) {
          if (skip_hubs && !state_ids.count(next) && !anchor_ids.count(next) &&
              call_degree[next] > 12) {
            continue;
          }
          if (seen.insert(next).second) {
            pending.push({next, depth + 1});
          }
        }
      }
      return false;
    };
    int closed_groups = 0;
    int closed_without_hubs = 0;
    for (const auto& group : macro.anchor_groups) {
      closed_groups += group_reaches_state(group, false) ? 1 : 0;
      closed_without_hubs += group_reaches_state(group, true) ? 1 : 0;
    }
    if (!state_ids.empty() &&
        closed_groups < static_cast<int>(macro.anchor_groups.size())) {
      risks.push_back("unclosed_query_branch");
    }
    if (closed_without_hubs < closed_groups) {
      risks.push_back("hub_dependent_bridge");
    }

    float overlap_previous = 0.f;
    for (const auto& previous : emitted_zone_nodes) {
      int intersection = 0;
      for (const auto& id : zone_ids) {
        intersection += previous.count(id) ? 1 : 0;
      }
      const int union_n =
          static_cast<int>(previous.size() + zone_ids.size()) - intersection;
      if (union_n > 0) {
        overlap_previous =
            std::max(overlap_previous, static_cast<float>(intersection) / union_n);
      }
    }
    emitted_zone_nodes.push_back(zone_ids);
    const float margin = zi + 1 < zone_cap
                             ? macro.score -
                                   selected_macros[static_cast<std::size_t>(zi + 1)]->score
                             : macro.score;
    if (zi == 0 && zone_cap > 1 && margin < 0.03f) {
      risks.push_back("low_top_margin");
    }

    for (const auto& hop : macro.nodes) {
      node_to_macro_zone[hop.node.id] = macro.id;
    }
    for (const auto& id : zone_ids) {
      if (!node_to_macro_zone.count(id)) {
        node_to_macro_zone[id] = macro.id;
      }
    }

    std::vector<std::string> emitted_primary(macro.primary_stems.begin(), macro.primary_stems.end());
    std::unordered_set<std::string> primary_seen(emitted_primary.begin(), emitted_primary.end());
    for (const auto& stem : context_stems) {
      if (emitted_primary.size() >= 3) {
        break;
      }
      if (primary_seen.count(stem)) {
        continue;
      }
      bool has_writer = false;
      for (const auto& writer_id : direct_ids) {
        auto wit = nodes.find(writer_id);
        if (wit != nodes.end() && wit->second->node.stem == stem) {
          has_writer = true;
          break;
        }
      }
      if (has_writer) {
        emitted_primary.push_back(stem);
        primary_seen.insert(stem);
      }
    }

    zones.push_back(
        {{"id", macro.id},
         {"rank", zi + 1},
         {"score", macro.score},
         {"score_margin", margin},
         {"mass_coverage", macro.mass_coverage},
         {"primary_stems", emitted_primary},
         {"core_stems", std::vector<std::string>(core_stems.begin(), core_stems.end())},
         {"context_stems",
          std::vector<std::string>(context_stems.begin(), context_stems.end())},
         {"merge_witnesses", macro.merge_witnesses},
         {"merge_strength", macro.merge_strength},
         {"why", macro.why},
         {"nuclei", nuclei},
         {"anchors", anchor_groups},
         {"roles", roles},
         {"mechanism", mechanism},
         {"ports", ports},
         {"support_edges", support_edges},
         {"pack_meta", pack_meta},
         {"edges", edges},
         {"representatives", representatives},
         {"trails", trails},
         {"closure",
          {{"groups", macro.anchor_groups.size()},
           {"closed", closed_groups},
           {"closed_without_hubs", closed_without_hubs},
           {"max_hops", 4}}},
         {"hub_nodes", hubs},
         {"overlap_previous", overlap_previous},
         {"risks", risks},
         {"zone_nodes", macro.nodes.size()}});
  }

  nlohmann::json uncovered = nlohmann::json::array();
  for (std::size_t i = 0; i < result.seeds.size(); ++i) {
    if (static_cast<int>(uncovered.size()) >= std::max(0, opts.max_uncovered_seeds)) {
      break;
    }
    if (!explained_anchors.count(result.seeds[i].node.id)) {
      uncovered.push_back(compact_ref(result.seeds[i]));
    }
  }
  if (opts.promote_uncovered) {
    const int max_hops = 2;
    const std::unordered_set<std::string> enrich_kinds = {"write", "read", "call", "handoff"};
    for (const auto& seed : result.seeds) {
      if (static_cast<int>(zones.size()) >= std::max(0, opts.max_zones)) {
        break;
      }
      if (explained_anchors.count(seed.node.id) || seed.node.kind != "fn") {
        continue;
      }
      std::unordered_map<std::string, RegistryNodeRow> enriched_nodes;
      enriched_nodes[seed.node.id] = seed.node;
      std::deque<RegistryTrailHop> enriched_hops;
      enriched_hops.push_back(seed);
      std::vector<std::pair<std::string, int>> pending{{seed.node.id, 0}};
      std::unordered_set<std::string> seen{seed.node.id};
      std::unordered_set<std::string> enriched_stems;
      if (!seed.node.stem.empty()) {
        enriched_stems.insert(seed.node.stem);
      }
      nlohmann::json enriched_edges = nlohmann::json::array();
      nlohmann::json enriched_roles = {{"writers", nlohmann::json::array()},
                                         {"readers", nlohmann::json::array()},
                                         {"controls", nlohmann::json::array()},
                                         {"handoffs", nlohmann::json::array()}};
      std::unordered_set<std::string> role_seen;
      auto add_role_node = [&](const char* role, const RegistryNodeRow& node) {
        const std::string key = std::string(role) + "|" + node.id;
        if (!role_seen.insert(key).second || enriched_roles[role].size() >= 4) {
          return;
        }
        nlohmann::json ref = {{"id", node.id},
                              {"target", node_target(node)},
                              {"kind", node.kind},
                              {"stem", node.stem}};
        auto qit = query_rank.find(node.id);
        if (qit != query_rank.end()) {
          ref["qrank"] = qit->second;
        }
        enriched_roles[role].push_back(std::move(ref));
      };
      for (std::size_t pi = 0; pi < pending.size(); ++pi) {
        const auto [id, depth] = pending[pi];
        if (depth >= max_hops) {
          continue;
        }
        std::vector<RegistryNeighbor> neighbors;
        std::string ignored;
        if (!registry_neighbors(r, id,
                                {"write", "read", "handoff", "call", "enter_ctrl", "then",
                                 "else", "case"},
                                "both", &neighbors, &ignored)) {
          continue;
        }
        RegistryNodeRow current;
        if (!registry_get(r, id, &current, &ignored)) {
          continue;
        }
        for (const auto& neighbor : neighbors) {
          if (!enrich_kinds.count(neighbor.fact.kind)) {
            continue;
          }
          if (neighbor.node.kind == "fn" && !neighbor.node.stem.empty()) {
            enriched_stems.insert(neighbor.node.stem);
          }
          if (enriched_edges.size() < static_cast<std::size_t>(std::max(0, opts.max_edges))) {
            const std::string from_target =
                neighbor.outbound ? node_target(current) : node_target(neighbor.node);
            const std::string to_target =
                neighbor.outbound ? node_target(neighbor.node) : node_target(current);
            enriched_edges.push_back({{"from", from_target},
                                      {"kind", neighbor.fact.kind},
                                      {"to", to_target},
                                      {"member", neighbor.fact.member}});
          }
          if (neighbor.fact.kind == "write") {
            add_role_node("writers", neighbor.outbound ? current : neighbor.node);
          } else if (neighbor.fact.kind == "read") {
            add_role_node("readers", neighbor.outbound ? neighbor.node : current);
          } else if (neighbor.fact.kind == "handoff") {
            add_role_node("handoffs", neighbor.outbound ? current : neighbor.node);
          }
          if (seen.insert(neighbor.node.id).second) {
            enriched_nodes[neighbor.node.id] = neighbor.node;
            RegistryTrailHop hop;
            hop.node = neighbor.node;
            enriched_hops.push_back(hop);
            pending.push_back({neighbor.node.id, depth + 1});
          }
        }
      }

      nlohmann::json nuclei = nlohmann::json::array();
      for (const auto& c : result.constellations) {
        if (!enriched_nodes.count(c.center_id)) {
          continue;
        }
        nuclei.push_back({{"id", c.id},
                          {"state", c.member},
                          {"score", c.score},
                          {"coverage", c.mass_coverage}});
        if (!c.member.empty()) {
          const auto slash = c.member.find('/');
          const std::string stem =
              slash == std::string::npos ? c.member : c.member.substr(0, slash);
          if (!stem.empty()) {
            enriched_stems.insert(stem);
          }
        }
        break;
      }

      nlohmann::json representatives = nlohmann::json::array();
      std::unordered_set<std::string> rep_targets;
      representatives.push_back(compact_card(seed.node, true));
      rep_targets.insert(node_target(seed.node));
      for (const auto& hop : enriched_hops) {
        if (representatives.size() >= static_cast<std::size_t>(std::max(3, opts.max_representatives))) {
          break;
        }
        const std::string target = node_target(hop.node);
        if (hop.node.id == seed.node.id || !rep_targets.insert(target).second) {
          continue;
        }
        representatives.push_back(compact_card(hop.node, hop.node.id == seed.node.id));
      }

      nlohmann::json stems = nlohmann::json::array();
      for (const auto& stem : enriched_stems) {
        stems.push_back(stem);
      }
      std::vector<std::string> risks = {"promoted_from_uncovered"};
      if (nuclei.empty()) {
        risks.push_back("no_state_nucleus");
      }
      risks.push_back("uncovered_candidate");

      const std::string id = "M" + std::to_string(zones.size() + 1);
      zones.push_back(
          {{"id", id},
           {"rank", zones.size() + 1},
           {"score", std::max(0.f, seed.cosine) * 0.5f},
           {"score_margin", 0.f},
           {"mass_coverage", seed.mass},
           {"primary_stems", stems},
           {"core_stems", stems},
           {"context_stems", nlohmann::json::array()},
           {"why", "uncovered query candidate with local causal envelope"},
           {"nuclei", nuclei},
           {"anchors", nlohmann::json::array({nlohmann::json::array({compact_ref(seed)})})},
           {"roles", enriched_roles},
           {"edges", enriched_edges},
           {"representatives", representatives},
           {"trails", nlohmann::json::array()},
           {"closure", {{"groups", 1}, {"closed", 0}, {"closed_without_hubs", 0}, {"max_hops", max_hops}}},
           {"hub_nodes", nlohmann::json::array()},
           {"overlap_previous", 0.f},
           {"risks", risks},
           {"zone_nodes", enriched_nodes.size()}});
      for (const auto& [nid, row] : enriched_nodes) {
        node_to_macro_zone[nid] = id;
      }
    }
  }

  nlohmann::json zone_bridges = nlohmann::json::array();
  for (const auto& trail : result.trails) {
    std::unordered_set<std::string> bridge_zones;
    std::unordered_set<std::string> bridge_stems;
    for (const auto& hop : trail.hops) {
      if (!hop.node.stem.empty()) {
        bridge_stems.insert(hop.node.stem);
      }
      auto zit = node_to_macro_zone.find(hop.node.id);
      if (zit != node_to_macro_zone.end()) {
        bridge_zones.insert(zit->second);
      }
    }
    if (bridge_zones.size() < 2) {
      continue;
    }
    nlohmann::json linked = nlohmann::json::array();
    for (const auto& zone_id : bridge_zones) {
      linked.push_back(zone_id);
    }
    nlohmann::json stems = nlohmann::json::array();
    for (const auto& stem : bridge_stems) {
      stems.push_back(stem);
    }
    zone_bridges.push_back(
        {{"trail", trail.id}, {"zones", linked}, {"stems", stems}, {"why", trail.why}});
  }

  *out = {{"schema", "causal_judge_v1"},
          {"query", query},
          {"gate",
           {{"max_cosine", result.max_cosine},
            {"map_boosted", result.map_boosted},
            {"weak", result.weak_gate},
            {"holes", result.holes}}},
          {"zones", zones},
          {"zone_bridges", zone_bridges},
          {"uncovered_seeds", uncovered},
          {"stats",
           {{"source_nodes", result.subgraph_nodes},
            {"source_facts", result.subgraph_facts},
            {"zones", zones.size()},
            {"representatives", emitted_representatives},
            {"edges", emitted_edges}}}};
  return true;
}

bool registry_expand_causal_judge_payload(EffectRegistry* r, const nlohmann::json& base_payload,
                                          const RegistryCausalTriageDecision& triage,
                                          const RegistryCausalJudgeOpts& opts,
                                          nlohmann::json* out, std::string* err) {
  if (r == nullptr || r->db == nullptr || out == nullptr || !triage.ok) {
    set_err(err, "causal judge expansion args");
    return false;
  }
  auto node_target = [](const RegistryNodeRow& node) {
    if (!node.path.empty() && !node.symbol.empty()) {
      return node.path + ":" + node.symbol;
    }
    return !node.symbol.empty() ? node.symbol : node.id;
  };
  auto rich_card = [&](const RegistryNodeRow& node) {
    nlohmann::json result = {{"id", node.id}, {"target", node_target(node)}};
    if (!node.card_json.empty()) {
      try {
        const auto card = nlohmann::json::parse(node.card_json);
        for (const char* key : {"sig", "ctrl", "guard", "roles", "writes", "reads", "calls_seed",
                                "hot"}) {
          if (card.contains(key)) {
            result[key] = card[key];
          }
        }
        const int start = card.value("start_line", 0);
        const int finish = card.value("end_line", 0);
        if (start > 0 && finish >= start && finish - start <= 160 && !node.path.empty()) {
          std::ifstream source(fs::path(r->workspace_root) / node.path);
          std::string line;
          int line_no = 0;
          nlohmann::json outline = nlohmann::json::array();
          while (std::getline(source, line) && outline.size() < 8) {
            ++line_no;
            if (line_no < start) {
              continue;
            }
            if (line_no > finish) {
              break;
            }
            const auto first = line.find_first_not_of(" \t");
            if (first == std::string::npos) {
              continue;
            }
            std::string text = line.substr(first);
            const bool useful = text.find('(') != std::string::npos ||
                                text.find(" = ") != std::string::npos ||
                                text.rfind("return", 0) == 0;
            if (!useful || text.rfind("//", 0) == 0) {
              continue;
            }
            if (text.size() > 140) {
              text.resize(140);
              text += "…";
            }
            outline.push_back(std::to_string(line_no) + ": " + text);
          }
          if (!outline.empty()) {
            result["outline"] = std::move(outline);
          }
        }
      } catch (...) {
      }
    }
    return result;
  };
  std::unordered_map<std::string, const RegistryZoneTriage*> triage_by_zone;
  for (const auto& zone : triage.zones) {
    triage_by_zone[zone.id] = &zone;
  }
  const std::unordered_set<std::string> shortlist(triage.shortlist.begin(),
                                                  triage.shortlist.end());
  nlohmann::json expanded_zones = nlohmann::json::array();
  for (const auto& base_zone : base_payload.value("zones", nlohmann::json::array())) {
    const std::string zone_id = base_zone.value("id", "");
    if (!shortlist.count(zone_id)) {
      continue;
    }
    nlohmann::json zone = base_zone;
    std::unordered_map<std::string, std::string> target_ids;
    auto remember = [&](const nlohmann::json& item) {
      const std::string target = item.value("target", "");
      const std::string id = item.value("id", "");
      if (!target.empty() && !id.empty()) {
        target_ids[target] = id;
      }
    };
    for (const auto& group : zone.value("anchors", nlohmann::json::array())) {
      for (const auto& item : group) {
        remember(item);
      }
    }
    const nlohmann::json role_values = zone.value("roles", nlohmann::json::object());
    for (const auto& [name, values] : role_values.items()) {
      (void)name;
      if (values.is_array()) {
        for (const auto& item : values) {
          remember(item);
        }
      }
    }
    for (const auto& item : zone.value("representatives", nlohmann::json::array())) {
      remember(item);
    }
    nlohmann::json representatives = zone.value("representatives", nlohmann::json::array());
    nlohmann::json edges = zone.value("edges", nlohmann::json::array());
    std::unordered_set<std::string> allowed_stems;
    for (const char* key : {"primary_stems", "core_stems", "context_stems"}) {
      for (const auto& stem : zone.value(key, nlohmann::json::array())) {
        if (stem.is_string()) {
          allowed_stems.insert(stem.get<std::string>());
        }
      }
    }
    for (const auto& bridge : base_payload.value("zone_bridges", nlohmann::json::array())) {
      const auto linked = bridge.value("zones", nlohmann::json::array());
      bool touches = false;
      for (const auto& linked_id : linked) {
        if (linked_id.is_string() && linked_id.get<std::string>() == zone_id) {
          touches = true;
          break;
        }
      }
      if (!touches) {
        continue;
      }
      for (const auto& stem : bridge.value("stems", nlohmann::json::array())) {
        if (stem.is_string()) {
          allowed_stems.insert(stem.get<std::string>());
        }
      }
    }
    std::unordered_set<std::string> representative_targets;
    for (const auto& item : representatives) {
      representative_targets.insert(item.value("target", ""));
    }
    const auto zit = triage_by_zone.find(zone_id);
    if (zit != triage_by_zone.end()) {
      for (const auto& requested : zit->second->expand_from) {
        const auto tit = target_ids.find(requested);
        if (tit == target_ids.end()) {
          set_err(err, "target de expansión no resoluble: " + requested);
          return false;
        }
        RegistryNodeRow root_node;
        if (!registry_get(r, tit->second, &root_node, err)) {
          return false;
        }
        const nlohmann::json root_card = rich_card(root_node);
        bool replaced = false;
        for (auto& item : representatives) {
          if (item.value("target", "") == requested) {
            item = root_card;
            replaced = true;
            break;
          }
        }
        if (!replaced) {
          representatives.push_back(root_card);
          representative_targets.insert(requested);
        }
        std::vector<std::pair<std::string, int>> pending{{root_node.id, 0}};
        std::unordered_set<std::string> seen{root_node.id};
        for (std::size_t pi = 0; pi < pending.size(); ++pi) {
          const auto [id, depth] = pending[pi];
          if (depth >= std::min(2, std::max(1, opts.expand_hops))) {
            continue;
          }
          std::vector<RegistryNeighbor> neighbors;
          RegistryNodeRow current_node;
          if (!registry_get(r, id, &current_node, err)) {
            return false;
          }
          if (!registry_neighbors(r, id,
                                  {"write", "read", "handoff", "call", "enter_ctrl", "then",
                                   "else", "case"},
                                  "both", &neighbors, err)) {
            return false;
          }
          for (const auto& neighbor : neighbors) {
            if (neighbor.node.kind == "fn" && !neighbor.node.stem.empty() &&
                !allowed_stems.count(neighbor.node.stem)) {
              continue;
            }
            const std::string target = node_target(neighbor.node);
            if (edges.size() < static_cast<std::size_t>(std::max(0, opts.max_edges))) {
              edges.push_back({{"from", neighbor.outbound ? node_target(current_node) : target},
                               {"kind", neighbor.fact.kind},
                               {"to", neighbor.outbound ? target : node_target(current_node)},
                               {"member", neighbor.fact.member}});
            }
            if (neighbor.node.kind == "fn" &&
                representatives.size() <
                    static_cast<std::size_t>(std::max(0, opts.max_representatives)) &&
                representative_targets.insert(target).second) {
              representatives.push_back(rich_card(neighbor.node));
            }
            if (seen.insert(neighbor.node.id).second) {
              pending.push_back({neighbor.node.id, depth + 1});
            }
          }
        }
      }
    }
    zone["representatives"] = std::move(representatives);
    zone["edges"] = std::move(edges);
    zone["triage_need"] =
        zit == triage_by_zone.end() ? "" : zit->second->need;
    zone["risks"].push_back("directed_expansion");
    expanded_zones.push_back(std::move(zone));
  }
  *out = base_payload;
  (*out)["schema"] = "causal_judge_v1_expanded";
  (*out)["zones"] = std::move(expanded_zones);
  (*out)["triage"] = registry_causal_triage_decision_to_json(triage);
  (*out)["stats"]["zones"] = (*out)["zones"].size();
  return true;
}

void registry_apply_deterministic_co_shortlist(const nlohmann::json& base_payload,
                                               RegistryCausalTriageDecision* triage) {
  if (triage == nullptr || !triage->ok || triage->shortlist.empty()) {
    return;
  }
  const auto& zones = base_payload.value("zones", nlohmann::json::array());
  if (zones.empty()) {
    return;
  }
  std::unordered_set<std::string> selected(triage->shortlist.begin(), triage->shortlist.end());
  auto zone_entry_exists = [&](const std::string& zone_id) {
    return std::any_of(triage->zones.begin(), triage->zones.end(),
                       [&](const RegistryZoneTriage& zone) { return zone.id == zone_id; });
  };
  auto ensure_zone_entry = [&](const std::string& zone_id, const std::string& need) {
    if (zone_entry_exists(zone_id)) {
      return;
    }
    RegistryZoneTriage complement;
    complement.id = zone_id;
    complement.verdict = "inspect";
    complement.need = need;
    triage->zones.push_back(std::move(complement));
  };
  auto add_zone = [&](const std::string& zone_id, const std::string& need =
                                                      "comprobar zona enlazada por margen bajo "
                                                      "o puente causal") {
    if (zone_id.empty() || selected.count(zone_id) || selected.size() >= 3) {
      return;
    }
    selected.insert(zone_id);
    triage->shortlist.push_back(zone_id);
    ensure_zone_entry(zone_id, need);
  };
  // Incluye zona aunque el shortlist esté lleno: sustituye la última entrada que no
  // sea top-2 del payload (garantiza M2 cuando el LLM llenó con M1+distractores).
  auto force_include = [&](const std::string& zone_id, const std::string& need) {
    if (zone_id.empty() || selected.count(zone_id)) {
      return;
    }
    if (selected.size() < 3) {
      add_zone(zone_id, need);
      return;
    }
    const std::string top0 = zones[0].value("id", "");
    const std::string top1 = zones.size() > 1 ? zones[1].value("id", "") : "";
    for (int i = static_cast<int>(triage->shortlist.size()) - 1; i >= 0; --i) {
      const std::string& cand = triage->shortlist[static_cast<size_t>(i)];
      if (cand == top0 || cand == top1) {
        continue;
      }
      selected.erase(cand);
      selected.insert(zone_id);
      triage->shortlist[static_cast<size_t>(i)] = zone_id;
      ensure_zone_entry(zone_id, need);
      return;
    }
  };

  if (triage->shortlist.size() == 1 && !triage->critical_mass && zones.size() > 1) {
    add_zone(zones[1].value("id", ""));
  }

  const auto& m1 = zones[0];
  bool low_margin = m1.value("score_margin", 1.f) < 0.05f;
  for (const auto& risk : m1.value("risks", nlohmann::json::array())) {
    if (risk.is_string() && risk.get<std::string>() == "low_top_margin") {
      low_margin = true;
      break;
    }
  }
  if (low_margin && zones.size() > 1) {
    add_zone(zones[1].value("id", ""));
  }

  for (const auto& bridge : base_payload.value("zone_bridges", nlohmann::json::array())) {
    const auto linked = bridge.value("zones", nlohmann::json::array());
    bool touches = false;
    for (const auto& zone_id : linked) {
      if (zone_id.is_string() && selected.count(zone_id.get<std::string>()) > 0) {
        touches = true;
        break;
      }
    }
    if (!touches) {
      continue;
    }
    for (const auto& zone_id : linked) {
      if (zone_id.is_string()) {
        add_zone(zone_id.get<std::string>());
      }
    }
  }

  // Floor top-2: si falta el runner-up de registry, forzarlo (con eviction si hace falta).
  const std::string top0 = zones[0].value("id", "");
  const std::string top1 = zones.size() > 1 ? zones[1].value("id", "") : "";
  const bool has_top0 = !top0.empty() && selected.count(top0) > 0;
  const bool has_top1 = !top1.empty() && selected.count(top1) > 0;
  if (!has_top0 && !has_top1) {
    force_include(top1.empty() ? top0 : top1, "floor top-2 registry: ancla sin top score");
    if (!top0.empty() && selected.count(top0) == 0) {
      add_zone(top0, "floor top-2 registry");
    }
  } else if (!top1.empty() && !has_top1) {
    force_include(top1, "floor top-2 registry: garantizar runner-up");
  }

  // Complemento por overlap context(shortlist) ∩ primary(candidato).
  std::unordered_set<std::string> context_pool;
  for (const auto& zone : zones) {
    const std::string zone_id = zone.value("id", "");
    if (zone_id.empty() || selected.count(zone_id) == 0) {
      continue;
    }
    for (const auto& stem : zone.value("context_stems", nlohmann::json::array())) {
      if (stem.is_string()) {
        context_pool.insert(stem.get<std::string>());
      }
    }
  }
  if (!context_pool.empty()) {
    for (const auto& zone : zones) {
      if (selected.size() >= 3) {
        break;
      }
      const std::string zone_id = zone.value("id", "");
      if (zone_id.empty() || selected.count(zone_id) > 0) {
        continue;
      }
      bool overlap = false;
      for (const auto& stem : zone.value("primary_stems", nlohmann::json::array())) {
        if (stem.is_string() && context_pool.count(stem.get<std::string>()) > 0) {
          overlap = true;
          break;
        }
      }
      if (overlap) {
        add_zone(zone_id, "comprobar zona con primary en context del shortlist");
      }
    }
  }
}

namespace {

std::string ascii_lower_copy(std::string s) {
  for (char& ch : s) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return s;
}

void push_token_min3(std::unordered_set<std::string>* out, const std::string& tok) {
  if (out == nullptr || tok.size() < 3) {
    return;
  }
  out->insert(tok);
}

void collect_text_tokens(const std::string& text, std::unordered_set<std::string>* out) {
  if (out == nullptr || text.empty()) {
    return;
  }
  std::string cur;
  auto flush = [&]() {
    push_token_min3(out, ascii_lower_copy(cur));
    cur.clear();
  };
  for (size_t i = 0; i < text.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    if (std::isalnum(ch) || ch == '_') {
      // Split camelCase: VisualHighlight → visual, highlight
      if (!cur.empty() && std::isupper(ch) && std::islower(static_cast<unsigned char>(cur.back()))) {
        flush();
      }
      cur.push_back(static_cast<char>(ch));
    } else {
      flush();
    }
  }
  flush();
}

void collect_stem_tokens(const std::string& stem, std::unordered_set<std::string>* out) {
  if (out == nullptr || stem.empty()) {
    return;
  }
  push_token_min3(out, ascii_lower_copy(stem));
  std::string cur;
  for (char ch : stem) {
    if (ch == '_' || ch == '-' || ch == '/') {
      push_token_min3(out, ascii_lower_copy(cur));
      cur.clear();
    } else {
      cur.push_back(ch);
    }
  }
  push_token_min3(out, ascii_lower_copy(cur));
}

void collect_symbolish_tokens(const std::string& text, std::unordered_set<std::string>* out) {
  if (out == nullptr || text.empty()) {
    return;
  }
  std::string cur;
  auto flush = [&]() {
    const std::string tok = ascii_lower_copy(cur);
    cur.clear();
    if (tok.size() >= 6 && tok.find('_') != std::string::npos) {
      out->insert(tok);
    }
  };
  for (unsigned char ch : text) {
    if (std::isalnum(ch) || ch == '_') {
      cur.push_back(static_cast<char>(ch));
    } else {
      flush();
    }
  }
  flush();
}

std::string representative_symbol(const std::string& target) {
  if (target.empty()) {
    return {};
  }
  const auto colon = target.rfind(':');
  std::string sym = colon == std::string::npos ? target : target.substr(colon + 1);
  const auto slash = sym.rfind('/');
  if (slash != std::string::npos) {
    sym = sym.substr(slash + 1);
  }
  return ascii_lower_copy(sym);
}

bool text_has_negation_cue(const std::string& text) {
  const std::string lower = ascii_lower_copy(text);
  static const char* kCues[] = {
      "no maneja", "no gestiona", "no restaura", "no controla", "no es responsable",
      "falta", "does not", "doesn't", "missing", "not handle", "not manage", "cannot",
      "does_not_explain", "no cubre"};
  for (const char* cue : kCues) {
    if (lower.find(cue) != std::string::npos) {
      return true;
    }
  }
  return false;
}

int hypothesis_stem_overlap(const std::unordered_set<std::string>& hyp_tokens,
                            const nlohmann::json& primary_stems) {
  int score = 0;
  for (const auto& stem : primary_stems) {
    if (!stem.is_string()) {
      continue;
    }
    std::unordered_set<std::string> stem_tokens;
    collect_stem_tokens(stem.get<std::string>(), &stem_tokens);
    for (const auto& tok : stem_tokens) {
      if (hyp_tokens.count(tok) > 0) {
        ++score;
        break;
      }
    }
  }
  return score;
}

int hypothesis_representative_overlap(const std::unordered_set<std::string>& symbolish,
                                      const nlohmann::json& representatives,
                                      bool negate_matched) {
  if (symbolish.empty()) {
    return 0;
  }
  int score = 0;
  for (const auto& rep : representatives) {
    if (!rep.is_object()) {
      continue;
    }
    const std::string sym = representative_symbol(rep.value("target", ""));
    if (sym.size() < 6) {
      continue;
    }
    bool hit = symbolish.count(sym) > 0;
    if (!hit) {
      for (const auto& tok : symbolish) {
        if (sym.find(tok) != std::string::npos || tok.find(sym) != std::string::npos) {
          hit = true;
          break;
        }
      }
    }
    if (!hit) {
      continue;
    }
    score += negate_matched ? -2 : 2;
  }
  return score;
}

}  // namespace

void registry_apply_synth_hypothesis_tiebreak(const nlohmann::json& expanded_payload,
                                              const std::string& hypothesis,
                                              const std::string& anchor_why,
                                              RegistryCausalJudgeDecision* decision) {
  if (decision == nullptr || !decision->ok || decision->selected.empty()) {
    return;
  }
  const auto& zones = expanded_payload.value("zones", nlohmann::json::array());
  if (zones.size() < 2) {
    return;
  }
  std::unordered_map<std::string, const nlohmann::json*> by_id;
  for (const auto& zone : zones) {
    const std::string id = zone.value("id", "");
    if (!id.empty()) {
      by_id[id] = &zone;
    }
  }
  if (by_id.size() < 2) {
    return;
  }

  const std::string probe = hypothesis + "\n" + anchor_why;
  std::unordered_set<std::string> hyp_tokens;
  collect_text_tokens(probe, &hyp_tokens);
  std::unordered_set<std::string> symbolish;
  collect_symbolish_tokens(probe, &symbolish);
  const bool negate = text_has_negation_cue(probe);

  struct ZoneScore {
    std::string id;
    int overlap = 0;
    float mass = 0.f;
  };
  std::vector<ZoneScore> ranked;
  ranked.reserve(by_id.size());
  for (const auto& [id, zone] : by_id) {
    ZoneScore row;
    row.id = id;
    row.overlap =
        hypothesis_stem_overlap(hyp_tokens, (*zone).value("primary_stems", nlohmann::json::array())) +
        hypothesis_representative_overlap(
            symbolish, (*zone).value("representatives", nlohmann::json::array()), negate);
    row.mass = (*zone).value("mass_coverage", 0.f);
    ranked.push_back(std::move(row));
  }
  // Prefer higher overlap; on ties prefer lower mass (more specific zone).
  std::sort(ranked.begin(), ranked.end(), [](const ZoneScore& a, const ZoneScore& b) {
    if (a.overlap != b.overlap) {
      return a.overlap > b.overlap;
    }
    return a.mass < b.mass;
  });
  const ZoneScore& best = ranked.front();
  const std::string current_id = decision->selected.front();
  auto current_it = std::find_if(ranked.begin(), ranked.end(),
                                 [&](const ZoneScore& row) { return row.id == current_id; });
  if (current_it == ranked.end()) {
    return;
  }
  const bool worse_overlap = current_it->overlap < best.overlap;
  const bool same_overlap_prefer_specificity =
      current_it->overlap == best.overlap && current_it->mass > best.mass + 1e-6f;
  if (!worse_overlap && !same_overlap_prefer_specificity) {
    return;
  }
  if (best.id == current_id) {
    return;
  }

  for (auto& zone : decision->zones) {
    if (zone.id == best.id) {
      zone.verdict = "select";
      zone.role = zone.role.empty() || zone.role == "none" ? "primary" : zone.role;
      zone.completeness = zone.completeness == "none" || zone.completeness.empty()
                              ? "partial"
                              : zone.completeness;
      zone.confidence = std::max(0.55f, zone.confidence);
      if (zone.why.size() < 12) {
        zone.why = "tie-break: mejor overlap hypothesis↔stems/reps";
      }
    } else if (zone.verdict == "select") {
      zone.verdict = "reject";
      zone.role = "none";
      zone.completeness = "none";
      if (zone.why.size() < 12) {
        zone.why = "desplazada por tie-break hypothesis";
      }
    }
  }
  bool touched = false;
  for (const auto& zone : decision->zones) {
    if (zone.id == best.id) {
      touched = true;
      break;
    }
  }
  if (!touched) {
    RegistryZoneVerdict zone;
    zone.id = best.id;
    zone.verdict = "select";
    zone.role = "primary";
    zone.completeness = "partial";
    zone.confidence = 0.55f;
    zone.why = "tie-break: mejor overlap hypothesis↔stems/reps";
    zone.contribution = zone.why;
    decision->zones.push_back(std::move(zone));
  }
  decision->selected = {best.id};
  if (decision->why.size() < 12) {
    decision->why = "tie-break hypothesis↔stems/reps → " + best.id;
  }
}

std::string registry_causal_triage_markdown(const nlohmann::json& payload) {
  std::ostringstream out;
  out << "# causal_zone_triage_v1\n";
  out << "query: " << payload.value("query", "") << "\n";
  const auto gate = payload.value("gate", nlohmann::json::object());
  out << "gate: cosine=" << gate.value("max_cosine", 0.f)
      << " map=" << gate.value("map_boosted", 0)
      << " weak=" << (gate.value("weak", false) ? "yes" : "no") << "\n";
  for (const auto& zone : payload.value("zones", nlohmann::json::array())) {
    out << "\n## " << zone.value("id", "?") << " score=" << zone.value("score", 0.f)
        << " coverage=" << zone.value("mass_coverage", 0.f) << "\n";
    out << "stems:";
    for (const auto& stem : zone.value("primary_stems", nlohmann::json::array())) {
      if (stem.is_string()) {
        out << " " << stem.get<std::string>();
      }
    }
    out << "\ncore:";
    for (const auto& stem : zone.value("core_stems", nlohmann::json::array())) {
      if (stem.is_string()) {
        out << " " << stem.get<std::string>();
      }
    }
    out << "\ncontext:";
    for (const auto& stem : zone.value("context_stems", nlohmann::json::array())) {
      if (stem.is_string()) {
        out << " " << stem.get<std::string>();
      }
    }
    out << "\nnuclei:";
    for (const auto& nucleus : zone.value("nuclei", nlohmann::json::array())) {
      out << " " << nucleus.value("id", "?") << "(" << nucleus.value("state", "?") << ")";
    }
    out << "\nanchors:";
    for (const auto& group : zone.value("anchors", nlohmann::json::array())) {
      for (const auto& anchor : group) {
        out << " " << anchor.value("target", "");
      }
    }
    out << "\nroles:";
    const auto roles = zone.value("roles", nlohmann::json::object());
    for (const char* role : {"writers", "readers", "controls", "handoffs"}) {
      for (const auto& item : roles.value(role, nlohmann::json::array())) {
        out << " " << item.value("target", "");
      }
    }
    out << "\nedges:\n";
    int edge_n = 0;
    for (const auto& edge : zone.value("edges", nlohmann::json::array())) {
      if (edge_n++ >= 4) {
        break;
      }
      out << "- " << edge.value("from", "") << " -" << edge.value("kind", "") << "-> "
          << edge.value("to", "") << "\n";
    }
    out << "targets:";
    int target_n = 0;
    std::unordered_set<std::string> target_seen;
    auto emit_target = [&](const std::string& target) {
      if (!target.empty() && target_n < 12 && target_seen.insert(target).second) {
        out << " " << target;
        ++target_n;
      }
    };
    for (const auto& rep : zone.value("representatives", nlohmann::json::array())) {
      emit_target(rep.value("target", ""));
    }
    for (const auto& group : zone.value("anchors", nlohmann::json::array())) {
      for (const auto& anchor : group) {
        emit_target(anchor.value("target", ""));
      }
    }
    out << "\nrisks:";
    for (const auto& risk : zone.value("risks", nlohmann::json::array())) {
      if (risk.is_string()) {
        out << " " << risk.get<std::string>();
      }
    }
    out << "\n";
  }
  const auto bridges = payload.value("zone_bridges", nlohmann::json::array());
  if (!bridges.empty()) {
    out << "\n## zone bridges\n";
    for (const auto& bridge : bridges) {
      out << "- " << bridge.value("trail", "?") << ": ";
      for (const auto& zone_id : bridge.value("zones", nlohmann::json::array())) {
        if (zone_id.is_string()) {
          out << zone_id.get<std::string>() << " ";
        }
      }
      out << "stems=";
      for (const auto& stem : bridge.value("stems", nlohmann::json::array())) {
        if (stem.is_string()) {
          out << stem.get<std::string>() << ",";
        }
      }
      out << "\n";
    }
  }
  out << "\n## uncovered seeds\n";
  for (const auto& seed : payload.value("uncovered_seeds", nlohmann::json::array())) {
    out << "- " << seed.value("target", "") << " q" << seed.value("qrank", -1) << "\n";
  }
  return out.str();
}

std::string registry_causal_triage_system_prompt() {
  return R"(Eres un triage causal de código. Recibes zonas ligeras y semillas todavía no cubiertas.
No elijas por score ni por coincidencia de palabras: busca el mecanismo solicitado.
Inspecciona hasta 3 zonas complementarias si la consulta contiene varias piezas (por ejemplo estado,
trigger, cancelación o consumidor). Usa expand_from únicamente con targets exactos visibles dentro del
M* indicado. Si ninguna zona contiene el mecanismo, activa retrieval_needed.
Devuelve solo este JSON compacto; las zonas omitidas se consideran rechazadas:
{"action":"causal_zone_triage_v1","inspect":[{"id":"M2","need":"qué eslabón comprobar","expand_from":["target exacto"]}],"retrieval_needed":false,"why":"razón concreta"}
Cada inspect requiere id M*, need concreto de 1-160 caracteres y como máximo 4 targets.)";
}

std::string registry_causal_triage_user_prompt(const std::string& cards_markdown) {
  std::ostringstream out;
  out << "Evalúa todas las zonas. La primera pasada no decide la solución: escoge qué evidencia ampliar.\n"
      << "No inventes targets ni uses conocimiento externo a estas fichas.\n\n"
      << cards_markdown;
  return out.str();
}

std::string registry_causal_anchor_system_prompt() {
  return R"(Eres un buscador de ANCLAS causales en código, no un ranking de temas relacionados.
Recibes fichas de zonas (el grafo ya empaquetó evidencia densa). Tu trabajo:
1) Elegir 1–2 anclas cuyo MECANISMO podría causar o formar parte necesaria del síntoma.
2) Formar una hipótesis breve solo si hay masa crítica para tirar de un hilo.
3) Indicar qué expandir (targets exactos visibles en ESA ficha) para comprobar la hipótesis.

No apiles zonas "relacionadas" por léxico o score. La segunda ancla solo si es un brazo causal distinto
(trigger vs state_owner vs cleanup vs consumer). Si no hay ancla útil: retrieval_needed=true y anchors=[].
Devuelve SOLO este JSON compacto:
{"action":"causal_zone_anchor_v1","anchors":[{"id":"M2","role_guess":"state_owner","explains":"qué cubre","does_not_explain":"qué falta","expand_from":["target exacto"],"thread":"qué eslabón tirar"}],"hypothesis":"causa plausible en una frase","critical_mass":true,"retrieval_needed":false,"why":"razón concreta"}
Máximo 2 anchors. explains/does_not_explain/thread/hypothesis: 12–160 chars. expand_from: 1–4 targets de esa ficha.
critical_mass=true solo si la hipótesis ya justifica expandir; si es sondeo débil, critical_mass=false.)";
}

std::string registry_causal_anchor_user_prompt(const std::string& cards_markdown,
                                              const std::string& reopen_need) {
  std::ostringstream out;
  out << "Busca anclas causales, no temas vecinos. No inventes targets ni uses conocimiento externo.\n";
  if (!reopen_need.empty()) {
    out << "REAPERTURA: la hipótesis previa se falsificó. Busca un hilo distinto. Necesidad: "
        << reopen_need << "\n";
  }
  out << "\n" << cards_markdown;
  return out.str();
}

namespace {

void truncate_utf8(std::string* text, std::size_t max_bytes) {
  if (text == nullptr || text->size() <= max_bytes) {
    return;
  }
  text->resize(max_bytes);
  while (!text->empty()) {
    const auto c = static_cast<unsigned char>(text->back());
    if ((c & 0xc0) != 0x80) {
      if ((c & 0x80) == 0) {
        break;
      }
      text->pop_back();
      break;
    }
    text->pop_back();
  }
}

std::string utf8_sanitize_clip(const std::string& s, std::size_t max_bytes) {
  std::string out;
  out.reserve(std::min(s.size(), max_bytes));
  for (std::size_t i = 0; i < s.size() && out.size() < max_bytes;) {
    const auto c = static_cast<unsigned char>(s[i]);
    std::size_t need = 1;
    if ((c & 0x80) == 0) {
      need = 1;
    } else if ((c & 0xe0) == 0xc0) {
      need = 2;
    } else if ((c & 0xf0) == 0xe0) {
      need = 3;
    } else if ((c & 0xf8) == 0xf0) {
      need = 4;
    } else {
      ++i;
      continue;
    }
    if (i + need > s.size() || out.size() + need > max_bytes) {
      break;
    }
    bool ok = true;
    for (std::size_t j = 1; j < need; ++j) {
      if ((static_cast<unsigned char>(s[i + j]) & 0xc0) != 0x80) {
        ok = false;
        break;
      }
    }
    if (!ok) {
      ++i;
      continue;
    }
    out.append(s, i, need);
    i += need;
  }
  return out;
}

bool fill_zone_expand_from(RegistryZoneTriage* zone, const nlohmann::json& item,
                           const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets,
                           const std::unordered_set<std::string>& allowed, std::string* error) {
  if (item.contains("expand_from") && item["expand_from"].is_array()) {
    for (const auto& value : item["expand_from"]) {
      if (value.is_string()) {
        zone->expand_from.push_back(value.get<std::string>());
      }
    }
  }
  if (!zone->expand_from.empty()) {
    std::string inferred;
    for (const auto& target : zone->expand_from) {
      std::string owner;
      for (const auto& [candidate, targets] : allowed_targets) {
        if (std::find(targets.begin(), targets.end(), target) != targets.end()) {
          if (!owner.empty() && owner != candidate) {
            owner.clear();
            break;
          }
          owner = candidate;
        }
      }
      if (owner.empty() || (!inferred.empty() && inferred != owner)) {
        inferred.clear();
        break;
      }
      inferred = owner;
    }
    if (!inferred.empty()) {
      zone->id = inferred;
    }
  }
  if (!allowed.count(zone->id) || zone->expand_from.size() > 4) {
    *error = "inspect inválido para " + zone->id;
    return false;
  }
  const auto ait = allowed_targets.find(zone->id);
  std::unordered_set<std::string> targets;
  if (ait != allowed_targets.end()) {
    targets.insert(ait->second.begin(), ait->second.end());
  }
  for (const auto& target : zone->expand_from) {
    if (!targets.count(target)) {
      *error = "target de expansión ajeno a " + zone->id + ": " + target;
      return false;
    }
  }
  return true;
}

}  // namespace

RegistryCausalTriageDecision registry_parse_causal_triage_decision(
    const std::string& raw, const std::vector<std::string>& allowed_zone_ids,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets) {
  RegistryCausalTriageDecision out;
  out.raw = raw;
  out.action = "causal_zone_triage_v1";
  const auto begin = raw.find('{');
  const auto end = raw.rfind('}');
  if (begin == std::string::npos || end == std::string::npos || end <= begin) {
    out.error = "triage sin objeto JSON";
    return out;
  }
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(raw.substr(begin, end - begin + 1));
  } catch (const std::exception& e) {
    out.error = std::string("JSON triage inválido: ") + e.what();
    return out;
  }
  if (j.value("action", "") != "causal_zone_triage_v1") {
    out.error = "contrato causal_zone_triage_v1 inválido";
    return out;
  }
  const std::unordered_set<std::string> allowed(allowed_zone_ids.begin(), allowed_zone_ids.end());
  std::unordered_set<std::string> inspected;
  std::vector<std::pair<std::string, nlohmann::json>> items;
  if (j.contains("inspect") && j["inspect"].is_array()) {
    for (const auto& item : j["inspect"]) {
      if (item.is_object()) {
        items.push_back({item.value("id", ""), item});
      }
    }
  } else if (j.contains("zones") && j["zones"].is_object()) {
    for (auto it = j["zones"].begin(); it != j["zones"].end(); ++it) {
      if (it.value().is_object() && it.value().value("verdict", "") == "inspect") {
        items.push_back({it.key(), it.value()});
      }
    }
  } else {
    out.error = "triage sin inspect";
    return out;
  }
  for (const auto& [declared_id, item] : items) {
    RegistryZoneTriage zone;
    zone.id = declared_id;
    zone.verdict = "inspect";
    zone.need = item.value("need", "");
    std::string expand_error;
    if (!fill_zone_expand_from(&zone, item, allowed_targets, allowed, &expand_error)) {
      out.error = expand_error;
      return out;
    }
    if (zone.need.empty() || zone.need.size() > 160) {
      out.error = "inspect inválido para " + zone.id;
      return out;
    }
    if (!inspected.insert(zone.id).second) {
      continue;
    }
    out.shortlist.push_back(zone.id);
    out.zones.push_back(std::move(zone));
  }
  if (out.shortlist.size() > 3) {
    out.error = "shortlist de triage demasiado grande";
    return out;
  }
  out.retrieval_needed = j.value("retrieval_needed", false);
  out.why = j.value("why", "");
  if (out.why.size() < 8 || out.why.size() > 240 ||
      (out.shortlist.empty() && !out.retrieval_needed)) {
    out.error = "resultado de triage no accionable";
    return out;
  }
  out.ok = true;
  return out;
}

RegistryCausalTriageDecision registry_parse_causal_atlas_survey(
    const std::string& raw, const std::vector<std::string>& allowed_zone_ids,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets) {
  RegistryCausalTriageDecision out;
  out.raw = raw;
  out.action = "causal_atlas_survey_v1";
  out.view = "inspect";
  const auto begin = raw.find('{');
  const auto end = raw.rfind('}');
  if (begin == std::string::npos || end == std::string::npos || end <= begin) {
    out.error = "atlas survey sin objeto JSON";
    return out;
  }
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(raw.substr(begin, end - begin + 1));
  } catch (const std::exception& e) {
    out.error = std::string("JSON atlas survey inválido: ") + e.what();
    return out;
  }
  const std::string action = j.value("action", "");
  if (action != "causal_atlas_survey_v1" && action != "causal_zone_triage_v1") {
    out.error = "contrato causal_atlas_survey_v1 inválido";
    return out;
  }
  const std::string view = j.value("view", "inspect");
  if (view == "deep" || view == "profunda") {
    out.view = "deep";
  } else {
    out.view = "inspect";
  }
  const std::unordered_set<std::string> allowed(allowed_zone_ids.begin(), allowed_zone_ids.end());
  std::unordered_set<std::string> inspected;
  std::vector<std::pair<std::string, nlohmann::json>> items;
  if (j.contains("inspect") && j["inspect"].is_array()) {
    for (const auto& item : j["inspect"]) {
      if (item.is_string()) {
        items.push_back({item.get<std::string>(), nlohmann::json::object()});
      } else if (item.is_object()) {
        items.push_back({item.value("id", ""), item});
      }
    }
  } else if (j.contains("zones") && j["zones"].is_object()) {
    for (auto it = j["zones"].begin(); it != j["zones"].end(); ++it) {
      if (it.value().is_object() && it.value().value("verdict", "") == "inspect") {
        items.push_back({it.key(), it.value()});
      }
    }
  }
  if (items.size() > 3) {
    items.resize(3);
  }
  for (const auto& [declared_id, item] : items) {
    RegistryZoneTriage zone;
    zone.id = declared_id;
    zone.verdict = "inspect";
    zone.need = item.value("need", "");
    if (zone.need.empty()) {
      zone.need = "ampliar mecanismo de esta zona";
    }
    std::string expand_error;
    if (!fill_zone_expand_from(&zone, item, allowed_targets, allowed, &expand_error)) {
      out.error = expand_error;
      return out;
    }
    if (zone.need.size() > 160) {
      out.error = "inspect inválido para " + zone.id;
      return out;
    }
    if (!inspected.insert(zone.id).second) {
      continue;
    }
    out.shortlist.push_back(zone.id);
    out.zones.push_back(std::move(zone));
  }
  out.retrieval_needed = j.value("retrieval_needed", false);
  out.why = j.value("why", "");
  if (out.why.size() < 8 && !out.shortlist.empty()) {
    out.why = "inspeccionar hilos del atlas";
  }
  if (out.why.size() < 8 || out.why.size() > 240 ||
      (out.shortlist.empty() && !out.retrieval_needed)) {
    out.error = "resultado de atlas survey no accionable";
    return out;
  }
  out.ok = true;
  return out;
}

RegistryCausalTriageDecision registry_parse_causal_anchor_decision(
    const std::string& raw, const std::vector<std::string>& allowed_zone_ids,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets) {
  RegistryCausalTriageDecision out;
  out.raw = raw;
  out.action = "causal_zone_anchor_v1";
  std::string cleaned = raw;
  const auto fence = cleaned.find("```");
  if (fence != std::string::npos) {
    const auto nl = cleaned.find('\n', fence);
    if (nl != std::string::npos) {
      cleaned = cleaned.substr(nl + 1);
    }
    const auto fence_end = cleaned.rfind("```");
    if (fence_end != std::string::npos) {
      cleaned.resize(fence_end);
    }
  }
  auto extract_balanced = [](const std::string& text) -> std::string {
    const auto begin = text.find('{');
    if (begin == std::string::npos) {
      return {};
    }
    int depth = 0;
    bool in_string = false;
    bool escape = false;
    for (std::size_t i = begin; i < text.size(); ++i) {
      const char c = text[i];
      if (in_string) {
        if (escape) {
          escape = false;
        } else if (c == '\\') {
          escape = true;
        } else if (c == '"') {
          in_string = false;
        }
        continue;
      }
      if (c == '"') {
        in_string = true;
      } else if (c == '{') {
        ++depth;
      } else if (c == '}') {
        --depth;
        if (depth == 0) {
          return text.substr(begin, i - begin + 1);
        }
      }
    }
    return {};
  };
  std::string payload = extract_balanced(cleaned);
  nlohmann::json j;
  if (!payload.empty()) {
    try {
      j = nlohmann::json::parse(payload);
    } catch (...) {
      payload.clear();
    }
  }
  // Truncación frecuente del 7B: recuperar objetos anchor completos.
  if (payload.empty() || !j.is_object()) {
    nlohmann::json anchors = nlohmann::json::array();
    const auto apos = cleaned.find("\"anchors\"");
    if (apos != std::string::npos) {
      const auto bracket = cleaned.find('[', apos);
      if (bracket != std::string::npos) {
        int depth = 0;
        bool in_string = false;
        bool escape = false;
        std::size_t obj_begin = std::string::npos;
        for (std::size_t i = bracket + 1; i < cleaned.size(); ++i) {
          const char c = cleaned[i];
          if (in_string) {
            if (escape) {
              escape = false;
            } else if (c == '\\') {
              escape = true;
            } else if (c == '"') {
              in_string = false;
            }
            continue;
          }
          if (c == '"') {
            in_string = true;
          } else if (c == '{') {
            if (depth == 0) {
              obj_begin = i;
            }
            ++depth;
          } else if (c == '}') {
            --depth;
            if (depth == 0 && obj_begin != std::string::npos) {
              try {
                anchors.push_back(nlohmann::json::parse(
                    cleaned.substr(obj_begin, i - obj_begin + 1)));
              } catch (...) {
              }
              obj_begin = std::string::npos;
            }
          } else if (c == ']' && depth == 0) {
            break;
          }
        }
      }
    }
    if (anchors.empty()) {
      out.error = "JSON ancla inválido o truncado";
      return out;
    }
    j = {{"action", "causal_zone_anchor_v1"},
         {"anchors", anchors},
         {"hypothesis", ""},
         {"critical_mass", true},
         {"retrieval_needed", false},
         {"why", ""}};
    auto grab_string = [&](const char* key) {
      const std::string needle = std::string("\"") + key + "\"";
      const auto pos = cleaned.find(needle);
      if (pos == std::string::npos) {
        return std::string();
      }
      const auto colon = cleaned.find(':', pos);
      const auto q1 = cleaned.find('"', colon);
      if (q1 == std::string::npos) {
        return std::string();
      }
      std::string value;
      for (std::size_t i = q1 + 1; i < cleaned.size(); ++i) {
        if (cleaned[i] == '\\' && i + 1 < cleaned.size()) {
          value.push_back(cleaned[i + 1]);
          ++i;
          continue;
        }
        if (cleaned[i] == '"') {
          break;
        }
        value.push_back(cleaned[i]);
      }
      return value;
    };
    j["hypothesis"] = grab_string("hypothesis");
    j["why"] = grab_string("why");
  }
  if (j.value("action", "") != "causal_zone_anchor_v1") {
    out.error = "contrato causal_zone_anchor_v1 inválido";
    return out;
  }
  const std::unordered_set<std::string> allowed(allowed_zone_ids.begin(), allowed_zone_ids.end());
  std::unordered_set<std::string> seen;
  if (!j.contains("anchors") || !j["anchors"].is_array()) {
    out.error = "ancla sin anchors";
    return out;
  }
  auto is_weak_phrase = [](const std::string& text) {
    return text == "qué cubre" || text == "que cubre" || text == "qué falta" ||
           text == "que falta" || text == "qué eslabón tirar" || text == "que eslabon tirar" ||
           text == "razón concreta";
  };
  for (const auto& item : j["anchors"]) {
    if (!item.is_object()) {
      out.error = "anchor no es objeto";
      return out;
    }
    RegistryZoneTriage zone;
    zone.id = item.value("id", "");
    zone.verdict = "anchor";
    zone.role_guess = item.value("role_guess", "");
    zone.explains = item.value("explains", "");
    zone.does_not_explain = item.value("does_not_explain", "");
    zone.thread = item.value("thread", "");
    if (is_weak_phrase(zone.explains)) {
      zone.explains = "mecanismo candidato en " + zone.id;
    }
    if (is_weak_phrase(zone.does_not_explain)) {
      zone.does_not_explain = "falta confirmar eslabón causal";
    }
    if (is_weak_phrase(zone.thread) || zone.thread.size() < 8) {
      zone.thread = zone.explains;
    }
    zone.need = zone.thread.empty() ? zone.explains : zone.thread;
    std::string expand_error;
    if (!fill_zone_expand_from(&zone, item, allowed_targets, allowed, &expand_error)) {
      out.error = expand_error;
      return out;
    }
    if (zone.explains.size() < 8 || zone.explains.size() > 160 ||
        zone.does_not_explain.size() < 8 || zone.does_not_explain.size() > 160 ||
        zone.thread.size() < 8 || zone.thread.size() > 160 || zone.expand_from.empty()) {
      out.error = "anchor incompleto para " + zone.id;
      return out;
    }
    if (!zone.role_guess.empty() && zone.role_guess != "primary" &&
        zone.role_guess != "trigger" && zone.role_guess != "state_owner" &&
        zone.role_guess != "cleanup" && zone.role_guess != "consumer" &&
        zone.role_guess != "boundary") {
      out.error = "role_guess inválido para " + zone.id;
      return out;
    }
    if (!seen.insert(zone.id).second) {
      continue;
    }
    out.shortlist.push_back(zone.id);
    out.zones.push_back(std::move(zone));
  }
  if (out.shortlist.size() > 2) {
    out.error = "demasiadas anclas";
    return out;
  }
  out.hypothesis = j.value("hypothesis", "");
  out.critical_mass = j.value("critical_mass", !out.shortlist.empty());
  out.retrieval_needed = j.value("retrieval_needed", false);
  out.why = j.value("why", "");
  if (out.why.size() < 8 && !out.hypothesis.empty()) {
    out.why = out.hypothesis;
  }
  if (out.why.size() < 8 && !out.shortlist.empty()) {
    out.why = "anclas recuperadas: " + out.shortlist.front();
  }
  if (out.why.size() > 240) {
    truncate_utf8(&out.why, 240);
  }
  if (out.why.size() < 8) {
    out.error = "why de ancla inválido";
    return out;
  }
  if (out.shortlist.empty()) {
    if (!out.retrieval_needed) {
      out.error = "sin anclas ni retrieval";
      return out;
    }
  } else if (out.hypothesis.size() < 12) {
    out.hypothesis = out.why;
  }
  if (!out.shortlist.empty() && (out.hypothesis.size() < 12 || out.hypothesis.size() > 200)) {
    if (out.hypothesis.size() > 200) {
      truncate_utf8(&out.hypothesis, 200);
    }
    if (out.hypothesis.size() < 12) {
      out.error = "hypothesis inválida";
      return out;
    }
  }
  out.ok = true;
  return out;
}

nlohmann::json registry_causal_triage_decision_to_json(
    const RegistryCausalTriageDecision& decision) {
  nlohmann::json zones = nlohmann::json::array();
  for (const auto& zone : decision.zones) {
    zones.push_back({{"id", zone.id},
                     {"verdict", zone.verdict},
                     {"need", zone.need},
                     {"explains", zone.explains},
                     {"does_not_explain", zone.does_not_explain},
                     {"thread", zone.thread},
                     {"role_guess", zone.role_guess},
                     {"expand_from", zone.expand_from}});
  }
  const std::string action =
      decision.action.empty() ? "causal_zone_triage_v1" : decision.action;
  return {{"action", action},
          {"ok", decision.ok},
          {"zones", zones},
          {"shortlist", decision.shortlist},
          {"hypothesis", decision.hypothesis},
          {"critical_mass", decision.critical_mass},
          {"retrieval_needed", decision.retrieval_needed},
          {"view", decision.view},
          {"why", decision.why},
          {"error", decision.error}};
}

std::string registry_causal_primary_survey_system_prompt() {
  return R"(Eres un encuestador de HIPÓTESIS GLOBALES con primaria rotativa.
Recibes fichas de zonas. Para CADA zona M* del mazo debes hacer UNA de estas dos cosas:
1) discard=true: esa zona NO puede ser primary del síntoma (solo léxico, infra, o sin mecanismo).
2) discard=false: asume que ESA zona es primary y escribe UNA hipótesis GLOBAL del síntoma
   centrada en ella. Las demás zonas solo pueden aparecer como supporting con rol
   (trigger|state_owner|cleanup|consumer|boundary).

Reglas:
- PROHIBIDO copiar la misma hypothesis cambiando solo el id. El mecanismo debe anclarse a
  stems/targets VISIBLES de la zona primary.
- supporting: 0–2 zonas distintas de la primary, roles causalmente distintos.
- expand_from: 1–3 targets exactos de la ficha primary (si discard=false).
- confidence: número 0.05–1 (nunca texto).
- Si no puedes formular un mecanismo distinto anclado a la primary: discard.

Devuelve SOLO JSON:
{"action":"causal_zone_primary_survey_v1","zones":[{"id":"M1","discard":false,"confidence":0.7,"hypothesis":"hyp global centrada en M1","supporting":[{"id":"M3","role":"trigger"}],"expand_from":["target exacto"]},{"id":"M2","discard":true,"discard_reason":"solo coincidencia léxica","confidence":0.1}]}
Debe haber exactamente un objeto por cada zona del mazo. hypothesis/discard_reason: 12–160 chars.)";
}

std::string registry_causal_primary_survey_user_prompt(
    const std::string& cards_markdown, const std::vector<std::string>& required_zone_ids) {
  std::ostringstream out;
  out << "Encuesta primary-rotativa: una hyp GLOBAL por zona como primary, o discard.\n";
  out << "No inventes targets. No reutilices la misma hyp con otro id.\n";
  if (!required_zone_ids.empty()) {
    out << "OBLIGATORIO: responde SOLO un objeto por cada una de estas zonas (todas):";
    for (const auto& id : required_zone_ids) {
      out << " " << id;
    }
    out << ".\nNo omitas ninguna de esa lista. Puedes usar el resto del mazo solo como supporting.\n";
  } else {
    out << "Debe haber exactamente un objeto por cada zona del mazo.\n";
  }
  out << "\n" << cards_markdown;
  return out.str();
}

namespace {

std::string extract_balanced_json_object(const std::string& text) {
  const auto begin = text.find('{');
  if (begin == std::string::npos) {
    return {};
  }
  int depth = 0;
  bool in_string = false;
  bool escape = false;
  for (std::size_t i = begin; i < text.size(); ++i) {
    const char c = text[i];
    if (in_string) {
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
    } else if (c == '{') {
      ++depth;
    } else if (c == '}') {
      --depth;
      if (depth == 0) {
        return text.substr(begin, i - begin + 1);
      }
    }
  }
  return {};
}

}  // namespace

RegistryPrimarySurveyDecision registry_parse_causal_primary_survey(
    const std::string& raw, const std::vector<std::string>& allowed_zone_ids,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets) {
  RegistryPrimarySurveyDecision out;
  out.raw = raw;
  out.action = "causal_zone_primary_survey_v1";
  std::string cleaned = raw;
  const auto fence = cleaned.find("```");
  if (fence != std::string::npos) {
    cleaned = cleaned.substr(fence);
    const auto nl = cleaned.find('\n');
    if (nl != std::string::npos) {
      cleaned = cleaned.substr(nl + 1);
    }
    const auto end_fence = cleaned.rfind("```");
    if (end_fence != std::string::npos) {
      cleaned.resize(end_fence);
    }
  }
  nlohmann::json j;
  const std::string payload = extract_balanced_json_object(cleaned);
  if (!payload.empty()) {
    try {
      j = nlohmann::json::parse(payload);
    } catch (...) {
      j = nlohmann::json();
    }
  }
  // Salvamento: objetos de zona sueltos si el JSON raíz truncó.
  if (!j.is_object() || !j.contains("zones")) {
    nlohmann::json zones = nlohmann::json::array();
    const auto zpos = cleaned.find("\"zones\"");
    if (zpos != std::string::npos) {
      const auto bracket = cleaned.find('[', zpos);
      if (bracket != std::string::npos) {
        int depth = 0;
        bool in_string = false;
        bool escape = false;
        std::size_t obj_begin = std::string::npos;
        for (std::size_t i = bracket + 1; i < cleaned.size(); ++i) {
          const char c = cleaned[i];
          if (in_string) {
            if (escape) {
              escape = false;
            } else if (c == '\\') {
              escape = true;
            } else if (c == '"') {
              in_string = false;
            }
            continue;
          }
          if (c == '"') {
            in_string = true;
          } else if (c == '{') {
            if (depth == 0) {
              obj_begin = i;
            }
            ++depth;
          } else if (c == '}') {
            --depth;
            if (depth == 0 && obj_begin != std::string::npos) {
              try {
                zones.push_back(nlohmann::json::parse(
                    cleaned.substr(obj_begin, i - obj_begin + 1)));
              } catch (...) {
              }
              obj_begin = std::string::npos;
            }
          } else if (c == ']' && depth == 0) {
            break;
          }
        }
      }
    }
    if (zones.empty()) {
      out.error = "JSON primary survey inválido o truncado";
      return out;
    }
    j = {{"action", "causal_zone_primary_survey_v1"}, {"zones", zones}};
  }
  if (j.value("action", "") != "causal_zone_primary_survey_v1") {
    // tolerar si zones está bien formado
    if (!j.contains("zones")) {
      out.error = "contrato causal_zone_primary_survey_v1 inválido";
      return out;
    }
  }
  const std::unordered_set<std::string> allowed(allowed_zone_ids.begin(), allowed_zone_ids.end());
  std::unordered_set<std::string> seen;
  if (!j["zones"].is_array()) {
    out.error = "zones no es array";
    return out;
  }
  for (const auto& item : j["zones"]) {
    if (!item.is_object()) {
      continue;
    }
    RegistryPrimarySurveyEntry entry;
    entry.id = item.value("id", "");
    if (entry.id.empty() || !allowed.count(entry.id) || seen.count(entry.id)) {
      continue;
    }
    seen.insert(entry.id);
    entry.discard = item.value("discard", false);
    entry.confidence = item.value("confidence", 0.f);
    if (entry.confidence < 0.05f) {
      entry.confidence = 0.05f;
    }
    if (entry.confidence > 1.f) {
      entry.confidence = 1.f;
    }
    entry.hypothesis = item.value("hypothesis", "");
    entry.discard_reason = item.value("discard_reason", "");
    if (item.contains("supporting") && item["supporting"].is_array()) {
      for (const auto& sup : item["supporting"]) {
        if (!sup.is_object()) {
          continue;
        }
        RegistryPrimarySurveySupporting s;
        s.id = sup.value("id", "");
        s.role = sup.value("role", "");
        if (s.id.empty() || s.id == entry.id || !allowed.count(s.id)) {
          continue;
        }
        if (s.role != "trigger" && s.role != "state_owner" && s.role != "cleanup" &&
            s.role != "consumer" && s.role != "boundary") {
          s.role = "consumer";
        }
        entry.supporting.push_back(std::move(s));
        if (entry.supporting.size() >= 2) {
          break;
        }
      }
    }
    const auto targets_it = allowed_targets.find(entry.id);
    if (item.contains("expand_from") && item["expand_from"].is_array() &&
        targets_it != allowed_targets.end()) {
      for (const auto& t : item["expand_from"]) {
        if (!t.is_string()) {
          continue;
        }
        const std::string target = t.get<std::string>();
        if (std::find(targets_it->second.begin(), targets_it->second.end(), target) ==
            targets_it->second.end()) {
          continue;
        }
        entry.expand_from.push_back(target);
        if (entry.expand_from.size() >= 3) {
          break;
        }
      }
    }
    if (!entry.discard) {
      if (entry.hypothesis.size() < 12) {
        entry.discard = true;
        entry.discard_reason = "hypothesis demasiado corta";
      } else if (entry.hypothesis.size() > 200) {
        truncate_utf8(&entry.hypothesis, 200);
      }
      if (!entry.discard && entry.expand_from.empty() && targets_it != allowed_targets.end() &&
          !targets_it->second.empty()) {
        entry.expand_from.push_back(targets_it->second.front());
      }
    } else if (entry.discard_reason.size() < 8) {
      entry.discard_reason = "sin mecanismo primary";
    }
    out.entries.push_back(std::move(entry));
  }
  // Rellenar zonas faltantes como discard.
  for (const auto& zone_id : allowed_zone_ids) {
    if (seen.count(zone_id)) {
      continue;
    }
    RegistryPrimarySurveyEntry missing;
    missing.id = zone_id;
    missing.discard = true;
    missing.confidence = 0.05f;
    missing.discard_reason = "ausente en survey";
    out.entries.push_back(std::move(missing));
  }
  if (out.entries.empty()) {
    out.error = "survey sin entradas";
    return out;
  }
  out.ok = true;
  return out;
}

nlohmann::json registry_primary_survey_to_json(const RegistryPrimarySurveyDecision& decision) {
  nlohmann::json zones = nlohmann::json::array();
  for (const auto& entry : decision.entries) {
    nlohmann::json supporting = nlohmann::json::array();
    for (const auto& s : entry.supporting) {
      supporting.push_back({{"id", s.id}, {"role", s.role}});
    }
    zones.push_back({{"id", entry.id},
                     {"discard", entry.discard},
                     {"confidence", entry.confidence},
                     {"hypothesis", entry.hypothesis},
                     {"discard_reason", entry.discard_reason},
                     {"supporting", supporting},
                     {"expand_from", entry.expand_from}});
  }
  return {{"action", decision.action.empty() ? "causal_zone_primary_survey_v1" : decision.action},
          {"ok", decision.ok},
          {"zones", zones},
          {"error", decision.error}};
}

std::vector<RegistryCausalTriageDecision> registry_primary_survey_select_threads(
    const RegistryPrimarySurveyDecision& survey, const nlohmann::json& base_payload,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets,
    int max_threads) {
  std::vector<RegistryCausalTriageDecision> threads;
  if (!survey.ok || max_threads <= 0) {
    return threads;
  }
  std::vector<const RegistryPrimarySurveyEntry*> ranked;
  for (const auto& entry : survey.entries) {
    if (!entry.discard && entry.hypothesis.size() >= 12) {
      ranked.push_back(&entry);
    }
  }
  std::sort(ranked.begin(), ranked.end(),
            [](const RegistryPrimarySurveyEntry* a, const RegistryPrimarySurveyEntry* b) {
              return a->confidence > b->confidence;
            });
  std::unordered_set<std::string> used_primary;
  for (const auto* entry : ranked) {
    if (static_cast<int>(threads.size()) >= max_threads) {
      break;
    }
    if (used_primary.count(entry->id)) {
      continue;
    }
    used_primary.insert(entry->id);
    RegistryCausalTriageDecision triage;
    triage.ok = true;
    triage.action = "causal_zone_primary_survey_v1";
    triage.hypothesis = entry->hypothesis;
    triage.critical_mass = entry->confidence >= 0.4f;
    triage.retrieval_needed = false;
    triage.why = "primary survey centered on " + entry->id +
                 " confidence=" + std::to_string(entry->confidence);
    triage.shortlist.push_back(entry->id);
    RegistryZoneTriage primary;
    primary.id = entry->id;
    primary.verdict = "anchor";
    primary.role_guess = "primary";
    primary.explains = entry->hypothesis;
    truncate_utf8(&primary.explains, 140);
    primary.need = "comprobar hyp centrada en " + entry->id;
    primary.expand_from = entry->expand_from;
    if (primary.expand_from.empty()) {
      auto it = allowed_targets.find(entry->id);
      if (it != allowed_targets.end() && !it->second.empty()) {
        primary.expand_from.push_back(it->second.front());
      }
    }
    triage.zones.push_back(std::move(primary));
    for (const auto& sup : entry->supporting) {
      if (triage.shortlist.size() >= 3) {
        break;
      }
      if (std::find(triage.shortlist.begin(), triage.shortlist.end(), sup.id) !=
          triage.shortlist.end()) {
        continue;
      }
      triage.shortlist.push_back(sup.id);
      RegistryZoneTriage arm;
      arm.id = sup.id;
      arm.verdict = "inspect";
      arm.role_guess = (sup.role.empty() ? "consumer" : sup.role);
      arm.need = "brazo " + arm.role_guess + " de hyp centrada en " + entry->id;
      auto it = allowed_targets.find(sup.id);
      if (it != allowed_targets.end() && !it->second.empty()) {
        arm.expand_from.push_back(it->second.front());
      }
      triage.zones.push_back(std::move(arm));
    }
    // Si el survey no dio supporting, el co-shortlist determinista puede completar.
    (void)base_payload;
    threads.push_back(std::move(triage));
  }
  return threads;
}

std::vector<std::string> registry_collect_must_compete_zone_ids(const nlohmann::json& base_payload,
                                                               int max_n) {
  std::vector<std::string> out;
  if (max_n <= 0) {
    return out;
  }
  const auto& zones = base_payload.value("zones", nlohmann::json::array());
  if (zones.size() < 2) {
    return out;
  }
  const std::string top0 = zones[0].value("id", "");
  std::unordered_set<std::string> context_pool;
  const size_t top_n = std::min<size_t>(3, zones.size());
  for (size_t i = 0; i < top_n; ++i) {
    for (const auto& stem : zones[i].value("context_stems", nlohmann::json::array())) {
      if (stem.is_string()) {
        context_pool.insert(stem.get<std::string>());
      }
    }
  }
  auto try_add = [&](const std::string& zone_id) {
    if (zone_id.empty() || zone_id == top0) {
      return;
    }
    if (std::find(out.begin(), out.end(), zone_id) != out.end()) {
      return;
    }
    if (static_cast<int>(out.size()) >= max_n) {
      return;
    }
    out.push_back(zone_id);
  };
  // Context(top) ∩ primary(candidato).
  for (size_t i = 1; i < zones.size(); ++i) {
    if (static_cast<int>(out.size()) >= max_n) {
      break;
    }
    const std::string zone_id = zones[i].value("id", "");
    if (zone_id.empty() || zone_id == top0) {
      continue;
    }
    bool overlap = false;
    for (const auto& stem : zones[i].value("primary_stems", nlohmann::json::array())) {
      if (stem.is_string() && context_pool.count(stem.get<std::string>()) > 0) {
        overlap = true;
        break;
      }
    }
    if (overlap) {
      try_add(zone_id);
    }
  }
  // Bridges touching top-2.
  std::unordered_set<std::string> top_touch;
  for (size_t i = 0; i < std::min<size_t>(2, zones.size()); ++i) {
    top_touch.insert(zones[i].value("id", ""));
  }
  for (const auto& bridge : base_payload.value("zone_bridges", nlohmann::json::array())) {
    if (static_cast<int>(out.size()) >= max_n) {
      break;
    }
    const auto linked = bridge.value("zones", nlohmann::json::array());
    bool touches = false;
    for (const auto& z : linked) {
      if (z.is_string() && top_touch.count(z.get<std::string>()) > 0) {
        touches = true;
        break;
      }
    }
    if (!touches) {
      continue;
    }
    for (const auto& z : linked) {
      if (z.is_string()) {
        try_add(z.get<std::string>());
      }
    }
  }
  return out;
}

std::string registry_strip_zone_scores_markdown(const std::string& cards_markdown) {
  std::ostringstream out;
  std::istringstream in(cards_markdown);
  std::string line;
  while (std::getline(in, line)) {
    // ## M1 score=0.91 margin=... → ## M1
    if (line.rfind("## M", 0) == 0) {
      const auto sp = line.find(' ', 3);
      if (sp != std::string::npos) {
        out << line.substr(0, sp) << "\n";
        continue;
      }
    }
    out << line << "\n";
  }
  return out.str();
}

std::string registry_causal_contrast_system_prompt() {
  return R"(Eres un buscador de CONTRASTE causal: hasta 2 hipótesis GLOBALES incompatibles.
Recibes fichas de zonas (sin scores). Debes proponer hilos rivales, no confirmar la primera idea.

Reglas:
1) Emite 1–2 threads. Cada thread tiene primary distinto y una hypothesis GLOBAL centrada en esa primary.
2) Las hypotheses deben ser MECANISMOS INCOMPATIBLES (prohibido copiar la misma frase/símbolo
   cambiando solo el primary; p.ej. no reutilizar run_compile en dos threads).
3) Si el user nombra must_compete, OBLIGATORIO: para cada id de esa lista, o bien un thread con
   primary=ese id, o un discard con reason concreta + expand_from (1–2 targets EXACTOS de ESA ficha).
   Frases vacías tipo "no se relaciona" SIN expand_from son INVÁLIDAS.
4) single_viable=true solo si must_compete está vacío o todos los must_compete tienen discard válido.
5) supporting: 0–2 zonas con rol trigger|state_owner|cleanup|consumer|boundary.
6) confidence: número 0.05–1. hypothesis: 12–160 chars.

Devuelve SOLO JSON:
{"action":"causal_zone_contrast_v1","single_viable":false,"threads":[{"primary":"M1","hypothesis":"hyp global","confidence":0.7,"supporting":[{"id":"M3","role":"trigger"}],"expand_from":["target exacto"]}],"discards":[{"id":"M2","reason":"solo léxico","expand_from":["target de M2"]}]}
)";
}

std::string registry_causal_contrast_user_prompt(const std::string& cards_markdown,
                                                 const std::vector<std::string>& must_compete,
                                                 const std::string& retry_need) {
  std::ostringstream out;
  out << "Contraste causal: 1–2 hyp globales incompatibles. No inventes targets.\n";
  if (!must_compete.empty()) {
    out << "MUST_COMPETE (obligatorio thread o discard válido con expand_from):";
    for (const auto& id : must_compete) {
      out << " " << id;
    }
    out << "\n";
  } else {
    out << "No hay must_compete; puedes usar single_viable=true si solo hay un mecanismo.\n";
  }
  if (!retry_need.empty()) {
    out << "REINTENTO: " << retry_need << "\n";
  }
  out << "\n" << cards_markdown;
  return out.str();
}

namespace {

std::unordered_set<std::string> contrast_hyp_tokens(const std::string& hyp) {
  std::unordered_set<std::string> out;
  std::string cur;
  auto flush = [&]() {
    if (cur.size() >= 4) {
      std::string lower = cur;
      for (char& ch : lower) {
        if (ch >= 'A' && ch <= 'Z') {
          ch = static_cast<char>(ch - 'A' + 'a');
        }
      }
      out.insert(lower);
    }
    cur.clear();
  };
  for (unsigned char ch : hyp) {
    if (std::isalnum(ch) || ch == '_') {
      cur.push_back(static_cast<char>(ch));
    } else {
      flush();
    }
  }
  flush();
  return out;
}

bool contrast_hypotheses_duplicate(const std::string& a, const std::string& b) {
  if (a.empty() || b.empty()) {
    return false;
  }
  if (a == b) {
    return true;
  }
  const auto ta = contrast_hyp_tokens(a);
  const auto tb = contrast_hyp_tokens(b);
  if (ta.empty() || tb.empty()) {
    return false;
  }
  // Symbolish overlap: tokens with '_' shared.
  int shared_sym = 0;
  for (const auto& t : ta) {
    if (t.find('_') == std::string::npos || t.size() < 6) {
      continue;
    }
    if (tb.count(t)) {
      ++shared_sym;
    }
  }
  if (shared_sym >= 1) {
    return true;
  }
  // High Jaccard on tokens len>=5
  int inter = 0;
  for (const auto& t : ta) {
    if (t.size() >= 5 && tb.count(t)) {
      ++inter;
    }
  }
  const int uni = static_cast<int>(ta.size() + tb.size()) - inter;
  return uni > 0 && (2 * inter >= uni);  // Jaccard >= 0.5
}

std::string first_zone_target(const nlohmann::json& zone) {
  for (const auto& group : zone.value("anchors", nlohmann::json::array())) {
    for (const auto& a : group) {
      const std::string t = a.value("target", "");
      if (!t.empty()) {
        return t;
      }
    }
  }
  for (const auto& r : zone.value("representatives", nlohmann::json::array())) {
    const std::string t = r.value("target", "");
    if (!t.empty()) {
      return t;
    }
  }
  return {};
}

}  // namespace

RegistryContrastDecision registry_parse_causal_contrast(
    const std::string& raw, const std::vector<std::string>& allowed_zone_ids,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets) {
  RegistryContrastDecision out;
  out.raw = raw;
  out.action = "causal_zone_contrast_v1";
  std::string cleaned = raw;
  const auto fence = cleaned.find("```");
  if (fence != std::string::npos) {
    cleaned = cleaned.substr(fence);
    const auto nl = cleaned.find('\n');
    if (nl != std::string::npos) {
      cleaned = cleaned.substr(nl + 1);
    }
    const auto end_fence = cleaned.rfind("```");
    if (end_fence != std::string::npos) {
      cleaned.resize(end_fence);
    }
  }
  nlohmann::json j;
  const std::string payload = extract_balanced_json_object(cleaned);
  if (!payload.empty()) {
    try {
      j = nlohmann::json::parse(payload);
    } catch (...) {
      j = nlohmann::json();
    }
  }
  if (!j.is_object()) {
    // Salvamento: array threads suelto
    nlohmann::json threads = nlohmann::json::array();
    const auto tpos = cleaned.find("\"threads\"");
    if (tpos != std::string::npos) {
      const auto bracket = cleaned.find('[', tpos);
      if (bracket != std::string::npos) {
        int depth = 0;
        bool in_string = false;
        bool escape = false;
        std::size_t obj_begin = std::string::npos;
        for (std::size_t i = bracket + 1; i < cleaned.size(); ++i) {
          const char c = cleaned[i];
          if (in_string) {
            if (escape) {
              escape = false;
            } else if (c == '\\') {
              escape = true;
            } else if (c == '"') {
              in_string = false;
            }
            continue;
          }
          if (c == '"') {
            in_string = true;
          } else if (c == '{') {
            if (depth == 0) {
              obj_begin = i;
            }
            ++depth;
          } else if (c == '}') {
            --depth;
            if (depth == 0 && obj_begin != std::string::npos) {
              try {
                threads.push_back(nlohmann::json::parse(
                    cleaned.substr(obj_begin, i - obj_begin + 1)));
              } catch (...) {
              }
              obj_begin = std::string::npos;
            }
          } else if (c == ']' && depth == 0) {
            break;
          }
        }
      }
    }
    if (threads.empty()) {
      out.error = "JSON contrast inválido o truncado";
      return out;
    }
    j = {{"action", "causal_zone_contrast_v1"},
         {"threads", threads},
         {"discards", nlohmann::json::array()},
         {"single_viable", threads.size() <= 1}};
  }
  const std::unordered_set<std::string> allowed(allowed_zone_ids.begin(), allowed_zone_ids.end());
  out.single_viable = j.value("single_viable", false);
  auto filter_expand = [&](const std::string& zone_id, const nlohmann::json& arr) {
    std::vector<std::string> out_exp;
    auto it = allowed_targets.find(zone_id);
    if (it == allowed_targets.end() || !arr.is_array()) {
      return out_exp;
    }
    for (const auto& t : arr) {
      if (!t.is_string()) {
        continue;
      }
      const std::string target = t.get<std::string>();
      if (std::find(it->second.begin(), it->second.end(), target) == it->second.end()) {
        continue;
      }
      out_exp.push_back(target);
      if (out_exp.size() >= 3) {
        break;
      }
    }
    return out_exp;
  };
  if (j.contains("threads") && j["threads"].is_array()) {
    std::unordered_set<std::string> seen_primary;
    for (const auto& item : j["threads"]) {
      if (!item.is_object()) {
        continue;
      }
      RegistryContrastThread th;
      th.primary = item.value("primary", item.value("id", ""));
      if (th.primary.empty() || !allowed.count(th.primary) || seen_primary.count(th.primary)) {
        continue;
      }
      th.hypothesis = item.value("hypothesis", "");
      th.confidence = item.value("confidence", 0.5f);
      if (th.confidence < 0.05f) {
        th.confidence = 0.05f;
      }
      if (th.confidence > 1.f) {
        th.confidence = 1.f;
      }
      if (th.hypothesis.size() < 12) {
        continue;
      }
      if (th.hypothesis.size() > 200) {
        truncate_utf8(&th.hypothesis, 200);
      }
      if (item.contains("supporting") && item["supporting"].is_array()) {
        for (const auto& sup : item["supporting"]) {
          if (!sup.is_object()) {
            continue;
          }
          RegistryPrimarySurveySupporting s;
          s.id = sup.value("id", "");
          s.role = sup.value("role", "consumer");
          if (s.id.empty() || s.id == th.primary || !allowed.count(s.id)) {
            continue;
          }
          th.supporting.push_back(std::move(s));
          if (th.supporting.size() >= 2) {
            break;
          }
        }
      }
      th.expand_from = filter_expand(th.primary, item.value("expand_from", nlohmann::json::array()));
      if (th.expand_from.empty()) {
        auto it = allowed_targets.find(th.primary);
        if (it != allowed_targets.end() && !it->second.empty()) {
          th.expand_from.push_back(it->second.front());
        }
      }
      seen_primary.insert(th.primary);
      out.threads.push_back(std::move(th));
      if (out.threads.size() >= 2) {
        break;
      }
    }
  }
  if (j.contains("discards") && j["discards"].is_array()) {
    for (const auto& item : j["discards"]) {
      if (!item.is_object()) {
        continue;
      }
      RegistryContrastDiscard d;
      d.id = item.value("id", "");
      d.reason = item.value("reason", item.value("discard_reason", ""));
      if (d.id.empty() || !allowed.count(d.id)) {
        continue;
      }
      d.expand_from = filter_expand(d.id, item.value("expand_from", nlohmann::json::array()));
      out.discards.push_back(std::move(d));
    }
  }
  if (out.threads.empty()) {
    out.error = "contrast sin threads";
    return out;
  }
  out.ok = true;
  return out;
}

nlohmann::json registry_contrast_to_json(const RegistryContrastDecision& decision) {
  nlohmann::json threads = nlohmann::json::array();
  for (const auto& th : decision.threads) {
    nlohmann::json supporting = nlohmann::json::array();
    for (const auto& s : th.supporting) {
      supporting.push_back({{"id", s.id}, {"role", s.role}});
    }
    threads.push_back({{"primary", th.primary},
                       {"hypothesis", th.hypothesis},
                       {"confidence", th.confidence},
                       {"supporting", supporting},
                       {"expand_from", th.expand_from},
                       {"synthetic", th.synthetic}});
  }
  nlohmann::json discards = nlohmann::json::array();
  for (const auto& d : decision.discards) {
    discards.push_back(
        {{"id", d.id}, {"reason", d.reason}, {"expand_from", d.expand_from}});
  }
  return {{"action", decision.action.empty() ? "causal_zone_contrast_v1" : decision.action},
          {"ok", decision.ok},
          {"single_viable", decision.single_viable},
          {"injected", decision.injected},
          {"threads", threads},
          {"discards", discards},
          {"error", decision.error}};
}

RegistryContrastValidation registry_validate_contrast_threads(
    const RegistryContrastDecision& decision, const std::vector<std::string>& must_compete,
    const nlohmann::json& base_payload,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets) {
  RegistryContrastValidation v;
  if (!decision.ok || decision.threads.empty()) {
    v.error = decision.error.empty() ? "empty_threads" : decision.error;
    return v;
  }
  if (decision.threads.size() >= 2 &&
      contrast_hypotheses_duplicate(decision.threads[0].hypothesis,
                                    decision.threads[1].hypothesis)) {
    v.error = "duplicate_hypothesis";
    return v;
  }
  auto has_thread = [&](const std::string& id) {
    return std::any_of(decision.threads.begin(), decision.threads.end(),
                       [&](const RegistryContrastThread& th) { return th.primary == id; });
  };
  auto valid_discard = [&](const std::string& id) {
    for (const auto& d : decision.discards) {
      if (d.id != id) {
        continue;
      }
      if (d.reason.size() < 12) {
        return false;
      }
      // Must cite at least one allowed target of that zone.
      auto it = allowed_targets.find(id);
      if (it == allowed_targets.end() || it->second.empty()) {
        return !d.expand_from.empty() || d.reason.size() >= 20;
      }
      return !d.expand_from.empty();
    }
    return false;
  };
  for (const auto& zid : must_compete) {
    if (has_thread(zid)) {
      continue;
    }
    if (valid_discard(zid)) {
      continue;
    }
    v.error = "incomplete_contrast";
    return v;
  }
  if (decision.single_viable && !must_compete.empty()) {
    // single_viable only OK if every must-compete was validly discarded
    for (const auto& zid : must_compete) {
      if (has_thread(zid)) {
        // has competing thread → not single
        break;
      }
      if (!valid_discard(zid)) {
        v.error = "incomplete_contrast";
        return v;
      }
    }
  }
  (void)base_payload;
  v.ok = true;
  return v;
}

void registry_inject_synthetic_contrast_threads(
    RegistryContrastDecision* decision, const std::vector<std::string>& must_compete,
    const nlohmann::json& base_payload,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets) {
  if (decision == nullptr) {
    return;
  }
  const auto& zones = base_payload.value("zones", nlohmann::json::array());
  auto find_zone = [&](const std::string& id) -> const nlohmann::json* {
    for (const auto& z : zones) {
      if (z.value("id", "") == id) {
        return &z;
      }
    }
    return nullptr;
  };
  auto make_synthetic = [&](const std::string& zid) -> RegistryContrastThread {
    const nlohmann::json* zone = find_zone(zid);
    RegistryContrastThread th;
    th.primary = zid;
    th.synthetic = true;
    th.confidence = 0.55f;
    std::ostringstream hyp;
    hyp << "El mecanismo causal puede residir en la zona " << zid;
    if (zone != nullptr) {
      const auto prim = zone->value("primary_stems", nlohmann::json::array());
      if (!prim.empty() && prim.front().is_string()) {
        hyp << " (stems " << prim.front().get<std::string>();
        if (prim.size() > 1 && prim[1].is_string()) {
          hyp << ", " << prim[1].get<std::string>();
        }
        hyp << ")";
      }
      const std::string target = first_zone_target(*zone);
      if (!target.empty()) {
        hyp << " via " << target;
        th.expand_from.push_back(target);
      }
    }
    th.hypothesis = hyp.str();
    if (th.hypothesis.size() < 12) {
      th.hypothesis = "Comprobar mecanismo primary en zona " + zid + " frente al ancla rival";
    }
    if (th.expand_from.empty()) {
      auto it = allowed_targets.find(zid);
      if (it != allowed_targets.end() && !it->second.empty()) {
        th.expand_from.push_back(it->second.front());
      }
    }
    return th;
  };
  auto is_must = [&](const std::string& id) {
    return std::find(must_compete.begin(), must_compete.end(), id) != must_compete.end();
  };
  for (const auto& zid : must_compete) {
    if (std::any_of(decision->threads.begin(), decision->threads.end(),
                    [&](const RegistryContrastThread& th) { return th.primary == zid; })) {
      continue;
    }
    auto syn = make_synthetic(zid);
    if (decision->threads.size() < 2) {
      decision->threads.push_back(std::move(syn));
    } else {
      // Replace weakest non-must-compete thread so the rival is always evaluated.
      int replace_i = -1;
      float worst_conf = 1e9f;
      for (int i = static_cast<int>(decision->threads.size()) - 1; i >= 0; --i) {
        if (is_must(decision->threads[static_cast<size_t>(i)].primary)) {
          continue;
        }
        const float c = decision->threads[static_cast<size_t>(i)].confidence;
        if (c <= worst_conf) {
          worst_conf = c;
          replace_i = i;
        }
      }
      if (replace_i < 0) {
        replace_i = static_cast<int>(decision->threads.size()) - 1;
      }
      decision->threads[static_cast<size_t>(replace_i)] = std::move(syn);
    }
    decision->injected = true;
    decision->single_viable = false;
    decision->ok = true;
    decision->error.clear();
  }
  if (decision->threads.size() > 2) {
    decision->threads.resize(2);
  }
}

std::vector<RegistryCausalTriageDecision> registry_contrast_select_threads(
    const RegistryContrastDecision& contrast, const nlohmann::json& base_payload,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets,
    int max_threads) {
  std::vector<RegistryCausalTriageDecision> threads;
  if (!contrast.ok || max_threads <= 0) {
    return threads;
  }
  for (const auto& th : contrast.threads) {
    if (static_cast<int>(threads.size()) >= max_threads) {
      break;
    }
    if (th.primary.empty() || th.hypothesis.size() < 12) {
      continue;
    }
    RegistryCausalTriageDecision triage;
    triage.ok = true;
    triage.action = "causal_zone_contrast_v1";
    triage.hypothesis = th.hypothesis;
    triage.critical_mass = th.confidence >= 0.4f || th.synthetic;
    triage.retrieval_needed = false;
    triage.why = std::string(th.synthetic ? "synthetic contrast primary " : "contrast primary ") +
                 th.primary + " confidence=" + std::to_string(th.confidence);
    triage.shortlist.push_back(th.primary);
    RegistryZoneTriage primary;
    primary.id = th.primary;
    primary.verdict = "anchor";
    primary.role_guess = "primary";
    primary.explains = th.hypothesis;
    truncate_utf8(&primary.explains, 140);
    primary.need = "comprobar hyp contraste centrada en " + th.primary;
    primary.expand_from = th.expand_from;
    if (primary.expand_from.empty()) {
      auto it = allowed_targets.find(th.primary);
      if (it != allowed_targets.end() && !it->second.empty()) {
        primary.expand_from.push_back(it->second.front());
      }
    }
    triage.zones.push_back(std::move(primary));
    for (const auto& sup : th.supporting) {
      if (triage.shortlist.size() >= 3) {
        break;
      }
      if (std::find(triage.shortlist.begin(), triage.shortlist.end(), sup.id) !=
          triage.shortlist.end()) {
        continue;
      }
      triage.shortlist.push_back(sup.id);
      RegistryZoneTriage arm;
      arm.id = sup.id;
      arm.verdict = "inspect";
      arm.role_guess = sup.role.empty() ? "consumer" : sup.role;
      arm.need = "brazo " + arm.role_guess + " de contraste " + th.primary;
      auto it = allowed_targets.find(sup.id);
      if (it != allowed_targets.end() && !it->second.empty()) {
        arm.expand_from.push_back(it->second.front());
      }
      triage.zones.push_back(std::move(arm));
    }
    (void)base_payload;
    threads.push_back(std::move(triage));
  }
  return threads;
}

std::vector<std::string> registry_collect_slot_queue_zone_ids(const nlohmann::json& base_payload,
                                                             int max_n) {
  std::vector<std::string> out;
  if (max_n <= 0) {
    return out;
  }
  const auto& zones = base_payload.value("zones", nlohmann::json::array());
  if (zones.empty()) {
    return out;
  }
  auto push_unique = [&](const std::string& id) {
    if (id.empty() || static_cast<int>(out.size()) >= max_n) {
      return;
    }
    if (std::find(out.begin(), out.end(), id) != out.end()) {
      return;
    }
    out.push_back(id);
  };
  // 1) top-1
  push_unique(zones[0].value("id", ""));
  // 2) must-compete / structural rivals
  for (const auto& id : registry_collect_must_compete_zone_ids(base_payload, max_n)) {
    push_unique(id);
  }
  // 3) rest by payload order (score order)
  for (const auto& zone : zones) {
    push_unique(zone.value("id", ""));
  }
  return out;
}

std::vector<std::string> registry_slot_supporting_zone_ids(const nlohmann::json& base_payload,
                                                          const std::string& primary_id,
                                                          int max_n) {
  std::vector<std::string> out;
  if (max_n <= 0 || primary_id.empty()) {
    return out;
  }
  auto push_unique = [&](const std::string& id) {
    if (id.empty() || id == primary_id || static_cast<int>(out.size()) >= max_n) {
      return;
    }
    if (std::find(out.begin(), out.end(), id) != out.end()) {
      return;
    }
    out.push_back(id);
  };
  for (const auto& bridge : base_payload.value("zone_bridges", nlohmann::json::array())) {
    bool touches = false;
    for (const auto& z : bridge.value("zones", nlohmann::json::array())) {
      if (z.is_string() && z.get<std::string>() == primary_id) {
        touches = true;
        break;
      }
    }
    if (!touches) {
      continue;
    }
    for (const auto& z : bridge.value("zones", nlohmann::json::array())) {
      if (z.is_string()) {
        push_unique(z.get<std::string>());
      }
    }
  }
  // Prefer must-compete partner / top-1 as weak supporting for non-top slots.
  const auto& zones = base_payload.value("zones", nlohmann::json::array());
  if (!zones.empty()) {
    const std::string top0 = zones[0].value("id", "");
    if (primary_id != top0) {
      push_unique(top0);
    }
  }
  for (const auto& mid : registry_collect_must_compete_zone_ids(base_payload, 2)) {
    push_unique(mid);
  }
  return out;
}

std::string registry_causal_slot_cards_markdown(const nlohmann::json& base_payload,
                                               const std::string& primary_id,
                                               const std::vector<std::string>& supporting_ids) {
  nlohmann::json slim = nlohmann::json::object();
  slim["query"] = base_payload.value("query", "");
  slim["gate"] = base_payload.value("gate", nlohmann::json::object());
  slim["zones"] = nlohmann::json::array();
  slim["zone_bridges"] = nlohmann::json::array();
  slim["uncovered_seeds"] = nlohmann::json::array();
  const auto& zones = base_payload.value("zones", nlohmann::json::array());
  auto append_zone = [&](const std::string& id, bool is_primary) {
    for (const auto& zone : zones) {
      if (zone.value("id", "") != id) {
        continue;
      }
      nlohmann::json z = zone;
      // Avoid score attention sink.
      z.erase("score");
      z.erase("score_margin");
      z.erase("mass_coverage");
      if (!is_primary) {
        z["slot_role"] = "supporting";
      } else {
        z["slot_role"] = "primary";
      }
      slim["zones"].push_back(std::move(z));
      return;
    }
  };
  append_zone(primary_id, true);
  for (const auto& sid : supporting_ids) {
    append_zone(sid, false);
  }
  std::string md = registry_causal_triage_markdown(slim);
  return registry_strip_zone_scores_markdown(md);
}

std::string registry_causal_slot_system_prompt() {
  return R"(Eres un generador de HIPÓTESIS causal por SLOT.
Recibes UNA zona primary (y como mucho 1–2 supporting ya elegidas). No ves el mazo completo.

Reglas:
1) Emite UNA hipótesis GLOBAL centrada en la primary indicada, usando stems/targets de ESA ficha.
2) Si la primary no puede explicar el síntoma, discard=true con reason concreta (≥12 chars) y
   expand_from con 1–2 targets EXACTOS de la ficha primary. Prohibido "no se relaciona" sin targets.
3) supporting opcional: 0–2 ids del prompt con rol trigger|state_owner|cleanup|consumer|boundary.
4) confidence 0.05–1. hypothesis 12–160 chars. No inventes targets.

Devuelve SOLO JSON:
{"action":"causal_zone_slot_hyp_v1","primary":"M7","discard":false,"confidence":0.7,"hypothesis":"...","supporting":[{"id":"M1","role":"trigger"}],"expand_from":["target exacto"]}
o discard:
{"action":"causal_zone_slot_hyp_v1","primary":"M2","discard":true,"discard_reason":"...","expand_from":["target de M2"],"confidence":0.1}
)";
}

std::string registry_causal_slot_user_prompt(const std::string& cards_markdown,
                                            const std::string& primary_id,
                                            const std::string& retry_need) {
  std::ostringstream out;
  out << "SLOT primary obligatoria: " << primary_id
      << "\nGenera hyp centrada en esa primary, o discard válido con expand_from de esa ficha.\n";
  if (!retry_need.empty()) {
    out << "REINTENTO: " << retry_need << "\n";
  }
  out << "\n" << cards_markdown;
  return out.str();
}

RegistrySlotHypothesis registry_parse_causal_slot_hypothesis(
    const std::string& raw, const std::string& expected_primary,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets) {
  RegistrySlotHypothesis out;
  out.raw = raw;
  out.primary = expected_primary;
  std::string cleaned = raw;
  const auto fence = cleaned.find("```");
  if (fence != std::string::npos) {
    cleaned = cleaned.substr(fence);
    const auto nl = cleaned.find('\n');
    if (nl != std::string::npos) {
      cleaned = cleaned.substr(nl + 1);
    }
    const auto end_fence = cleaned.rfind("```");
    if (end_fence != std::string::npos) {
      cleaned.resize(end_fence);
    }
  }
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(extract_balanced_json_object(cleaned));
  } catch (...) {
    out.error = "json_parse";
    return out;
  }
  if (!j.is_object()) {
    out.error = "not_object";
    return out;
  }
  const std::string primary = j.value("primary", expected_primary);
  if (!primary.empty() && primary != expected_primary) {
    // Force contract to the slot primary.
    out.primary = expected_primary;
  }
  out.discard = j.value("discard", false);
  out.confidence = j.value("confidence", 0.f);
  out.hypothesis = j.value("hypothesis", "");
  out.discard_reason = j.value("discard_reason", "");
  if (out.discard_reason.empty()) {
    out.discard_reason = j.value("reason", "");
  }
  if (j.contains("supporting") && j["supporting"].is_array()) {
    for (const auto& s : j["supporting"]) {
      if (!s.is_object()) {
        continue;
      }
      RegistryPrimarySurveySupporting sup;
      sup.id = s.value("id", "");
      sup.role = s.value("role", "");
      if (!sup.id.empty()) {
        out.supporting.push_back(std::move(sup));
      }
    }
  }
  auto it = allowed_targets.find(expected_primary);
  const std::vector<std::string>* allowed =
      it == allowed_targets.end() ? nullptr : &it->second;
  if (j.contains("expand_from") && j["expand_from"].is_array()) {
    for (const auto& t : j["expand_from"]) {
      if (!t.is_string()) {
        continue;
      }
      const std::string target = t.get<std::string>();
      if (allowed != nullptr &&
          std::find(allowed->begin(), allowed->end(), target) == allowed->end()) {
        continue;
      }
      out.expand_from.push_back(target);
    }
  }
  out.ok = true;
  return out;
}

bool registry_validate_slot_hypothesis(
    const RegistrySlotHypothesis& hyp, const std::string& expected_primary,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets,
    std::string* err) {
  auto set_err = [&](const char* e) {
    if (err != nullptr) {
      *err = e;
    }
    return false;
  };
  if (!hyp.ok) {
    return set_err(hyp.error.empty() ? "parse_fail" : hyp.error.c_str());
  }
  if (hyp.primary != expected_primary) {
    return set_err("primary_mismatch");
  }
  if (hyp.discard) {
    if (hyp.discard_reason.size() < 12) {
      return set_err("weak_discard");
    }
    auto it = allowed_targets.find(expected_primary);
    if (it != allowed_targets.end() && !it->second.empty() && hyp.expand_from.empty()) {
      return set_err("discard_missing_targets");
    }
    return true;
  }
  if (hyp.hypothesis.size() < 12) {
    return set_err("short_hypothesis");
  }
  return true;
}

void registry_inject_synthetic_slot_hypothesis(
    RegistrySlotHypothesis* hyp, const std::string& primary_id,
    const nlohmann::json& base_payload,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets) {
  if (hyp == nullptr) {
    return;
  }
  hyp->ok = true;
  hyp->error.clear();
  hyp->discard = false;
  hyp->discard_reason.clear();
  hyp->synthetic = true;
  hyp->primary = primary_id;
  hyp->confidence = 0.55f;
  hyp->supporting.clear();
  hyp->expand_from.clear();
  nlohmann::json zone_copy = nlohmann::json::object();
  bool have_zone = false;
  if (base_payload.contains("zones") && base_payload["zones"].is_array()) {
    for (const auto& z : base_payload["zones"]) {
      if (z.is_object() && z.value("id", "") == primary_id) {
        zone_copy = z;
        have_zone = true;
        break;
      }
    }
  }
  std::ostringstream h;
  h << "El mecanismo causal puede residir en la zona " << primary_id;
  if (have_zone) {
    const auto prim = zone_copy.value("primary_stems", nlohmann::json::array());
    if (prim.is_array() && !prim.empty() && prim.front().is_string()) {
      h << " (stems " << prim.front().get<std::string>();
      if (prim.size() > 1 && prim[1].is_string()) {
        h << ", " << prim[1].get<std::string>();
      }
      h << ")";
    }
    const std::string target = first_zone_target(zone_copy);
    if (!target.empty()) {
      h << " via " << target;
      hyp->expand_from.push_back(target);
    }
  }
  hyp->hypothesis = h.str();
  if (hyp->hypothesis.size() < 12) {
    hyp->hypothesis = "Comprobar mecanismo primary en zona " + primary_id;
  }
  if (hyp->expand_from.empty()) {
    auto it = allowed_targets.find(primary_id);
    if (it != allowed_targets.end() && !it->second.empty()) {
      hyp->expand_from.push_back(it->second.front());
    }
  }
}

std::vector<RegistrySlotHypothesis> registry_slot_retain_hypotheses(
    const std::vector<RegistrySlotHypothesis>& slots, const nlohmann::json& base_payload,
    int max_keep) {
  std::vector<RegistrySlotHypothesis> kept;
  if (max_keep <= 0) {
    return kept;
  }
  const auto must = registry_collect_must_compete_zone_ids(base_payload, 2);
  auto is_dup = [](const std::string& a, const std::string& b) {
    return contrast_hypotheses_duplicate(a, b);
  };
  auto try_keep = [&](const RegistrySlotHypothesis& hyp) {
    if (static_cast<int>(kept.size()) >= max_keep || hyp.discard || !hyp.ok) {
      return;
    }
    if (hyp.hypothesis.size() < 12) {
      return;
    }
    if (std::any_of(kept.begin(), kept.end(),
                    [&](const RegistrySlotHypothesis& k) { return k.primary == hyp.primary; })) {
      return;
    }
    if (std::any_of(kept.begin(), kept.end(), [&](const RegistrySlotHypothesis& k) {
          return is_dup(k.hypothesis, hyp.hypothesis);
        })) {
      return;
    }
    kept.push_back(hyp);
  };
  // Prefer structural rivals first, then others by confidence.
  for (const auto& mid : must) {
    for (const auto& hyp : slots) {
      if (hyp.primary == mid) {
        try_keep(hyp);
      }
    }
  }
  std::vector<RegistrySlotHypothesis> rest;
  for (const auto& hyp : slots) {
    if (hyp.discard || !hyp.ok) {
      continue;
    }
    if (std::find(must.begin(), must.end(), hyp.primary) != must.end()) {
      continue;
    }
    rest.push_back(hyp);
  }
  std::stable_sort(rest.begin(), rest.end(),
                   [](const RegistrySlotHypothesis& a, const RegistrySlotHypothesis& b) {
                     return a.confidence > b.confidence;
                   });
  for (const auto& hyp : rest) {
    try_keep(hyp);
  }
  return kept;
}

bool registry_slot_gold_in_hypotheses(const std::vector<RegistrySlotHypothesis>& retained,
                                     const nlohmann::json& base_payload,
                                     const std::vector<std::string>& expected_stems) {
  if (expected_stems.empty() || retained.empty()) {
    return false;
  }
  std::unordered_set<std::string> expect(expected_stems.begin(), expected_stems.end());
  const auto& zones = base_payload.value("zones", nlohmann::json::array());
  for (const auto& hyp : retained) {
    if (hyp.discard) {
      continue;
    }
    for (const auto& stem : expected_stems) {
      if (hyp.hypothesis.find(stem) != std::string::npos) {
        return true;
      }
    }
    for (const auto& zone : zones) {
      if (zone.value("id", "") != hyp.primary) {
        continue;
      }
      for (const char* key : {"primary_stems", "core_stems", "context_stems"}) {
        for (const auto& stem : zone.value(key, nlohmann::json::array())) {
          if (stem.is_string() && expect.count(stem.get<std::string>()) > 0) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

nlohmann::json registry_slot_survey_to_json(const RegistrySlotSurveyResult& result) {
  nlohmann::json slots = nlohmann::json::array();
  auto dump_hyp = [](const RegistrySlotHypothesis& h) {
    nlohmann::json supporting = nlohmann::json::array();
    for (const auto& s : h.supporting) {
      supporting.push_back({{"id", s.id}, {"role", s.role}});
    }
    return nlohmann::json{{"primary", h.primary},
                          {"hypothesis", h.hypothesis},
                          {"confidence", h.confidence},
                          {"discard", h.discard},
                          {"discard_reason", h.discard_reason},
                          {"expand_from", h.expand_from},
                          {"supporting", supporting},
                          {"synthetic", h.synthetic},
                          {"ok", h.ok},
                          {"error", h.error}};
  };
  for (const auto& h : result.slots) {
    slots.push_back(dump_hyp(h));
  }
  nlohmann::json retained = nlohmann::json::array();
  for (const auto& h : result.retained) {
    retained.push_back(dump_hyp(h));
  }
  return {{"action", "causal_zone_slot_survey_v1"},
          {"queue", result.queue},
          {"slots", slots},
          {"retained", retained},
          {"gold_in_hypotheses", result.gold_in_hypotheses}};
}

std::vector<RegistryCausalTriageDecision> registry_slot_hyps_to_threads(
    const std::vector<RegistrySlotHypothesis>& retained, const nlohmann::json& base_payload,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets) {
  std::vector<RegistryCausalTriageDecision> threads;
  for (const auto& th : retained) {
    if (th.discard || th.hypothesis.size() < 12) {
      continue;
    }
    RegistryCausalTriageDecision triage;
    triage.ok = true;
    triage.action = "causal_zone_slot_hyp_v1";
    triage.hypothesis = th.hypothesis;
    triage.critical_mass = th.confidence >= 0.4f || th.synthetic;
    triage.why = std::string(th.synthetic ? "synthetic slot primary " : "slot primary ") +
                 th.primary + " confidence=" + std::to_string(th.confidence);
    triage.shortlist.push_back(th.primary);
    RegistryZoneTriage primary;
    primary.id = th.primary;
    primary.verdict = "anchor";
    primary.role_guess = "primary";
    primary.explains = th.hypothesis;
    truncate_utf8(&primary.explains, 140);
    primary.need = "comprobar hyp slot centrada en " + th.primary;
    primary.expand_from = th.expand_from;
    if (primary.expand_from.empty()) {
      auto it = allowed_targets.find(th.primary);
      if (it != allowed_targets.end() && !it->second.empty()) {
        primary.expand_from.push_back(it->second.front());
      }
    }
    triage.zones.push_back(std::move(primary));
    for (const auto& sup : th.supporting) {
      if (triage.shortlist.size() >= 3) {
        break;
      }
      if (std::find(triage.shortlist.begin(), triage.shortlist.end(), sup.id) !=
          triage.shortlist.end()) {
        continue;
      }
      triage.shortlist.push_back(sup.id);
      RegistryZoneTriage arm;
      arm.id = sup.id;
      arm.verdict = "inspect";
      arm.role_guess = sup.role.empty() ? "consumer" : sup.role;
      arm.need = "brazo " + arm.role_guess + " de slot " + th.primary;
      auto it = allowed_targets.find(sup.id);
      if (it != allowed_targets.end() && !it->second.empty()) {
        arm.expand_from.push_back(it->second.front());
      }
      triage.zones.push_back(std::move(arm));
    }
    (void)base_payload;
    threads.push_back(std::move(triage));
  }
  return threads;
}

namespace {
std::string atlas_owns_caption(const nlohmann::json& zone);
std::string atlas_not_caption(const nlohmann::json& zone, const std::string& kind);
}  // namespace

std::string registry_causal_judge_markdown(const nlohmann::json& payload) {
  std::ostringstream out;
  auto short_target = [](const std::string& target) {
    const auto slash = target.rfind('/');
    if (slash == std::string::npos) {
      return target;
    }
    const auto colon = target.find(':', slash);
    if (colon == std::string::npos) {
      return target;
    }
    std::string file = target.substr(slash + 1, colon - slash - 1);
    const auto dot = file.rfind('.');
    if (dot != std::string::npos) {
      file.resize(dot);
    }
    std::string suffix = target.substr(colon + 1);
    if (target.rfind("ctrl:", 0) == 0) {
      suffix = "L" + suffix;
    }
    return file + "::" + suffix;
  };
  out << "# causal_judge_v1\n";
  out << "query: " << payload.value("query", "") << "\n";
  const auto gate = payload.value("gate", nlohmann::json::object());
  out << "gate: cosine=" << gate.value("max_cosine", 0.f)
      << " map=" << gate.value("map_boosted", 0)
      << " weak=" << (gate.value("weak", false) ? "yes" : "no") << "\n";
  for (const auto& zone : payload.value("zones", nlohmann::json::array())) {
    out << "\n## " << zone.value("id", "?") << " score=" << zone.value("score", 0.f)
        << " margin=" << zone.value("score_margin", 0.f)
        << " coverage=" << zone.value("mass_coverage", 0.f) << "\n";
    const std::string kind = registry_causal_zone_kind(zone);
    out << "kind=" << kind << "\n";
    const std::string owns = atlas_owns_caption(zone);
    if (!owns.empty()) {
      out << "owns: " << owns << "\n";
    }
    const std::string notc = atlas_not_caption(zone, kind);
    if (!notc.empty()) {
      out << "not: " << notc << "\n";
    }
    out << "stems:";
    for (const auto& stem : zone.value("primary_stems", nlohmann::json::array())) {
      out << " " << stem.get<std::string>();
    }
    out << "\ncore stems:";
    for (const auto& stem : zone.value("core_stems", nlohmann::json::array())) {
      out << " " << stem.get<std::string>();
    }
    out << "\ncontext stems:";
    for (const auto& stem : zone.value("context_stems", nlohmann::json::array())) {
      out << " " << stem.get<std::string>();
    }
    const float merge_strength = zone.value("merge_strength", 0.f);
    if (merge_strength > 0.f) {
      out << "\nmerge: strength=" << merge_strength;
      for (const auto& witness : zone.value("merge_witnesses", nlohmann::json::array())) {
        out << " " << witness.get<std::string>();
      }
    }
    out << "\nnuclei:";
    for (const auto& nucleus : zone.value("nuclei", nlohmann::json::array())) {
      out << " " << nucleus.value("id", "?") << "(" << nucleus.value("state", "?") << ")";
    }
    out << "\nanchors:\n";
    int group_n = 0;
    for (const auto& group : zone.value("anchors", nlohmann::json::array())) {
      out << "- G" << ++group_n << ": ";
      bool first = true;
      for (const auto& anchor : group) {
        if (!first) {
          out << " -> ";
        }
        first = false;
        out << short_target(anchor.value("target", "?"));
        if (anchor.contains("qrank")) {
          out << "[q" << anchor["qrank"].get<int>() << "]";
        }
      }
      out << "\n";
    }
    const auto closure = zone.value("closure", nlohmann::json::object());
    out << "closure: " << closure.value("closed", 0) << "/"
        << closure.value("groups", 0) << " groups, hub-free="
        << closure.value("closed_without_hubs", 0) << "\n";
    out << "roles:\n";
    const auto roles = zone.value("roles", nlohmann::json::object());
    for (const char* role : {"writers", "readers", "controls", "handoffs"}) {
      const auto values = roles.value(role, nlohmann::json::array());
      if (values.empty()) {
        continue;
      }
      out << "- " << role << ": ";
      bool first = true;
      for (const auto& value : values) {
        if (!first) {
          out << ", ";
        }
        first = false;
        out << short_target(value.value("target", "?"));
      }
      out << "\n";
    }
    const auto mechanism = zone.value("mechanism", nlohmann::json::object());
    if (!mechanism.empty()) {
      out << "mechanism:\n";
      for (const char* slot : {"trigger", "state", "effect"}) {
        if (!mechanism.contains(slot) || !mechanism[slot].is_object()) {
          continue;
        }
        const auto& edge = mechanism[slot];
        out << "- " << slot << ": " << short_target(edge.value("from", "?")) << " -"
            << edge.value("kind", "?");
        if (edge.contains("member")) {
          out << "(" << edge["member"].get<std::string>() << ")";
        }
        out << "-> " << short_target(edge.value("to", "?"));
        if (edge.contains("cond")) {
          out << " if " << edge["cond"].get<std::string>();
        }
        out << "\n";
      }
    }
    const auto pack_meta = zone.value("pack_meta", nlohmann::json::object());
    if (pack_meta.contains("skeleton_missing") &&
        pack_meta["skeleton_missing"].is_array() &&
        !pack_meta["skeleton_missing"].empty()) {
      out << "skeleton_missing:";
      for (const auto& miss : pack_meta["skeleton_missing"]) {
        out << " " << miss.get<std::string>();
      }
      out << "\n";
    }
    const auto ports = zone.value("ports", nlohmann::json::array());
    if (!ports.empty()) {
      out << "ports:\n";
      for (const auto& edge : ports) {
        out << "- " << edge.value("from_zone", "?") << "=>" << edge.value("to_zone", "?")
            << " " << short_target(edge.value("from", "?")) << " -" << edge.value("kind", "?")
            << "-> " << short_target(edge.value("to", "?")) << "\n";
      }
    }
    out << "causal edges:\n";
    for (const auto& edge : zone.value("edges", nlohmann::json::array())) {
      out << "- " << short_target(edge.value("from", "?")) << " -"
          << edge.value("kind", "?");
      if (edge.contains("member")) {
        out << "(" << edge["member"].get<std::string>() << ")";
      }
      out << "-> " << short_target(edge.value("to", "?"));
      if (edge.contains("slot")) {
        out << " [" << edge["slot"].get<std::string>() << "]";
      }
      if (edge.contains("cond")) {
        out << " if " << edge["cond"].get<std::string>();
      }
      out << "\n";
    }
    out << "mini-cards:\n";
    for (const auto& card : zone.value("representatives", nlohmann::json::array())) {
      out << "- " << short_target(card.value("target", "?"));
      if (card.contains("sig")) {
        out << " | " << card["sig"].get<std::string>();
      }
      for (const char* key : {"roles", "writes", "reads", "calls_seed", "hot"}) {
        if (!card.contains(key)) {
          continue;
        }
        out << " | " << key << "=";
        if (card[key].is_array()) {
          bool first = true;
          for (const auto& value : card[key]) {
            if (!first) {
              out << ",";
            }
            first = false;
            out << value.get<std::string>();
          }
        } else {
          out << card[key].get<std::string>();
        }
      }
      out << "\n";
      if (card.contains("outline") && card["outline"].is_array()) {
        for (const auto& line : card["outline"]) {
          out << "  · " << line.get<std::string>() << "\n";
        }
      }
    }
    for (const auto& trail : zone.value("trails", nlohmann::json::array())) {
      out << "trail " << trail.value("id", "?") << ": ";
      bool first = true;
      for (const auto& step : trail.value("path", nlohmann::json::array())) {
        if (!first) {
          out << " -> ";
        }
        first = false;
        out << short_target(step.get<std::string>());
      }
      out << " | " << trail.value("why", "") << "\n";
    }
    const auto risks = zone.value("risks", nlohmann::json::array());
    if (!risks.empty()) {
      out << "risks:";
      for (const auto& risk : risks) {
        out << " " << risk.get<std::string>();
      }
      out << "\n";
    }
  }
  const auto bridges = payload.value("zone_bridges", nlohmann::json::array());
  if (!bridges.empty()) {
    out << "\n## zone bridges\n";
    for (const auto& bridge : bridges) {
      out << "- " << bridge.value("trail", "?") << ":";
      for (const auto& z : bridge.value("zones", nlohmann::json::array())) {
        out << " " << z.get<std::string>();
      }
      out << " | " << bridge.value("why", "") << "\n";
    }
  }
  const auto uncovered = payload.value("uncovered_seeds", nlohmann::json::array());
  if (!uncovered.empty()) {
    out << "\n## uncovered seeds\n";
    for (const auto& seed : uncovered) {
      out << "- q" << seed.value("qrank", -1) << " "
          << short_target(seed.value("target", "?")) << "\n";
    }
  }
  int evidence_units = 0;
  for (const auto& zone : payload.value("zones", nlohmann::json::array())) {
    evidence_units += static_cast<int>(zone.value("nuclei", nlohmann::json::array()).size());
    evidence_units += static_cast<int>(zone.value("anchors", nlohmann::json::array()).size());
    evidence_units += static_cast<int>(zone.value("edges", nlohmann::json::array()).size());
    evidence_units +=
        static_cast<int>(zone.value("representatives", nlohmann::json::array()).size());
    evidence_units += static_cast<int>(zone.value("trails", nlohmann::json::array()).size());
  }
  const std::string body = out.str();
  const std::size_t token_estimate = (body.size() + 3) / 4;
  out << "\n<!-- budget: chars=" << body.size() << " token_est_4c=" << token_estimate
      << " evidence_units=" << evidence_units << " -->\n";
  return out.str();
}

namespace {

std::string causal_short_target(const std::string& target) {
  const auto slash = target.rfind('/');
  if (slash == std::string::npos) {
    return target;
  }
  const auto colon = target.find(':', slash);
  if (colon == std::string::npos) {
    return target;
  }
  std::string file = target.substr(slash + 1, colon - slash - 1);
  const auto dot = file.rfind('.');
  if (dot != std::string::npos) {
    file.resize(dot);
  }
  std::string suffix = target.substr(colon + 1);
  if (target.rfind("ctrl:", 0) == 0) {
    suffix = "L" + suffix;
  }
  return file + "::" + suffix;
}

std::string causal_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool causal_hay_has(const std::string& hay, std::initializer_list<const char*> needles) {
  for (const char* needle : needles) {
    if (hay.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string causal_symbol_of(const std::string& target) {
  const auto colon = target.rfind(':');
  return colon == std::string::npos ? target : target.substr(colon + 1);
}

bool causal_token_is_stop(const std::string& t) {
  static const std::unordered_set<std::string> kStop = {
      "el",     "la",     "lo",     "los",    "las",    "un",     "una",    "unos",   "unas",
      "de",     "del",    "al",     "a",      "en",     "y",      "o",      "u",      "que",
      "se",     "su",     "sus",    "es",     "son",    "ser",    "fue",    "por",    "para",
      "con",    "sin",    "sobre",  "entre",  "hasta",  "desde",  "como",   "cuando", "donde",
      "quien",  "cual",   "cuales", "este",   "esta",   "estos",  "estas",  "eso",    "esa",
      "no",     "si",     "mas",    "muy",    "ya",     "hay",    "the",    "of",     "to",
      "in",     "on",     "for",    "and",    "or",     "is",     "are",    "be",     "was",
      "with",   "from",   "as",     "at",     "it",     "an",     "quiero", "necesito",
      "entender","modifica","modificar","añade","anade","añadir","anadir","cambia","muestrame",
      "mostrar","dentro","cuando","usuario","tambien","despues","antes","tiene","hacer"};
  return kStop.count(t) != 0;
}

void causal_push_tokens(const std::string& raw, std::vector<std::string>* out) {
  const std::string s = causal_lower(raw);
  std::string cur;
  auto flush = [&]() {
    if (cur.size() >= 3 && !causal_token_is_stop(cur)) {
      out->push_back(cur);
    }
    cur.clear();
  };
  for (unsigned char c : s) {
    if (std::isalnum(c) || c >= 0x80) {
      cur.push_back(static_cast<char>(c));
    } else {
      flush();
    }
  }
  flush();
}

bool causal_tokens_match(const std::string& a, const std::string& b) {
  if (a == b) {
    return true;
  }
  if (a.size() < 4 || b.size() < 4) {
    return false;
  }
  return a.find(b) == 0 || b.find(a) == 0 || a.find(b) != std::string::npos ||
         b.find(a) != std::string::npos;
}

int causal_token_overlap_score(const std::vector<std::string>& query_toks,
                               const std::vector<std::string>& hay_toks) {
  int score = 0;
  for (const auto& qt : query_toks) {
    int best = 0;
    for (const auto& ht : hay_toks) {
      if (causal_tokens_match(qt, ht)) {
        best = std::max(best, static_cast<int>(std::min(qt.size(), ht.size())));
      }
    }
    score += best;
  }
  return score;
}

std::string causal_zone_overlap_hay(const nlohmann::json& zone) {
  std::string hay;
  auto add = [&](const std::string& s) {
    if (!s.empty()) {
      hay += " ";
      hay += s;
    }
  };
  for (const char* key : {"primary_stems", "core_stems", "context_stems"}) {
    for (const auto& s : zone.value(key, nlohmann::json::array())) {
      if (s.is_string()) {
        add(s.get<std::string>());
      }
    }
  }
  add(zone.value("id", ""));
  for (const auto& card : zone.value("representatives", nlohmann::json::array())) {
    add(card.value("target", ""));
  }
  const auto roles = zone.value("roles", nlohmann::json::object());
  for (const auto& writer : roles.value("writers", nlohmann::json::array())) {
    add(writer.value("target", ""));
  }
  for (const auto& nucleus : zone.value("nuclei", nlohmann::json::array())) {
    add(nucleus.value("state", ""));
  }
  return hay;
}

}  // namespace

int registry_causal_query_zone_overlap(const std::string& query, const nlohmann::json& zone) {
  std::vector<std::string> qtoks;
  std::vector<std::string> htoks;
  causal_push_tokens(query, &qtoks);
  causal_push_tokens(causal_zone_overlap_hay(zone), &htoks);
  return causal_token_overlap_score(qtoks, htoks);
}

int registry_causal_query_hay_overlap(const std::string& query, const std::string& hay) {
  std::vector<std::string> qtoks;
  std::vector<std::string> htoks;
  causal_push_tokens(query, &qtoks);
  causal_push_tokens(hay, &htoks);
  return causal_token_overlap_score(qtoks, htoks);
}

namespace {

std::string atlas_primary_stem(const nlohmann::json& zone) {
  for (const char* key : {"primary_stems", "core_stems"}) {
    for (const auto& stem : zone.value(key, nlohmann::json::array())) {
      if (stem.is_string() && !stem.get<std::string>().empty()) {
        return stem.get<std::string>();
      }
    }
  }
  return {};
}

bool atlas_stem_is_ui_object(const std::string& stem) {
  return causal_hay_has(causal_lower(stem), {"modal", "overlay", "strip", "layout", "settings",
                                            "file_tree", "quit_confirm", "editor_panel",
                                            "editor_text", "busy_strip"});
}

std::vector<std::string> atlas_peek_pool(const nlohmann::json& zone) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  auto push = [&](const std::string& target) {
    const std::string s = causal_short_target(target);
    if (s.empty() || !seen.insert(causal_lower(s)).second) {
      return;
    }
    out.push_back(s);
  };
  for (const auto& card : zone.value("representatives", nlohmann::json::array())) {
    push(card.value("target", ""));
  }
  const auto roles = zone.value("roles", nlohmann::json::object());
  for (const auto& writer : roles.value("writers", nlohmann::json::array())) {
    push(writer.value("target", ""));
  }
  return out;
}

std::string atlas_sym_bucket(const std::string& short_t) {
  std::string s = causal_lower(short_t);
  const auto pos = s.rfind("::");
  if (pos != std::string::npos) {
    s = s.substr(pos + 2);
  }
  if (s.size() > 10) {
    s.resize(10);
  }
  return s;
}

bool atlas_peek_is_entry(const std::string& short_t) {
  return causal_hay_has(causal_lower(short_t), {"make", "append_", "open_", "overlay", "toolpack",
                                               "switch_top", "render_top", "apply_", "save",
                                               "load", "begin_"});
}

std::vector<std::string> atlas_diverse_peeks(const nlohmann::json& zone) {
  const auto pool = atlas_peek_pool(zone);
  std::vector<std::string> picked;
  std::unordered_set<std::string> buckets;
  auto try_add = [&](const std::string& s) {
    if (picked.size() >= 2) {
      return;
    }
    if (std::find(picked.begin(), picked.end(), s) != picked.end()) {
      return;
    }
    const std::string b = atlas_sym_bucket(s);
    if (buckets.count(b)) {
      return;
    }
    picked.push_back(s);
    buckets.insert(b);
  };
  for (const auto& s : pool) {
    if (atlas_peek_is_entry(s)) {
      try_add(s);
    }
  }
  for (const auto& s : pool) {
    try_add(s);
  }
  if (picked.empty() && !pool.empty()) {
    picked.push_back(pool.front());
  }
  return picked;
}

std::string atlas_owns_caption(const nlohmann::json& zone) {
  std::string owns = atlas_primary_stem(zone);
  for (const auto& p : atlas_diverse_peeks(zone)) {
    if (!atlas_peek_is_entry(p)) {
      continue;
    }
    if (owns.empty()) {
      owns = p;
    } else if (causal_lower(p).find(causal_lower(owns)) == std::string::npos) {
      owns += " / " + p;
    }
    break;
  }
  if (owns.size() > 88) {
    owns.resize(88);
  }
  return owns;
}

std::string atlas_not_caption(const nlohmann::json& zone, const std::string& kind) {
  if (kind == "chrome" || kind == "hole") {
    return {};
  }
  std::string blob = causal_lower(atlas_primary_stem(zone));
  for (const auto& p : atlas_peek_pool(zone)) {
    blob += " ";
    blob += causal_lower(p);
  }
  if (atlas_stem_is_ui_object(atlas_primary_stem(zone)) &&
      causal_hay_has(blob, {"cancel", "recording", "clamp_"})) {
    return "no es solo cancel/recording";
  }
  if (causal_hay_has(blob, {"hover", "clickable"})) {
    return "no es hover/chrome";
  }
  return {};
}

}  // namespace

std::string registry_causal_zone_kind(const nlohmann::json& zone) {
  std::string hay;
  auto add = [&](const std::string& s) {
    if (!s.empty()) {
      hay += " ";
      hay += causal_lower(s);
    }
  };
  for (const auto& risk : zone.value("risks", nlohmann::json::array())) {
    if (risk.is_string() && (risk.get<std::string>() == "promoted_from_uncovered" ||
                             risk.get<std::string>() == "uncovered_candidate")) {
      return "hole";
    }
  }
  for (const char* key : {"primary_stems", "core_stems", "context_stems"}) {
    for (const auto& stem : zone.value(key, nlohmann::json::array())) {
      if (stem.is_string()) {
        add(stem.get<std::string>());
      }
    }
  }
  bool has_nucleus = false;
  bool latch_nucleus = false;
  for (const auto& nucleus : zone.value("nuclei", nlohmann::json::array())) {
    has_nucleus = true;
    add(nucleus.value("state", ""));
    add(nucleus.value("id", ""));
    const std::string st = causal_lower(nucleus.value("state", ""));
    if (causal_hay_has(st, {"spinner", "busy", "latch", "flag", "pending", "frame"})) {
      latch_nucleus = true;
    }
  }
  const auto roles = zone.value("roles", nlohmann::json::object());
  for (const auto& writer : roles.value("writers", nlohmann::json::array())) {
    add(causal_symbol_of(writer.value("target", "")));
  }
  for (const auto& card : zone.value("representatives", nlohmann::json::array())) {
    add(causal_symbol_of(card.value("target", "")));
  }
  const bool has_ports = !zone.value("ports", nlohmann::json::array()).empty();
  if (latch_nucleus) {
    return "latch";
  }
  if (causal_hay_has(hay, {"hover", "clickable", "gutter", "ui_wake"})) {
    return "chrome";
  }
  if (atlas_stem_is_ui_object(atlas_primary_stem(zone))) {
    return "object";
  }
  if (has_ports || (has_nucleus && !latch_nucleus)) {
    return "caller";
  }
  if (causal_hay_has(hay, {"diagnostics", "tab_"})) {
    return "chrome";
  }
  if (has_nucleus) {
    return "latch";
  }
  return "other";
}

void registry_atlas_fill_expand_from(const nlohmann::json& payload,
                                     RegistryCausalTriageDecision* decision) {
  if (decision == nullptr) {
    return;
  }
  for (auto& zone : decision->zones) {
    if (!zone.expand_from.empty()) {
      continue;
    }
    for (const auto& item : payload.value("zones", nlohmann::json::array())) {
      if (item.value("id", "") != zone.id) {
        continue;
      }
      auto push_target = [&](const std::string& target) {
        if (target.empty() || zone.expand_from.size() >= 3) {
          return;
        }
        if (std::find(zone.expand_from.begin(), zone.expand_from.end(), target) !=
            zone.expand_from.end()) {
          return;
        }
        zone.expand_from.push_back(target);
      };
      std::vector<std::string> pool;
      auto add_pool = [&](const std::string& target) {
        if (!target.empty()) {
          pool.push_back(target);
        }
      };
      for (const auto& card : item.value("representatives", nlohmann::json::array())) {
        add_pool(card.value("target", ""));
      }
      const auto roles = item.value("roles", nlohmann::json::object());
      for (const auto& writer : roles.value("writers", nlohmann::json::array())) {
        add_pool(writer.value("target", ""));
      }
      for (const auto& edge : item.value("edges", nlohmann::json::array())) {
        add_pool(edge.value("from", ""));
        add_pool(edge.value("to", ""));
      }
      for (const auto& target : pool) {
        if (atlas_peek_is_entry(causal_short_target(target))) {
          push_target(target);
        }
      }
      for (const auto& target : pool) {
        push_target(target);
      }
      break;
    }
  }
}

nlohmann::json registry_atlas_overlap_add_ids(const nlohmann::json& payload,
                                            const std::string& query,
                                            const std::vector<std::string>& opened_ids) {
  nlohmann::json out = nlohmann::json::array();
  if (query.empty()) {
    return out;
  }
  const auto zones = payload.value("zones", nlohmann::json::array());
  std::unordered_set<std::string> opened(opened_ids.begin(), opened_ids.end());
  int max_opened = 0;
  for (const auto& zone : zones) {
    const std::string id = zone.value("id", "");
    if (id.empty() || opened.count(id) == 0) {
      continue;
    }
    max_opened = std::max(max_opened, registry_causal_query_zone_overlap(query, zone));
  }
  int best_closed = 0;
  std::string best_id;
  for (const auto& zone : zones) {
    const std::string id = zone.value("id", "");
    if (id.empty() || opened.count(id) != 0) {
      continue;
    }
    const int ov = registry_causal_query_zone_overlap(query, zone);
    if (ov > best_closed) {
      best_closed = ov;
      best_id = id;
    }
  }
  if (best_closed > max_opened && !best_id.empty()) {
    out.push_back(best_id);
  }
  return out;
}

std::string registry_causal_zone_id_for_stem(const nlohmann::json& payload,
                                            const std::string& stem) {
  if (stem.empty()) {
    return "";
  }
  const std::string needle = causal_lower(stem);
  const auto zones = payload.value("zones", nlohmann::json::array());
  for (const auto& zone : zones) {
    if (causal_lower(zone.value("stem", "")) == needle) {
      return zone.value("id", "");
    }
    for (const char* key : {"primary_stems", "core_stems", "context_stems"}) {
      for (const auto& s : zone.value(key, nlohmann::json::array())) {
        if (s.is_string() && causal_lower(s.get<std::string>()) == needle) {
          return zone.value("id", "");
        }
      }
    }
  }
  return "";
}

std::string registry_causal_atlas_markdown(const nlohmann::json& payload,
                                          const std::string& consulta) {
  std::ostringstream out;
  const auto zones = payload.value("zones", nlohmann::json::array());
  out << "# causal_atlas_v1\n";
  out << "view: atlas\n";
  if (!consulta.empty()) {
    out << "consulta: " << utf8_sanitize_clip(consulta, 160) << "\n";
  }
  out << "search: " << payload.value("query", "")
      << "  (retrieval; no son módulos)\n";
  out << "n=" << zones.size()
      << "  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)\n";
  std::unordered_map<std::string, std::string> stem_first_id;
  for (const auto& zone : zones) {
    const std::string id = zone.value("id", "?");
    const std::string kind = registry_causal_zone_kind(zone);
    const std::string pstem = atlas_primary_stem(zone);
    if (!pstem.empty()) {
      auto it = stem_first_id.find(causal_lower(pstem));
      if (it != stem_first_id.end()) {
        out << "\n" << id << "  same=" << it->second << "  kind=" << kind;
        if (!consulta.empty()) {
          out << "  ov=" << registry_causal_query_zone_overlap(consulta, zone);
        }
        out << "  stems: " << pstem
            << "\n";
        const auto peeks = atlas_diverse_peeks(zone);
        if (!peeks.empty()) {
          out << "    peek: " << peeks.front() << "\n";
        }
        continue;
      }
      stem_first_id.emplace(causal_lower(pstem), id);
    }
    out << "\n" << id << "  kind=" << kind;
    if (!consulta.empty()) {
      out << "  ov=" << registry_causal_query_zone_overlap(consulta, zone);
    }
    out << "  stems:";
    int stem_n = 0;
    for (const char* key : {"primary_stems", "core_stems"}) {
      for (const auto& stem : zone.value(key, nlohmann::json::array())) {
        if (stem.is_string() && stem_n < 4) {
          out << " " << stem.get<std::string>();
          ++stem_n;
        }
      }
    }
    out << "\n";
    const std::string owns = atlas_owns_caption(zone);
    if (!owns.empty()) {
      out << "    owns: " << owns << "\n";
    }
    const std::string not_cap = atlas_not_caption(zone, kind);
    if (!not_cap.empty()) {
      out << "    not: " << not_cap << "\n";
    }
    std::string nucleus;
    for (const auto& item : zone.value("nuclei", nlohmann::json::array())) {
      const std::string state = item.value("state", "");
      if (!state.empty()) {
        if (!nucleus.empty()) {
          nucleus += ", ";
        }
        nucleus += state;
      }
      if (nucleus.size() > 60) {
        break;
      }
    }
    const auto missing = zone.value("pack_meta", nlohmann::json::object())
                             .value("skeleton_missing", nlohmann::json::array());
    std::string gap;
    for (const auto& miss : missing) {
      if (miss.is_string()) {
        if (!gap.empty()) {
          gap += ",";
        }
        gap += miss.get<std::string>();
      }
    }
    for (const auto& risk : zone.value("risks", nlohmann::json::array())) {
      if (risk.is_string() && risk.get<std::string>() == "no_state_nucleus" && gap.empty()) {
        gap = "no_state";
      }
    }
    if (!nucleus.empty()) {
      out << "    nucleus: " << nucleus << "\n";
    }
    if (!gap.empty()) {
      out << "    gap: " << gap << "\n";
    }
    const auto peeks = atlas_diverse_peeks(zone);
    if (!peeks.empty()) {
      out << "    peek:";
      for (std::size_t i = 0; i < peeks.size(); ++i) {
        if (i) {
          out << ",";
        }
        out << " " << peeks[i];
      }
      out << "\n";
    }
    const auto ports = zone.value("ports", nlohmann::json::array());
    if (!ports.empty()) {
      const auto& edge = ports.front();
      out << "    port: " << edge.value("from_zone", "?") << "=>" << edge.value("to_zone", "?")
          << " " << causal_short_target(edge.value("from", "?")) << " -"
          << edge.value("kind", "?") << "-> " << causal_short_target(edge.value("to", "?"))
          << "\n";
    } else {
      const auto edges = zone.value("edges", nlohmann::json::array());
      if (!edges.empty()) {
        const auto& edge = edges.front();
        out << "    peek-edge: " << causal_short_target(edge.value("from", "?")) << " -"
            << edge.value("kind", "?") << "-> " << causal_short_target(edge.value("to", "?"))
            << "\n";
      }
    }
  }
  const auto bridges = payload.value("zone_bridges", nlohmann::json::array());
  if (!bridges.empty()) {
    out << "\nbridges:";
    int n = 0;
    for (const auto& bridge : bridges) {
      if (n++ >= 4) {
        break;
      }
      out << " " << bridge.value("trail", "?") << "[";
      bool first = true;
      for (const auto& z : bridge.value("zones", nlohmann::json::array())) {
        if (!first) {
          out << ",";
        }
        first = false;
        if (z.is_string()) {
          out << z.get<std::string>();
        }
      }
      out << "]";
    }
    out << "\n";
  }
  const auto uncovered = payload.value("uncovered_seeds", nlohmann::json::array());
  if (!uncovered.empty()) {
    out << "holes:";
    int n = 0;
    for (const auto& seed : uncovered) {
      if (n++ >= 4) {
        break;
      }
      out << " " << causal_short_target(seed.value("target", "?"));
    }
    out << "\n";
  }
  const std::string body = out.str();
  out << "<!-- budget: chars=" << body.size() << " token_est_4c=" << (body.size() + 3) / 4
      << " -->\n";
  return out.str();
}

std::string registry_causal_pilot_opened_pack(const nlohmann::json& payload,
                                             const std::vector<std::string>& ids,
                                             const std::string& query) {
  std::ostringstream out;
  out << "# pilot_opened_v1\n";
  out << "n=" << ids.size() << "  (owns+nucleus+peek+port; sin inspect gordo)\n";
  std::unordered_map<std::string, nlohmann::json> by_id;
  for (const auto& zone : payload.value("zones", nlohmann::json::array())) {
    const std::string id = zone.value("id", "");
    if (!id.empty()) {
      by_id[id] = zone;
    }
  }
  for (const auto& want : ids) {
    auto it = by_id.find(want);
    if (it == by_id.end()) {
      out << "\n" << want << "  (id no está en el pack)\n";
      continue;
    }
    const auto& zone = it->second;
    const std::string kind = registry_causal_zone_kind(zone);
    const std::string pstem = atlas_primary_stem(zone);
    out << "\n" << want << "  kind=" << kind;
    if (!query.empty()) {
      out << "  ov=" << registry_causal_query_zone_overlap(query, zone);
    }
    out << "\n";
    const std::string owns = atlas_owns_caption(zone);
    out << "    owns: " << (owns.empty() ? (pstem.empty() ? "?" : pstem) : owns) << "\n";
    const std::string not_cap = atlas_not_caption(zone, kind);
    if (!not_cap.empty()) {
      out << "    not: " << not_cap << "\n";
    }
    std::string nucleus;
    for (const auto& item : zone.value("nuclei", nlohmann::json::array())) {
      const std::string state = item.value("state", "");
      if (state.empty()) {
        continue;
      }
      if (!nucleus.empty()) {
        nucleus += ", ";
      }
      nucleus += state;
      if (nucleus.size() > 72) {
        break;
      }
    }
    if (!nucleus.empty()) {
      out << "    nucleus: " << nucleus << "\n";
    }
    const auto peeks = atlas_diverse_peeks(zone);
    if (!peeks.empty()) {
      out << "    peek:";
      for (std::size_t i = 0; i < peeks.size() && i < 2; ++i) {
        if (i) {
          out << ",";
        }
        out << " " << peeks[i];
      }
      out << "\n";
    }
    const auto ports = zone.value("ports", nlohmann::json::array());
    if (!ports.empty()) {
      const auto& edge = ports.front();
      out << "    port: " << causal_short_target(edge.value("from", "?")) << " -"
          << edge.value("kind", "?") << "-> " << causal_short_target(edge.value("to", "?"))
          << "\n";
    }
  }
  return out.str();
}

std::string registry_causal_pack_markdown(const nlohmann::json& payload, GraphViewLevel level) {
  switch (level) {
    case GraphViewLevel::Atlas:
      return registry_causal_atlas_markdown(payload);
    case GraphViewLevel::Inspect:
      return registry_strip_zone_scores_markdown(registry_causal_judge_markdown(payload));
    case GraphViewLevel::Deep:
      return registry_causal_judge_markdown(payload);
  }
  return registry_causal_atlas_markdown(payload);
}

std::string registry_causal_atlas_survey_system_prompt() {
  return R"(Eres un encuestador de un ATLAS de zonas de código, no un juez final.
El atlas es un esquema: owns = objeto que posee la zona; not = lo que NO es; same=M* = clon.
No corones ancla ni des f1_done. search: son términos de retrieval, NO módulos.

Elige 1–2 M* cuyo owns cubra el OBJETO de la consulta (modal, panel, latch, entrypoint):
- Prefiere el owns/peek que más solape con la consulta. kind=object/latch es desempate, no el criterio.
- Si hay same=M1, inspecciona M1 no el clon.
- No sustituyas el objeto por un vecino (download, hover, ai_trace, bindings path, recording).
- Un 3º id solo si es hilo rival (latch vs caller). PROHIBIDO 3 hilos "por si acaso".
- kind=chrome NO explica drag-drop, compile, settings ni LSP.
- Si ningún owns cubre el objeto: inspect=[] y retrieval_needed=true.

view=inspect o deep. Devuelve SOLO este JSON:
{"action":"causal_atlas_survey_v1","inspect":["M1"],"view":"inspect","retrieval_needed":false,"why":"razón concreta de 8-240 chars"}
También vale inspect como objetos {id,need}. Máximo 3 ids. Solo ids que aparecen en el atlas.)";
}

std::string registry_causal_atlas_survey_user_prompt(const std::string& atlas_markdown) {
  std::ostringstream out;
  out << "Cubre el objeto de la consulta usando owns/not y el ov= (solape). "
         "Si same=M*, elige el primero. No inventes ids.\n\n"
      << atlas_markdown;
  return out.str();
}

std::string registry_causal_atlas_cover_system_prompt() {
  return R"(Eres un revisor de COBERTURA de un atlas, no un ranking ni un formulador de hyps.
Decide si las zonas ya abiertas cubren el OBJETO (owns) de la consulta. Si no, add ids
de la lista Ids restantes. PROHIBIDO inventar M13 u otros ids que no estén en esa lista.

Reglas:
- covers=true solo si owns de lo abierto es el modal/panel/latch/entrypoint pedido.
- Hover no cubre drag-drop. SHA256 no cubre toolpacks. Recording/bindings.json no cubre
  abrir settings. Highlight scheduler no cubre "animación al aplicar un edit" si hay editor_panel.
- Resaltado que tarda: visual_highlight SÍ puede cubrir si no hay mejor owns de editor.
- Si un Restante tiene ov= mayor que lo abierto (owns/peek vs consulta), add ese id.
- add: 1–2 ids de Ids restantes (3 si no hay ninguna abierta). Vacío si covers=true.
- Si el objeto no está en el atlas: add=[] (no inventes ids).

Devuelve SOLO este JSON:
{"action":"causal_atlas_cover_v1","covers":false,"add":["M1"],"why":"razón concreta de 8-240 chars"})";
}

std::string registry_causal_atlas_cover_user_prompt(const std::string& atlas_markdown,
                                                    const std::vector<std::string>& opened,
                                                    const std::vector<std::string>& remaining,
                                                    const std::string& inspect_markdown) {
  std::ostringstream out;
  out << "Zonas ya ampliadas: ";
  if (opened.empty()) {
    out << "(ninguna)\n";
  } else {
    for (std::size_t i = 0; i < opened.size(); ++i) {
      if (i > 0) {
        out << ", ";
      }
      out << opened[i];
    }
    out << "\n";
  }
  out << "Ids restantes (SOLO estos en add): ";
  if (remaining.empty()) {
    out << "(ninguno)\n";
  } else {
    for (std::size_t i = 0; i < remaining.size(); ++i) {
      if (i > 0) {
        out << ", ";
      }
      out << remaining[i];
    }
    out << "\n";
  }
  out << "¿Cubren el objeto (owns) de la consulta? Si un Restante solapa más (ov=), add. "
         "No inventes ids.\n\n"
      << atlas_markdown;
  if (!inspect_markdown.empty()) {
    out << "\n\nFichas ya ampliadas:\n";
    constexpr std::size_t kCap = 5000;
    out << utf8_sanitize_clip(inspect_markdown, kCap);
    if (inspect_markdown.size() > kCap) {
      out << "\n…\n";
    } else {
      out << "\n";
    }
  }
  return out.str();
}

namespace {

bool hyp_json_bool(const nlohmann::json& o, const char* key, bool fallback) {
  if (!o.contains(key) || o[key].is_null()) {
    return fallback;
  }
  if (o[key].is_boolean()) {
    return o[key].get<bool>();
  }
  if (o[key].is_number_integer()) {
    return o[key].get<int>() != 0;
  }
  if (o[key].is_string()) {
    const std::string s = causal_lower(o[key].get<std::string>());
    return s == "true" || s == "yes" || s == "1";
  }
  return fallback;
}

}  // namespace

RegistryCausalAtlasCoverDecision registry_parse_causal_atlas_cover(
    const std::string& raw, const std::vector<std::string>& allowed_zone_ids,
    const std::vector<std::string>& already_open) {
  RegistryCausalAtlasCoverDecision out;
  out.raw = raw;
  const auto begin = raw.find('{');
  const auto end = raw.rfind('}');
  if (begin == std::string::npos || end == std::string::npos || end <= begin) {
    out.error = "atlas cover sin objeto JSON";
    return out;
  }
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(raw.substr(begin, end - begin + 1));
  } catch (const std::exception& e) {
    out.error = std::string("JSON atlas cover inválido: ") + e.what();
    return out;
  }
  if (j.value("action", "") != "causal_atlas_cover_v1") {
    out.error = "contrato causal_atlas_cover_v1 inválido";
    return out;
  }
  out.covers = hyp_json_bool(j, "covers", false);
  out.why = j.value("why", "");
  const std::unordered_set<std::string> allowed(allowed_zone_ids.begin(),
                                                allowed_zone_ids.end());
  const std::unordered_set<std::string> open(already_open.begin(), already_open.end());
  const int max_add = already_open.empty() ? 3 : 2;
  if (j.contains("add") && j["add"].is_array()) {
    for (const auto& item : j["add"]) {
      std::string id;
      if (item.is_string()) {
        id = item.get<std::string>();
      } else if (item.is_object()) {
        id = item.value("id", "");
      }
      if (id.empty() || !allowed.count(id) || open.count(id)) {
        continue;
      }
      if (std::find(out.add.begin(), out.add.end(), id) != out.add.end()) {
        continue;
      }
      out.add.push_back(id);
      if (static_cast<int>(out.add.size()) >= max_add) {
        break;
      }
    }
  }
  if (out.covers) {
    out.add.clear();
  }
  if (out.why.size() < 8) {
    out.why = out.covers ? "el pack cubre el objeto de la consulta"
                         : "ampliar zona cuyo peek cubre el objeto";
  }
  if (out.why.size() > 240) {
    out.error = "why de cover inválido";
    return out;
  }
  out.ok = true;
  return out;
}

int registry_atlas_merge_inspect_ids(RegistryCausalTriageDecision* decision,
                                     const std::vector<std::string>& add_ids,
                                     const std::vector<std::string>& allowed_zone_ids,
                                     int max_total) {
  if (decision == nullptr || max_total <= 0) {
    return 0;
  }
  const std::unordered_set<std::string> allowed(allowed_zone_ids.begin(),
                                                allowed_zone_ids.end());
  std::unordered_set<std::string> have(decision->shortlist.begin(), decision->shortlist.end());
  int added = 0;
  for (const auto& id : add_ids) {
    if (static_cast<int>(decision->shortlist.size()) >= max_total) {
      break;
    }
    if (id.empty() || !allowed.count(id) || have.count(id)) {
      continue;
    }
    RegistryZoneTriage zone;
    zone.id = id;
    zone.verdict = "inspect";
    zone.need = "cobertura: peek cubre el objeto de la consulta";
    decision->shortlist.push_back(id);
    decision->zones.push_back(std::move(zone));
    have.insert(id);
    ++added;
  }
  if (!decision->shortlist.empty()) {
    decision->ok = true;
    if (decision->why.size() < 8) {
      decision->why = "inspeccionar cobertura del atlas";
    }
  }
  return added;
}

std::string registry_causal_zone_hyp_system_prompt() {
  return R"(Eres un formulador de HIPÓTESIS DE FALLO sobre código, no un ranking ni un juez final.
Recibes fichas INSPECT. NO te autoevalúes con un número de confianza: o anclas al objeto
o pides ampliar fichas. Una hyp rellena sobre un vecino con más "mechanism" no vale.

Reglas:
- owns: de cada ficha es el objeto. Si coincide con la consulta, ancla AHÍ (affected).
- Cada slot cita stem/path_symbol VISIBLE en owns/stems/peeks. null si no aparece.
- Prohibido glosa de la consulta como stem. Prohibido copiar ejemplos de este prompt.
- Si el pack NO cubre el objeto, o te falta el eslabón: need_more (NO rellenes un vecino).
  add = ids de la lista Restantes; view=deep para ahondar las fichas ya abiertas;
  expand_from = peeks de la ficha del hueco.
- La 2ª hyp SOLO si es un mecanismo rival. Chrome hover ≠ drag-drop; highlight ≠ editor hole.

Devuelve SOLO uno de estos JSON:
{"action":"causal_zone_hyp_v1","hypotheses":[{"claim":"el objeto X no hace Y al evento Z","slots":{"affected":{"stem":"STEM_DE_OWNS","path_symbol":"file::entry"},"control":null,"trigger":null,"cleanup":null},"gap":"trigger","anchor_role":"affected","falsify_by":"si entry corre al evento, hyp muere","why":"M* posee STEM_DE_OWNS"}],"why":"objeto y eslabón visibles"}
{"action":"causal_zone_hyp_v1","need_more":true,"add":["M3"],"view":"deep","expand_from":[],"hypotheses":[],"why":"las fichas abiertas no cubren el objeto de la consulta"}
Máximo 2 hyps. claim/why/falsify_by: 12–160 caracteres.)";
}

std::string registry_causal_zone_hyp_user_prompt(const std::string& inspect_markdown,
                                                 const std::vector<std::string>& remaining_ids) {
  std::ostringstream out;
  out << "Con estas fichas, o anclas una hyp al owns del objeto, o pides need_more.\n"
         "No rellenes un vecino 'más mecanismo'. No copies el ejemplo del sistema.\n";
  if (!remaining_ids.empty()) {
    out << "Ids restantes para add:";
    for (const auto& id : remaining_ids) {
      out << " " << id;
    }
    out << "\n";
  }
  out << "\n" << inspect_markdown;
  return out.str();
}

namespace {

std::string hyp_stem_from_target(const std::string& target) {
  const auto dbl = target.find("::");
  if (dbl != std::string::npos) {
    return target.substr(0, dbl);
  }
  const auto colon = target.rfind(':');
  const std::string path = colon == std::string::npos ? target : target.substr(0, colon);
  const std::string stem = registry_stem_of(path);
  if (!stem.empty()) {
    return stem;
  }
  return causal_symbol_of(target);
}

void hyp_collect_menu(const nlohmann::json& payload, std::unordered_set<std::string>* stems,
                      std::unordered_set<std::string>* targets,
                      std::unordered_map<std::string, std::string>* short_to_full) {
  auto add_stem = [&](const std::string& s) {
    if (!s.empty()) {
      stems->insert(causal_lower(s));
    }
  };
  auto add_target = [&](const std::string& t) {
    if (t.empty()) {
      return;
    }
    targets->insert(t);
    const std::string shortened = causal_short_target(t);
    targets->insert(shortened);
    if (short_to_full != nullptr && short_to_full->find(shortened) == short_to_full->end()) {
      (*short_to_full)[shortened] = t;
    }
    add_stem(hyp_stem_from_target(t));
    add_stem(causal_symbol_of(t));
  };
  for (const auto& zone : payload.value("zones", nlohmann::json::array())) {
    for (const char* key : {"primary_stems", "core_stems", "context_stems"}) {
      for (const auto& stem : zone.value(key, nlohmann::json::array())) {
        if (stem.is_string()) {
          add_stem(stem.get<std::string>());
        }
      }
    }
    for (const auto& nucleus : zone.value("nuclei", nlohmann::json::array())) {
      add_stem(nucleus.value("state", ""));
    }
    for (const auto& card : zone.value("representatives", nlohmann::json::array())) {
      add_target(card.value("target", ""));
    }
    const auto roles = zone.value("roles", nlohmann::json::object());
    for (const char* role : {"writers", "readers", "controls", "handoffs"}) {
      for (const auto& item : roles.value(role, nlohmann::json::array())) {
        add_target(item.value("target", ""));
      }
    }
    for (const auto& edge : zone.value("edges", nlohmann::json::array())) {
      add_target(edge.value("from", ""));
      add_target(edge.value("to", ""));
    }
    for (const auto& edge : zone.value("ports", nlohmann::json::array())) {
      add_target(edge.value("from", ""));
      add_target(edge.value("to", ""));
    }
  }
}

std::string hyp_json_string(const nlohmann::json& o, const char* key) {
  if (!o.contains(key) || o[key].is_null() || !o[key].is_string()) {
    return "";
  }
  return o[key].get<std::string>();
}

bool hyp_fill_slot(HypSlotLocus* slot, const nlohmann::json& item, const std::string& declared_id,
                   const std::unordered_set<std::string>& stems,
                   const std::unordered_set<std::string>& targets,
                   const std::unordered_map<std::string, std::string>& short_to_full,
                   std::string* error) {
  if (slot == nullptr) {
    return false;
  }
  if (item.is_null() || item.empty()) {
    return true;
  }
  if (!item.is_object()) {
    *error = "slot inválido en " + declared_id;
    return false;
  }
  slot->stem = hyp_json_string(item, "stem");
  slot->path_symbol = hyp_json_string(item, "path_symbol");
  if (item.contains("from_map") && item["from_map"].is_number_integer()) {
    slot->from_map = item["from_map"].get<int>();
  }
  if (slot->stem.empty() && slot->path_symbol.empty()) {
    return true;
  }
  if (!slot->path_symbol.empty()) {
    auto it = short_to_full.find(slot->path_symbol);
    if (it != short_to_full.end()) {
      slot->path_symbol = it->second;
    }
    if (!targets.count(slot->path_symbol)) {
      slot->path_symbol.clear();
      slot->stem.clear();
      return true;
    }
    if (slot->stem.empty() || !stems.count(causal_lower(slot->stem))) {
      slot->stem = hyp_stem_from_target(slot->path_symbol);
    }
  }
  if (!slot->stem.empty() && !stems.count(causal_lower(slot->stem))) {
    slot->stem.clear();
    slot->path_symbol.clear();
  }
  return true;
}

bool hyp_kind_objectish(const std::string& kind) {
  return kind == "object" || kind == "latch" || kind == "hole";
}

int hyp_count_grounded(const AnchorHypothesis& h) {
  int n = 0;
  auto filled = [](const HypSlotLocus& s) { return !s.stem.empty() || !s.path_symbol.empty(); };
  if (filled(h.affected)) {
    ++n;
  }
  if (filled(h.control)) {
    ++n;
  }
  if (filled(h.trigger)) {
    ++n;
  }
  if (filled(h.cleanup)) {
    ++n;
  }
  return n;
}

std::string hyp_anchor_stem(const AnchorHypothesis& h) {
  if (!h.affected.stem.empty()) {
    return h.affected.stem;
  }
  if (!h.control.stem.empty()) {
    return h.control.stem;
  }
  if (!h.trigger.stem.empty()) {
    return h.trigger.stem;
  }
  return h.cleanup.stem;
}

bool hyp_zone_has_stem(const nlohmann::json& zone, const std::string& stem) {
  const std::string needle = causal_lower(stem);
  if (needle.empty()) {
    return false;
  }
  auto hit = [&](const std::string& s) {
    const std::string l = causal_lower(s);
    return !l.empty() && (l == needle || l.find(needle) != std::string::npos ||
                          needle.find(l) != std::string::npos);
  };
  for (const char* key : {"primary_stems", "core_stems"}) {
    for (const auto& s : zone.value(key, nlohmann::json::array())) {
      if (s.is_string() && hit(s.get<std::string>())) {
        return true;
      }
    }
  }
  return hit(atlas_primary_stem(zone)) || hit(atlas_owns_caption(zone));
}

RegistryCausalHypMass hyp_score_one(const AnchorHypothesis& h, const nlohmann::json& payload) {
  RegistryCausalHypMass m;
  m.grounded_slots = hyp_count_grounded(h);
  m.honest_gap = m.grounded_slots == 0 && (h.gap == "affected" || h.gap == "consumer");
  const std::string anchor = hyp_anchor_stem(h);
  bool has_objectish_other = false;
  for (const auto& zone : payload.value("zones", nlohmann::json::array())) {
    const std::string kind = registry_causal_zone_kind(zone);
    const bool mine = !anchor.empty() && hyp_zone_has_stem(zone, anchor);
    if (hyp_kind_objectish(kind) && mine) {
      m.owns_ok = true;
    }
    if (hyp_kind_objectish(kind) && !mine) {
      has_objectish_other = true;
    }
  }
  m.neighbor_fill = !anchor.empty() && has_objectish_other && !m.owns_ok;
  float s = 0.12f * static_cast<float>(m.grounded_slots);
  if (m.owns_ok) {
    s += 0.50f;
  }
  if (m.neighbor_fill) {
    s = std::min(s, 0.45f);
  }
  if (m.honest_gap) {
    s = std::min(s, 0.22f);
  }
  m.score = std::max(0.f, std::min(1.f, s));
  if (m.owns_ok && m.grounded_slots >= 1 && !m.neighbor_fill && m.score >= 0.55f) {
    m.band = "high";
  } else if (m.score >= 0.28f || m.grounded_slots >= 2) {
    m.band = "medium";
  } else {
    m.band = "low";
  }
  if (m.honest_gap) {
    m.why = "hueco declarado sin slots grounded";
  } else if (m.neighbor_fill) {
    m.why = "ancla en vecino; el pack ya tiene un objeto/latch/hueco distinto";
  } else if (m.owns_ok) {
    m.why = "ancla en objeto/latch/hueco de las fichas";
  } else {
    m.why = "ancla sin owns objectish";
  }
  return m;
}

void hyp_score_decision(RegistryCausalHypDecision* out, const nlohmann::json& inspect_payload) {
  if (out == nullptr) {
    return;
  }
  out->masses.clear();
  out->mass = 0.f;
  out->mass_band = "low";
  bool any_high = false;
  for (const auto& h : out->hypotheses) {
    auto m = hyp_score_one(h, inspect_payload);
    if (m.score > out->mass) {
      out->mass = m.score;
      out->mass_band = m.band;
    } else if (m.score == out->mass && m.band == "high") {
      out->mass_band = "high";
    }
    if (m.band == "high") {
      any_high = true;
    }
    out->masses.push_back(std::move(m));
  }
  if (any_high) {
    out->mass_band = "high";
    out->ok = out->parsed;
    out->need_more = false;
  } else {
    out->ok = false;
    if (out->parsed) {
      out->need_more = true;
      if (out->why.size() < 8) {
        out->why = "masa insuficiente: ampliar fichas";
      }
    }
  }
}

}  // namespace

RegistryCausalHypDecision registry_parse_causal_zone_hyp(
    const std::string& raw, const nlohmann::json& inspect_payload,
    const std::vector<std::string>& atlas_zone_ids) {
  RegistryCausalHypDecision out;
  out.raw = raw;
  out.action = "causal_zone_hyp_v1";
  const auto begin = raw.find('{');
  const auto end = raw.rfind('}');
  if (begin == std::string::npos || end == std::string::npos || end <= begin) {
    out.error = "hyp sin objeto JSON";
    return out;
  }
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(raw.substr(begin, end - begin + 1));
  } catch (const std::exception& e) {
    out.error = std::string("JSON hyp inválido: ") + e.what();
    return out;
  }
  const std::string action = j.value("action", "");
  if (action != "causal_zone_hyp_v1" && action != "causal_zone_hyp_need_more_v1") {
    out.error = "contrato causal_zone_hyp_v1 inválido";
    return out;
  }
  out.need_more = j.value("need_more", false) || action == "causal_zone_hyp_need_more_v1";
  out.view = causal_lower(hyp_json_string(j, "view"));
  if (out.view != "inspect" && out.view != "deep") {
    out.view.clear();
  }
  std::unordered_set<std::string> allowed_ids;
  for (const auto& id : atlas_zone_ids) {
    if (!id.empty()) {
      allowed_ids.insert(id);
    }
  }
  for (const auto& zone : inspect_payload.value("zones", nlohmann::json::array())) {
    const std::string id = zone.value("id", "");
    if (!id.empty()) {
      allowed_ids.insert(id);
    }
  }
  if (j.contains("add") && j["add"].is_array()) {
    for (const auto& id : j["add"]) {
      if (!id.is_string()) {
        continue;
      }
      const std::string s = id.get<std::string>();
      if (allowed_ids.empty() || allowed_ids.count(s)) {
        if (std::find(out.add.begin(), out.add.end(), s) == out.add.end()) {
          out.add.push_back(s);
        }
      }
    }
    if (out.add.size() > 2) {
      out.add.resize(2);
    }
  }
  std::unordered_set<std::string> stems;
  std::unordered_set<std::string> targets;
  std::unordered_map<std::string, std::string> short_to_full;
  hyp_collect_menu(inspect_payload, &stems, &targets, &short_to_full);
  if (j.contains("expand_from") && j["expand_from"].is_array()) {
    for (const auto& t : j["expand_from"]) {
      if (!t.is_string()) {
        continue;
      }
      std::string s = t.get<std::string>();
      auto it = short_to_full.find(s);
      if (it != short_to_full.end()) {
        s = it->second;
      }
      if (targets.count(s) || allowed_ids.empty()) {
        if (std::find(out.expand_from.begin(), out.expand_from.end(), s) ==
            out.expand_from.end()) {
          out.expand_from.push_back(s);
        }
      }
      if (out.expand_from.size() >= 3) {
        break;
      }
    }
  }
  static const std::unordered_set<std::string> kRoles = {
      "trigger", "control", "cleanup", "affected", "consumer", "state"};
  nlohmann::json items = nlohmann::json::array();
  if (j.contains("hypotheses") && j["hypotheses"].is_array()) {
    items = j["hypotheses"];
  } else if (j.contains("anchor_hypotheses") && j["anchor_hypotheses"].is_array()) {
    items = j["anchor_hypotheses"];
  }
  if (items.size() > 2) {
    items.erase(items.begin() + 2, items.end());
  }
  for (const auto& item : items) {
    if (!item.is_object()) {
      continue;
    }
    AnchorHypothesis hyp;
    hyp.claim = hyp_json_string(item, "claim");
    if (hyp.claim.empty()) {
      hyp.claim = hyp_json_string(item, "objective");
    }
    hyp.objective = hyp.claim;
    hyp.why = hyp_json_string(item, "why");
    hyp.falsify_by = hyp_json_string(item, "falsify_by");
    hyp.gap = causal_lower(hyp_json_string(item, "gap"));
    hyp.anchor_role = causal_lower(hyp_json_string(item, "anchor_role"));
    if (hyp.claim.size() < 12 || hyp.claim.size() > 160) {
      continue;
    }
    if (hyp.why.size() > 160 || hyp.falsify_by.size() > 160) {
      continue;
    }
    if (kRoles.count(hyp.gap) == 0) {
      continue;
    }
    if (hyp.anchor_role.empty()) {
      hyp.anchor_role = hyp.gap == "state" ? "affected" : hyp.gap;
    }
    if (hyp.anchor_role == "state") {
      hyp.anchor_role = "control";
    }
    if (kRoles.count(hyp.anchor_role) == 0 && hyp.anchor_role != "affected") {
      continue;
    }
    nlohmann::json slots = nlohmann::json::object();
    if (item.contains("slots") && item["slots"].is_object()) {
      slots = item["slots"];
    }
    std::string slot_err;
    if (!hyp_fill_slot(&hyp.affected, slots.value("affected", nlohmann::json()), "affected", stems,
                       targets, short_to_full, &slot_err) ||
        !hyp_fill_slot(&hyp.control, slots.value("control", nlohmann::json()), "control", stems,
                       targets, short_to_full, &slot_err) ||
        !hyp_fill_slot(&hyp.trigger, slots.value("trigger", nlohmann::json()), "trigger", stems,
                       targets, short_to_full, &slot_err) ||
        !hyp_fill_slot(&hyp.cleanup, slots.value("cleanup", nlohmann::json()), "cleanup", stems,
                       targets, short_to_full, &slot_err)) {
      continue;
    }
    auto push_term = [&](const std::string& t) {
      if (t.empty()) {
        return;
      }
      if (std::find(hyp.search_terms.begin(), hyp.search_terms.end(), t) ==
          hyp.search_terms.end()) {
        hyp.search_terms.push_back(t);
      }
    };
    push_term(hyp.affected.stem);
    push_term(hyp.control.stem);
    push_term(hyp.trigger.stem);
    push_term(hyp.cleanup.stem);
    if (hyp.search_terms.size() > 4) {
      hyp.search_terms.resize(4);
    }
    out.hypotheses.push_back(std::move(hyp));
  }
  out.why = hyp_json_string(j, "why");
  if (out.hypotheses.empty() && !out.need_more) {
    out.error = "resultado de hyp no accionable";
    return out;
  }
  if (out.why.size() < 8 && !out.hypotheses.empty()) {
    out.why = "hipótesis anclada a fichas inspect";
  }
  if (out.why.size() < 8 || out.why.size() > 240) {
    out.error = "resultado de hyp no accionable";
    return out;
  }
  out.parsed = true;
  hyp_score_decision(&out, inspect_payload);
  if (j.value("need_more", false) || action == "causal_zone_hyp_need_more_v1") {
    out.need_more = true;
    out.ok = false;
  }
  return out;
}

void registry_score_causal_zone_hyp(RegistryCausalHypDecision* decision,
                                    const nlohmann::json& inspect_payload) {
  hyp_score_decision(decision, inspect_payload);
}

std::vector<std::string> registry_atlas_suggest_cover_ids(const nlohmann::json& atlas_payload,
                                                         const std::vector<std::string>& opened,
                                                         int max_n) {
  std::unordered_set<std::string> have(opened.begin(), opened.end());
  std::vector<std::string> object_ids;
  std::vector<std::string> latch_ids;
  std::vector<std::string> hole_ids;
  for (const auto& zone : atlas_payload.value("zones", nlohmann::json::array())) {
    const std::string id = zone.value("id", "");
    if (id.empty() || have.count(id)) {
      continue;
    }
    const std::string kind = registry_causal_zone_kind(zone);
    if (kind == "object") {
      object_ids.push_back(id);
    } else if (kind == "latch") {
      latch_ids.push_back(id);
    } else if (kind == "hole") {
      hole_ids.push_back(id);
    }
  }
  std::vector<std::string> out;
  auto take = [&](const std::vector<std::string>& ids) {
    for (const auto& id : ids) {
      if (static_cast<int>(out.size()) >= max_n) {
        return;
      }
      out.push_back(id);
    }
  };
  take(object_ids);
  take(latch_ids);
  take(hole_ids);
  return out;
}

nlohmann::json registry_causal_hyp_decision_to_json(const RegistryCausalHypDecision& decision) {
  nlohmann::json hyps = nlohmann::json::array();
  for (size_t i = 0; i < decision.hypotheses.size(); ++i) {
    const auto& h = decision.hypotheses[i];
    auto slot = [](const HypSlotLocus& s) -> nlohmann::json {
      if (s.stem.empty() && s.path_symbol.empty() && s.from_map <= 0) {
        return nullptr;
      }
      nlohmann::json o = nlohmann::json::object();
      if (!s.stem.empty()) {
        o["stem"] = s.stem;
      }
      if (!s.path_symbol.empty()) {
        o["path_symbol"] = s.path_symbol;
      }
      if (s.from_map > 0) {
        o["from_map"] = s.from_map;
      }
      return o;
    };
    nlohmann::json row = {{"claim", h.claim},
                          {"why", h.why},
                          {"falsify_by", h.falsify_by},
                          {"gap", h.gap},
                          {"anchor_role", h.anchor_role},
                          {"search_terms", h.search_terms},
                          {"slots",
                           {{"affected", slot(h.affected)},
                            {"control", slot(h.control)},
                            {"trigger", slot(h.trigger)},
                            {"cleanup", slot(h.cleanup)}}}};
    if (i < decision.masses.size()) {
      const auto& m = decision.masses[i];
      row["mass"] = m.score;
      row["mass_band"] = m.band;
      row["owns_ok"] = m.owns_ok;
      row["neighbor_fill"] = m.neighbor_fill;
      row["grounded_slots"] = m.grounded_slots;
      row["mass_why"] = m.why;
    }
    hyps.push_back(std::move(row));
  }
  return {{"action", decision.action.empty() ? "causal_zone_hyp_v1" : decision.action},
          {"ok", decision.ok},
          {"parsed", decision.parsed},
          {"need_more", decision.need_more},
          {"view", decision.view},
          {"add", decision.add},
          {"expand_from", decision.expand_from},
          {"mass", decision.mass},
          {"mass_band", decision.mass_band},
          {"hypotheses", hyps},
          {"why", decision.why},
          {"error", decision.error}};
}

std::string registry_causal_pilot_plan_system_prompt(bool allow_need_more) {
  std::string out = R"(Eres el PILOTO. NO formulas hipótesis de fallo. NO lees cuerpos de código.
Recibes el atlas de todas las zonas y fichas compactas (owns/nucleus/peek/port) de las ya abiertas.
Los trabajadores sordos leerán el inspect gordo. Tú solo eliges M* y verbos.

Si ya cubre el objeto, planifica encargos:
1. cubre — el ABIERTO cuyo owns/nucleus/peek es el objeto (object/latch/hole; NO chrome).
   Nucleus/peek mandan sobre ov= alto. Un hole de overlay/wizard no gana a un latch de spinner.
2. cubre — otro owns distinto (rival o Restante con peek que solape).
3. como — el barrio de plaza 1 (el objeto), no el chrome ni el hole vecino.
4. opcional: gap en un abierto, o un cubre de Restantes. NUNCA como/gap fuera de Abiertos.

question de cubre: sí/no y nombra el owns. question de como: quién + verbo de código.
PROHIBIDO: cómo/qué en cubre; dos cubre al mismo owns; chrome en plaza 1.
No copies frases de este contrato.
)";
  if (allow_need_more) {
    out += R"(
Si el OBJETO (modal, panel, latch, entrypoint) NO está en Abiertos y SÍ en el atlas:
una sola ampliación. add = 1–2 ids de Restantes. why nombra el owns que falta. PROHIBIDO inventar ids.

UNO de estos JSON:
{"action":"causal_pilot_plan_v1","tasks":[{"kind":"cubre","zone":"M1","stem":"","question":"…"}],"why":"…"}
{"action":"causal_pilot_need_more","add":["M?"],"why":"owns X de Restantes cubre el objeto"}
)";
  } else {
    out += R"(
Ya se amplió una vez. PROHIBIDO ampliar otra vez. Devuelve SOLO el plan.

JSON:
{"action":"causal_pilot_plan_v1","tasks":[{"kind":"cubre","zone":"M1","stem":"","question":"…"}],"why":"…"}
)";
  }
  return out;
}

std::string registry_causal_pilot_plan_user_prompt(
    const std::string& atlas_markdown, const std::string& opened_pack,
    const std::vector<std::string>& remaining_ids,
    const std::vector<std::string>& overlap_suggest, bool allow_need_more) {
  std::ostringstream out;
  if (allow_need_more) {
    out << "Elige plan o una ampliación. Plaza 1 = objeto de la consulta según owns/nucleus/peek "
           "(no el ov más alto). No copies plantillas.\n\n";
  } else {
    out << "Ya se amplió. PROHIBIDO add. Plaza 1 = objeto según owns/nucleus/peek "
           "(no el ov más alto). Planifica ahora. No copies plantillas.\n\n";
  }
  out << "## Atlas (todas las zonas; add solo Ids restantes)\n";
  out << (atlas_markdown.empty() ? "(vacío)\n" : atlas_markdown);
  out << "\n## Abiertos (fichas compactas ya ampliadas)\n";
  out << (opened_pack.empty() ? "(ninguno)\n" : opened_pack);
  if (allow_need_more) {
    out << "\n## Ids restantes (SOLO estos en add):";
  } else {
    out << "\n## Ids restantes (no add; cubre opcional si peek solapa):";
  }
  if (remaining_ids.empty()) {
    out << " (ninguno)\n";
  } else {
    for (const auto& id : remaining_ids) {
      out << " " << id;
    }
    out << "\n";
  }
  if (!overlap_suggest.empty()) {
    out << "solape_sugerido:";
    for (const auto& id : overlap_suggest) {
      out << " " << id;
    }
    out << "  (candidato; no está abierto hasta que lo pidas en add)\n";
  }
  return out.str();
}

namespace {

std::string pilot_zone_primary_stem(const nlohmann::json& zone) {
  for (const char* key : {"primary_stems", "core_stems"}) {
    for (const auto& s : zone.value(key, nlohmann::json::array())) {
      if (s.is_string() && !s.get<std::string>().empty()) {
        return s.get<std::string>();
      }
    }
  }
  return {};
}

bool pilot_zone_has_stem(const nlohmann::json& zone, const std::string& stem) {
  const std::string needle = causal_lower(stem);
  if (needle.empty()) {
    return false;
  }
  for (const char* key : {"primary_stems", "core_stems", "context_stems"}) {
    for (const auto& s : zone.value(key, nlohmann::json::array())) {
      if (s.is_string() && causal_lower(s.get<std::string>()) == needle) {
        return true;
      }
    }
  }
  return false;
}

bool pilot_question_is_template(const std::string& q) {
  const std::string l = causal_lower(q);
  return l.find("este owns") != std::string::npos ||
         l.find("x en este") != std::string::npos ||
         l.find("este stem") != std::string::npos;
}

bool pilot_question_is_como_shaped(const std::string& q) {
  const std::string l = causal_lower(q);
  return l.find("como se") != std::string::npos || l.find("cómo") != std::string::npos ||
         l.find("c\xc3\xb3mo") != std::string::npos || l.find("que codigo") != std::string::npos ||
         l.find("qué código") != std::string::npos || l.find("que controla") != std::string::npos ||
         l.find("qué controla") != std::string::npos || l.find("como abrir") != std::string::npos ||
         l.find("como escribir") != std::string::npos || l.find("como mostrar") != std::string::npos;
}

bool pilot_question_is_yesno(const std::string& q) {
  const std::string l = causal_lower(q);
  return l.find("¿es ") != std::string::npos || l.find("es ") == 0 ||
         l.find(" el objeto") != std::string::npos || l.find("es este") != std::string::npos;
}

int pilot_kind_rank(const std::string& kind) {
  if (kind == "object") {
    return 0;
  }
  if (kind == "latch") {
    return 1;
  }
  if (kind == "hole") {
    return 2;
  }
  if (kind == "other") {
    return 3;
  }
  if (kind == "caller") {
    return 4;
  }
  return 5;
}

std::string pilot_specialize_question(const std::string& kind, const std::string& stem) {
  if (kind == "cubre") {
    return "¿es " + stem + " el objeto de la consulta?";
  }
  if (kind == "como") {
    return "¿quién escribe, limpia o dispara la acción en " + stem + "?";
  }
  return "¿qué eslabón falta en " + stem + " para explicar la consulta?";
}

std::string pilot_normalize_question(const std::string& kind, const std::string& stem,
                                     std::string question) {
  if (pilot_question_is_template(question) || question.size() < 12 || question.size() > 160) {
    return pilot_specialize_question(kind, stem);
  }
  const std::string l = causal_lower(question);
  const bool names_stem = l.find(causal_lower(stem)) != std::string::npos;
  if (kind == "cubre") {
    if (pilot_question_is_como_shaped(question) || !pilot_question_is_yesno(question) ||
        !names_stem) {
      return pilot_specialize_question(kind, stem);
    }
    return question;
  }
  if (kind == "como") {
    const bool has_quien = l.find("quién") != std::string::npos ||
                           l.find("quien") != std::string::npos ||
                           l.find("qui\xc3\xa9n") != std::string::npos;
    if (pilot_question_is_yesno(question) || !has_quien || !names_stem) {
      return pilot_specialize_question(kind, stem);
    }
    return question;
  }
  return question;
}

}  // namespace

std::string registry_causal_pilot_barrio_menu(const nlohmann::json& payload,
                                              const std::vector<std::string>& ids,
                                              const std::string& query) {
  std::unordered_map<std::string, std::string> stem_first;
  for (const auto& zone : payload.value("zones", nlohmann::json::array())) {
    const std::string id = zone.value("id", "");
    const std::string st = causal_lower(pilot_zone_primary_stem(zone));
    if (id.empty() || st.empty() || stem_first.count(st)) {
      continue;
    }
    stem_first[st] = id;
  }
  struct Row {
    std::string line;
    int overlap = 0;
    int rank = 0;
    int idx = 0;
  };
  std::vector<Row> rows;
  int idx = 0;
  for (const auto& want : ids) {
    for (const auto& zone : payload.value("zones", nlohmann::json::array())) {
      if (zone.value("id", "") != want) {
        continue;
      }
      const std::string st = pilot_zone_primary_stem(zone);
      const std::string kind = registry_causal_zone_kind(zone);
      const int ov = query.empty() ? 0 : registry_causal_query_zone_overlap(query, zone);
      std::ostringstream line;
      line << want << "  kind=" << kind << "  owns=" << (st.empty() ? "?" : st);
      if (!query.empty()) {
        line << "  ov=" << ov;
      }
      auto it = stem_first.find(causal_lower(st));
      const bool clone = it != stem_first.end() && it->second != want;
      if (clone) {
        line << "  same=" << it->second << " (clon, no encargar)";
      }
      line << "\n";
      Row row;
      row.line = line.str();
      row.overlap = ov;
      row.rank = pilot_kind_rank(kind) + (clone ? 10 : 0);
      row.idx = idx++;
      rows.push_back(std::move(row));
      break;
    }
  }
  std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
    if (a.overlap != b.overlap) {
      return a.overlap > b.overlap;
    }
    if (a.rank != b.rank) {
      return a.rank < b.rank;
    }
    return a.idx < b.idx;
  });
  std::ostringstream out;
  for (const auto& row : rows) {
    out << row.line;
  }
  return out.str();
}

RegistryCausalPilotPlan registry_parse_causal_pilot_plan(
    const std::string& raw, const nlohmann::json& atlas_payload,
    const std::vector<std::string>& atlas_zone_ids, const std::vector<std::string>& opened_ids,
    const std::string& query, bool allow_need_more) {
  RegistryCausalPilotPlan out;
  out.raw = raw;
  out.action = "causal_pilot_plan_v1";
  const auto begin = raw.find('{');
  const auto end = raw.rfind('}');
  if (begin == std::string::npos || end == std::string::npos || end <= begin) {
    out.error = "plan sin objeto JSON";
    return out;
  }
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(raw.substr(begin, end - begin + 1));
  } catch (const std::exception& e) {
    out.error = std::string("JSON plan inválido: ") + e.what();
    return out;
  }
  const std::string action = j.value("action", "");
  if (action == "causal_pilot_need_more") {
    out.action = action;
    out.need_more = true;
    if (!allow_need_more) {
      out.error = "need_more no permitido en este pase";
      return out;
    }
    std::unordered_set<std::string> allowed;
    std::unordered_set<std::string> opened(opened_ids.begin(), opened_ids.end());
    for (const auto& zone : atlas_payload.value("zones", nlohmann::json::array())) {
      const std::string id = zone.value("id", "");
      if (!id.empty()) {
        allowed.insert(id);
      }
    }
    for (const auto& id : atlas_zone_ids) {
      if (!id.empty()) {
        allowed.insert(id);
      }
    }
    const int room = std::max(0, 4 - static_cast<int>(opened.size()));
    const int max_add = std::min(2, room);
    if (j.contains("add") && j["add"].is_array() && max_add > 0) {
      for (const auto& item : j["add"]) {
        std::string id;
        if (item.is_string()) {
          id = item.get<std::string>();
        } else if (item.is_object()) {
          id = item.value("id", "");
        }
        if (id.empty() || !allowed.count(id) || opened.count(id)) {
          continue;
        }
        if (std::find(out.add.begin(), out.add.end(), id) != out.add.end()) {
          continue;
        }
        out.add.push_back(id);
        if (static_cast<int>(out.add.size()) >= max_add) {
          break;
        }
      }
    }
    out.why = hyp_json_string(j, "why");
    if (out.why.size() < 8) {
      out.why = "ampliar zona cuyo owns cubre el objeto";
    }
    if (out.why.size() > 240) {
      out.why.resize(240);
    }
    if (out.add.empty()) {
      out.error = "need_more sin ids restantes";
      return out;
    }
    out.ok = true;
    return out;
  }
  if (action != "causal_pilot_plan_v1") {
    out.error = "contrato causal_pilot_plan_v1 inválido";
    return out;
  }
  std::unordered_map<std::string, nlohmann::json> zones_by_id;
  std::unordered_set<std::string> allowed;
  std::unordered_map<std::string, std::string> stem_first_id;
  for (const auto& zone : atlas_payload.value("zones", nlohmann::json::array())) {
    const std::string id = zone.value("id", "");
    if (id.empty()) {
      continue;
    }
    allowed.insert(id);
    zones_by_id[id] = zone;
    const std::string st = causal_lower(pilot_zone_primary_stem(zone));
    if (!st.empty() && stem_first_id.find(st) == stem_first_id.end()) {
      stem_first_id[st] = id;
    }
  }
  for (const auto& id : atlas_zone_ids) {
    if (!id.empty()) {
      allowed.insert(id);
    }
  }
  std::unordered_set<std::string> opened(opened_ids.begin(), opened_ids.end());
  const bool restrict_open = !opened.empty();
  static const std::unordered_set<std::string> kKinds = {"cubre", "como", "gap"};
  nlohmann::json items = nlohmann::json::array();
  if (j.contains("tasks") && j["tasks"].is_array()) {
    items = j["tasks"];
  }
  std::unordered_set<std::string> used_stem_kind;
  std::unordered_set<std::string> used_questions;
  int remaining_cubre = 0;
  for (const auto& item : items) {
    if (!item.is_object() || static_cast<int>(out.tasks.size()) >= 4) {
      continue;
    }
    RegistryCausalPilotTask t;
    t.kind = causal_lower(hyp_json_string(item, "kind"));
    t.zone = item.value("zone", item.value("id", std::string{}));
    t.stem = hyp_json_string(item, "stem");
    t.question = hyp_json_string(item, "question");
    if (t.question.empty()) {
      t.question = hyp_json_string(item, "ask");
    }
    if (kKinds.count(t.kind) == 0 || t.zone.empty() || !allowed.count(t.zone)) {
      continue;
    }
    auto zit = zones_by_id.find(t.zone);
    if (zit != zones_by_id.end()) {
      if (t.stem.empty() || !pilot_zone_has_stem(zit->second, t.stem)) {
        t.stem = pilot_zone_primary_stem(zit->second);
      }
    }
    if (t.stem.empty()) {
      continue;
    }
    if (t.question.size() < 12 || t.question.size() > 160 ||
        pilot_question_is_template(t.question)) {
      t.question = pilot_specialize_question(t.kind, t.stem);
    } else {
      t.question = pilot_normalize_question(t.kind, t.stem, t.question);
    }
    std::string qkey = causal_lower(t.question);
    if (!used_questions.insert(qkey).second) {
      t.question = pilot_specialize_question(t.kind, t.stem);
      qkey = causal_lower(t.question);
      if (!used_questions.insert(qkey).second) {
        continue;
      }
    }
    const bool is_open = !restrict_open || opened.count(t.zone);
    if (!is_open) {
      if (t.kind != "cubre" || remaining_cubre >= 1) {
        continue;
      }
      if (!query.empty()) {
        auto zit_ov = zones_by_id.find(t.zone);
        if (zit_ov == zones_by_id.end() ||
            registry_causal_query_zone_overlap(query, zit_ov->second) <= 0) {
          continue;
        }
      }
      ++remaining_cubre;
    }
    const std::string stl = causal_lower(t.stem);
    auto first = stem_first_id.find(stl);
    if (first != stem_first_id.end() && first->second != t.zone && t.kind == "cubre") {
      continue;
    }
    const std::string sk = stl + "|" + t.kind;
    if (!used_stem_kind.insert(sk).second) {
      continue;
    }
    out.tasks.push_back(std::move(t));
  }
  {
    int best_rank = 99;
    std::string best_zone;
    std::string best_stem;
    for (const auto& t : out.tasks) {
      if (t.kind != "cubre") {
        continue;
      }
      auto zit = zones_by_id.find(t.zone);
      const int rank =
          zit == zones_by_id.end() ? 5 : pilot_kind_rank(registry_causal_zone_kind(zit->second));
      if (rank < best_rank) {
        best_rank = rank;
        best_zone = t.zone;
        best_stem = t.stem;
      }
    }
    std::unordered_set<std::string> como_stems;
    for (auto& t : out.tasks) {
      if (t.kind != "como") {
        continue;
      }
      auto zit = zones_by_id.find(t.zone);
      const int rank =
          zit == zones_by_id.end() ? 5 : pilot_kind_rank(registry_causal_zone_kind(zit->second));
      if (rank >= 4 && best_rank <= 1 && !best_zone.empty() &&
          causal_lower(t.stem) != causal_lower(best_stem)) {
        t.zone = best_zone;
        t.stem = best_stem;
        t.question = pilot_specialize_question("como", best_stem);
      }
      como_stems.insert(causal_lower(t.stem));
    }
    (void)como_stems;
  }
  out.why = hyp_json_string(j, "why");
  if (out.why.size() < 8 && !out.tasks.empty()) {
    out.why = "plan de encargos sobre fichas inspect";
  }
  if (out.why.size() > 240) {
    out.why.resize(240);
  }
  std::unordered_set<std::string> stems;
  for (const auto& t : out.tasks) {
    stems.insert(causal_lower(t.stem));
    if (t.kind == "cubre") {
      ++out.n_cubre;
    } else if (t.kind == "como") {
      out.has_como = true;
    } else if (t.kind == "gap") {
      out.has_gap = true;
    }
  }
  out.unique_stems = static_cast<int>(stems.size());
  if (out.tasks.size() < 2 || out.why.size() < 8) {
    out.error = "plan de piloto no accionable";
    return out;
  }
  out.ok = true;
  return out;
}

nlohmann::json registry_causal_pilot_plan_to_json(const RegistryCausalPilotPlan& plan) {
  nlohmann::json tasks = nlohmann::json::array();
  for (const auto& t : plan.tasks) {
    tasks.push_back(
        {{"kind", t.kind}, {"zone", t.zone}, {"stem", t.stem}, {"question", t.question}});
  }
  return {{"action", plan.action.empty() ? "causal_pilot_plan_v1" : plan.action},
          {"ok", plan.ok},
          {"need_more", plan.need_more},
          {"add", plan.add},
          {"tasks", tasks},
          {"unique_stems", plan.unique_stems},
          {"n_cubre", plan.n_cubre},
          {"has_como", plan.has_como},
          {"has_gap", plan.has_gap},
          {"why", plan.why},
          {"error", plan.error}};
}

std::string registry_causal_pilot_plan_markdown(const RegistryCausalPilotPlan& plan) {
  std::ostringstream out;
  out << "# " << (plan.action.empty() ? "causal_pilot_plan_v1" : plan.action) << "\n";
  out << "ok=" << (plan.ok ? "yes" : "no") << " need_more=" << (plan.need_more ? "yes" : "no")
      << " n=" << plan.tasks.size() << " stems=" << plan.unique_stems << " cubre=" << plan.n_cubre
      << " como=" << (plan.has_como ? "yes" : "no") << " gap=" << (plan.has_gap ? "yes" : "no")
      << "\n";
  if (!plan.why.empty()) {
    out << "why: " << plan.why << "\n";
  }
  if (!plan.error.empty()) {
    out << "error: " << plan.error << "\n";
  }
  if (!plan.add.empty()) {
    out << "add:";
    for (const auto& id : plan.add) {
      out << " " << id;
    }
    out << "\n";
  }
  int i = 0;
  for (const auto& t : plan.tasks) {
    out << "\n" << ++i << ". [" << t.kind << "] " << t.zone << "  stem=" << t.stem << "\n";
    out << "   " << t.question << "\n";
  }
  return out.str();
}

nlohmann::json registry_causal_payload_filter_zones(const nlohmann::json& payload,
                                                    const std::vector<std::string>& ids) {
  nlohmann::json out = payload;
  std::unordered_set<std::string> want(ids.begin(), ids.end());
  nlohmann::json zones = nlohmann::json::array();
  for (const auto& zone : payload.value("zones", nlohmann::json::array())) {
    if (want.count(zone.value("id", ""))) {
      zones.push_back(zone);
    }
  }
  out["zones"] = zones;
  return out;
}

bool registry_causal_pilot_target_in_stem(const std::string& target, const std::string& stem) {
  if (target.empty() || stem.empty()) {
    return false;
  }
  std::string path = target;
  const auto slash = path.find_last_of("/\\");
  const auto colon = path.rfind(':');
  if (colon != std::string::npos && slash != std::string::npos && colon > slash) {
    path = path.substr(0, colon);
  }
  return causal_lower(registry_stem_of(path)) == causal_lower(stem);
}

namespace {

std::string pilot_target_path(const std::string& target) {
  std::string path = target;
  const auto slash = path.find_last_of("/\\");
  const auto colon = path.rfind(':');
  if (colon != std::string::npos && (slash == std::string::npos || colon > slash)) {
    path = path.substr(0, colon);
  }
  return path;
}

std::string pilot_strip_window(const std::string& s) {
  const auto hash = s.rfind('#');
  if (hash == std::string::npos || hash + 1 >= s.size()) {
    return s;
  }
  const std::string w = causal_lower(s.substr(hash + 1));
  if (w == "head" || w == "mid" || w == "tail" || w == "hit") {
    return s.substr(0, hash);
  }
  return s;
}

bool pilot_targets_match(const std::string& a, const std::string& b) {
  const std::string la = causal_lower(pilot_strip_window(a));
  const std::string lb = causal_lower(pilot_strip_window(b));
  if (la.empty() || lb.empty()) {
    return false;
  }
  if (la == lb) {
    return true;
  }
  auto symbol_of = [](const std::string& s) {
    const auto col = s.rfind(':');
    if (col == std::string::npos || col + 1 >= s.size()) {
      return s;
    }
    const auto slash = s.find_last_of("/\\");
    if (slash != std::string::npos && col < slash) {
      return s;
    }
    return s.substr(col + 1);
  };
  const std::string sa = symbol_of(la);
  const std::string sb = symbol_of(lb);
  if (sa.size() >= 4 && sa == sb) {
    return true;
  }
  return false;
}

void pilot_notebook_push(RegistryCausalPilotWorkerNotebook* nb, const std::string& target) {
  if (nb == nullptr || target.empty()) {
    return;
  }
  for (const auto& have : nb->allowed_targets) {
    if (pilot_targets_match(have, target)) {
      return;
    }
  }
  nb->allowed_targets.push_back(target);
  const std::string path = pilot_target_path(target);
  if (!path.empty() && path.find('/') != std::string::npos) {
    if (std::find(nb->allowed_paths.begin(), nb->allowed_paths.end(), path) ==
        nb->allowed_paths.end()) {
      nb->allowed_paths.push_back(path);
    }
  }
}

void pilot_collect_json_target(RegistryCausalPilotWorkerNotebook* nb, const nlohmann::json& j) {
  if (nb == nullptr) {
    return;
  }
  if (j.is_string()) {
    pilot_notebook_push(nb, j.get<std::string>());
    return;
  }
  if (!j.is_object()) {
    return;
  }
  for (const char* key : {"target", "from", "to", "path_symbol", "symbol", "member"}) {
    const std::string s = j.value(key, "");
    if (!s.empty()) {
      pilot_notebook_push(nb, s);
    }
  }
}

bool pilot_text_is_brief(const std::string& s) {
  return s.size() >= 24 && s.find(' ') != std::string::npos;
}

bool pilot_why_cites_targets(const std::string& why,
                             const std::vector<std::string>& allowed) {
  const std::string hay = causal_lower(why);
  if (hay.size() < 4) {
    return false;
  }
  for (const auto& t : allowed) {
    std::string needle = causal_lower(t);
    const auto col = needle.rfind("::");
    if (col != std::string::npos) {
      needle = needle.substr(col + 2);
    }
    const auto c2 = needle.rfind(':');
    if (c2 != std::string::npos) {
      needle = needle.substr(c2 + 1);
    }
    if (needle.size() >= 4 && hay.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool pilot_notes_have_tool_result(const std::string& notes) {
  return notes.find("----- follow ") != std::string::npos ||
         notes.find("----- need_code ") != std::string::npos ||
         notes.find("----- outline ") != std::string::npos ||
         notes.find("----- dataflow ") != std::string::npos ||
         notes.find("----- causal ") != std::string::npos;
}

bool pilot_notes_have_body(const std::string& notes) {
  return notes.find("----- need_code ") != std::string::npos;
}

bool pilot_notes_have_flow(const std::string& notes) {
  return notes.find("----- dataflow ") != std::string::npos ||
         notes.find("----- causal ") != std::string::npos;
}

bool pilot_target_is_windowed(const std::string& target) {
  if (target.find('#') != std::string::npos) {
    return true;
  }
  const auto col = target.rfind(':');
  if (col == std::string::npos || col + 1 >= target.size()) {
    return false;
  }
  return std::isdigit(static_cast<unsigned char>(target[col + 1])) != 0;
}

bool pilot_notes_have_tool_target(const std::string& notes, const std::string& tool,
                                  const std::string& target) {
  if (notes.empty() || tool.empty() || target.empty()) {
    return false;
  }
  const std::string prefix = "----- " + tool + " ";
  std::size_t pos = 0;
  while (true) {
    const auto hit = notes.find(prefix, pos);
    if (hit == std::string::npos) {
      return false;
    }
    std::size_t i = hit + prefix.size();
    while (i < notes.size() && (notes[i] == ' ' || notes[i] == '\t')) {
      ++i;
    }
    auto end = notes.find(" -----", i);
    if (end == std::string::npos) {
      end = notes.find('\n', i);
    }
    std::string have = notes.substr(i, end == std::string::npos ? std::string::npos : end - i);
    while (!have.empty() && std::isspace(static_cast<unsigned char>(have.back()))) {
      have.pop_back();
    }
    const auto sp = have.find(' ');
    if (sp != std::string::npos) {
      have = have.substr(0, sp);
    }
    if (pilot_targets_match(have, target)) {
      return true;
    }
    pos = hit + 1;
  }
}

std::string pilot_missing_clear_field(const std::string& notes) {
  const std::string needle = "no hay clear de ";
  const auto hit = notes.find(needle);
  if (hit == std::string::npos) {
    return {};
  }
  std::size_t i = hit + needle.size();
  std::string name;
  while (i < notes.size() &&
         (std::isalnum(static_cast<unsigned char>(notes[i])) != 0 || notes[i] == '_')) {
    name.push_back(notes[i]);
    ++i;
  }
  return name;
}

bool pilot_notes_are_closing(const std::string& notes) {
  return notes.find("----- cierre -----") != std::string::npos;
}

std::string pilot_trim_copy(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  return s;
}

std::string pilot_need_code_corpus(const std::string& notes) {
  std::string out;
  const std::string prefix = "----- need_code ";
  std::size_t pos = 0;
  while (true) {
    const auto hit = notes.find(prefix, pos);
    if (hit == std::string::npos) {
      break;
    }
    const auto next = notes.find("\n----- ", hit + 1);
    const auto n = next == std::string::npos ? notes.size() - hit : next - hit;
    out.append(notes, hit, n);
    out.push_back('\n');
    pos = hit + 1;
  }
  return out;
}

bool pilot_step_is_duda(const std::string& step) {
  const std::string s = causal_lower(pilot_trim_copy(step));
  return s.find("duda:") == 0 || s.find("duda ") == 0;
}

bool pilot_step_is_port(const std::string& step) {
  const std::string s = causal_lower(pilot_trim_copy(step));
  return s.find("port_to:") == 0 || s.find("port_to ") == 0;
}

std::vector<std::string> pilot_split_walk_steps(const std::string& walk) {
  std::vector<std::string> steps;
  std::string cur;
  auto flush = [&]() {
    const std::string t = pilot_trim_copy(cur);
    if (!t.empty()) {
      steps.push_back(t);
    }
    cur.clear();
  };
  for (std::size_t i = 0; i < walk.size(); ++i) {
    if (walk[i] == ';') {
      flush();
      continue;
    }
    if (walk[i] == '-' && i + 1 < walk.size() && walk[i + 1] == '>') {
      flush();
      ++i;
      continue;
    }
    if (static_cast<unsigned char>(walk[i]) == 0xE2 && i + 2 < walk.size() &&
        static_cast<unsigned char>(walk[i + 1]) == 0x86 &&
        static_cast<unsigned char>(walk[i + 2]) == 0x92) {
      flush();
      i += 2;
      continue;
    }
    cur.push_back(walk[i]);
  }
  flush();
  return steps;
}

std::string pilot_walk_claimed_hay(const std::string& walk) {
  std::string hay;
  for (const auto& step : pilot_split_walk_steps(walk)) {
    if (pilot_step_is_duda(step) || pilot_step_is_port(step)) {
      continue;
    }
    hay += ' ';
    hay += causal_lower(step);
  }
  return hay;
}

bool pilot_ident_bounded_in(const std::string& hay, const std::string& needle) {
  if (needle.size() < 4 || hay.size() < needle.size()) {
    return false;
  }
  auto is_id = [](unsigned char c) {
    return std::isalnum(c) != 0 || c == '_';
  };
  std::size_t pos = 0;
  while (true) {
    const auto hit = hay.find(needle, pos);
    if (hit == std::string::npos) {
      return false;
    }
    const bool before_ok = hit == 0 || !is_id(static_cast<unsigned char>(hay[hit - 1]));
    const auto end = hit + needle.size();
    const bool after_ok = end >= hay.size() || !is_id(static_cast<unsigned char>(hay[end]));
    if (before_ok && after_ok) {
      return true;
    }
    pos = hit + 1;
  }
}

std::string pilot_walk_step_symbol(const std::string& step) {
  std::string s = pilot_trim_copy(step);
  const auto acto = s.find(": ");
  std::string head = acto == std::string::npos ? s : s.substr(0, acto);
  if (acto == std::string::npos) {
    const auto sp = head.find(' ');
    if (sp != std::string::npos) {
      head = head.substr(0, sp);
    }
  }
  const auto ns = head.rfind("::");
  if (ns != std::string::npos) {
    head = head.substr(ns + 2);
  } else {
    const auto slash = head.find_last_of("/\\");
    const auto col = head.rfind(':');
    if (col != std::string::npos && (slash == std::string::npos || col > slash)) {
      head = head.substr(col + 1);
    }
  }
  while (!head.empty() && std::isalnum(static_cast<unsigned char>(head.back())) == 0 &&
         head.back() != '_') {
    head.pop_back();
  }
  return head;
}

bool pilot_walk_has_duda_of(const std::string& walk, const std::string& target) {
  std::string needle = causal_lower(target);
  const auto slash = needle.find_last_of("/\\");
  const auto col = needle.rfind(':');
  if (col != std::string::npos && (slash == std::string::npos || col > slash)) {
    needle = needle.substr(col + 1);
  }
  if (needle.size() < 4) {
    return false;
  }
  return causal_lower(walk).find("duda:" + needle) != std::string::npos;
}

bool rest_has_write_token(const std::string& rest) {
  std::size_t i = 0;
  while (i < rest.size()) {
    while (i < rest.size() && std::isalnum(static_cast<unsigned char>(rest[i])) == 0) {
      ++i;
    }
    std::size_t j = i;
    while (j < rest.size() && std::isalnum(static_cast<unsigned char>(rest[j])) != 0) {
      ++j;
    }
    if (j - i == 5 && rest.compare(i, 5, "write") == 0) {
      return true;
    }
    i = j;
  }
  return false;
}

std::string pilot_unread_dataflow_writer(const std::string& notes, const std::string& stem) {
  const std::string marker = "fn=`";
  std::vector<std::string> writers;
  std::size_t pos = 0;
  while (true) {
    const auto hit = notes.find(marker, pos);
    if (hit == std::string::npos) {
      break;
    }
    const auto end = notes.find('`', hit + marker.size());
    if (end == std::string::npos) {
      break;
    }
    const std::string target = notes.substr(hit + marker.size(), end - (hit + marker.size()));
    const auto nl = notes.find('\n', end);
    const std::string rest =
        notes.substr(end + 1, (nl == std::string::npos ? notes.size() : nl) - (end + 1));
    pos = end + 1;
    if (target.size() < 4 || !rest_has_write_token(rest)) {
      continue;
    }
    if (!stem.empty() && !registry_causal_pilot_target_in_stem(target, stem)) {
      continue;
    }
    bool seen = false;
    for (const auto& w : writers) {
      if (pilot_targets_match(w, target)) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      writers.push_back(target);
    }
  }
  int n_fenced = 0;
  std::string first_unfenced;
  for (const auto& t : writers) {
    if (pilot_notes_have_tool_target(notes, "need_code", t)) {
      ++n_fenced;
    } else if (first_unfenced.empty()) {
      first_unfenced = t;
    }
  }
  if (first_unfenced.empty() || n_fenced >= 2) {
    return {};
  }
  return first_unfenced;
}

std::string pilot_walk_first_unread(const std::string& walk,
                                    const RegistryCausalPilotWorkerNotebook& nb) {
  for (const auto& step : pilot_split_walk_steps(walk)) {
    if (pilot_step_is_duda(step) || pilot_step_is_port(step)) {
      continue;
    }
    const std::string ident = pilot_walk_step_symbol(step);
    if (ident.size() < 4) {
      continue;
    }
    bool in_card = false;
    std::string full = ident;
    for (const auto& have : nb.allowed_targets) {
      if (pilot_targets_match(ident, have)) {
        in_card = true;
        full = have;
        break;
      }
    }
    const bool looks_sym = ident.find('_') != std::string::npos ||
                           std::any_of(ident.begin(), ident.end(), [](char c) {
                             return std::isupper(static_cast<unsigned char>(c)) != 0;
                           });
    if (!in_card && !looks_sym) {
      continue;
    }
    if (!pilot_notes_have_tool_target(nb.notes, "need_code", ident) &&
        !pilot_notes_have_tool_target(nb.notes, "need_code", full)) {
      return full;
    }
  }
  return {};
}

bool pilot_notes_need_tool_now(const std::string& notes) {
  if (!pilot_notes_have_body(notes)) {
    return true;
  }
  const std::string unread_key = "walk nombra un símbolo no leído: ";
  const std::string writer_key = "falta need_code del writer: ";
  const auto unread_hit = notes.rfind(unread_key);
  const auto writer_hit = notes.rfind(writer_key);
  std::string key;
  std::size_t hit = std::string::npos;
  if (unread_hit != std::string::npos && (writer_hit == std::string::npos || unread_hit > writer_hit)) {
    key = unread_key;
    hit = unread_hit;
  } else if (writer_hit != std::string::npos) {
    key = writer_key;
    hit = writer_hit;
  }
  if (hit == std::string::npos) {
    return false;
  }
  std::size_t i = hit + key.size();
  std::size_t j = i;
  while (j < notes.size() && !std::isspace(static_cast<unsigned char>(notes[j])) &&
         notes[j] != '"' ) {
    ++j;
  }
  std::string tgt = notes.substr(i, j - i);
  while (!tgt.empty() && (tgt.back() == '.' || tgt.back() == ',')) {
    tgt.pop_back();
  }
  if (tgt.size() < 4) {
    return true;
  }
  return !pilot_notes_have_tool_target(notes, "need_code", tgt);
}

bool pilot_ident_in_notebook(const std::string& ident,
                             const RegistryCausalPilotWorkerNotebook& nb) {
  const std::string id = causal_lower(ident);
  if (id.size() < 3) {
    return false;
  }
  for (const auto& have : nb.allowed_targets) {
    if (pilot_targets_match(ident, have)) {
      return true;
    }
    const std::string h = causal_lower(have);
    if (h.size() >= id.size() && h.find(id) != std::string::npos) {
      return true;
    }
  }
  const std::string hay = causal_lower(nb.notes);
  return hay.find(id) != std::string::npos;
}

int pilot_notes_unique_code_targets(const std::string& notes) {
  std::unordered_set<std::string> keys;
  auto take = [&](const char* hdr) {
    const std::string prefix = hdr;
    std::size_t pos = 0;
    while (true) {
      const auto hit = notes.find(prefix, pos);
      if (hit == std::string::npos) {
        return;
      }
      std::size_t i = hit + prefix.size();
      while (i < notes.size() && (notes[i] == ' ' || notes[i] == '\t')) {
        ++i;
      }
      const auto end = notes.find_first_of(" \t\n", i);
      const std::string target =
          notes.substr(i, end == std::string::npos ? std::string::npos : end - i);
      std::string key = causal_lower(target);
      const auto col = key.rfind(':');
      if (col != std::string::npos && col + 1 < key.size() && key.find('/') < col) {
        key = key.substr(col + 1);
      }
      if (key.size() >= 4) {
        keys.insert(key);
      }
      pos = hit + 1;
    }
  };
  take("----- follow ");
  take("----- need_code ");
  return static_cast<int>(keys.size());
}

}  // namespace

void registry_causal_pilot_notebook_add_target(RegistryCausalPilotWorkerNotebook* nb,
                                               const std::string& target) {
  pilot_notebook_push(nb, target);
}

bool registry_causal_pilot_target_in_notebook(const std::string& target,
                                              const RegistryCausalPilotWorkerNotebook& nb) {
  if (target.empty()) {
    return false;
  }
  for (const auto& have : nb.allowed_targets) {
    if (pilot_targets_match(target, have)) {
      return true;
    }
  }
  return false;
}

void registry_causal_pilot_notebook_from_payload(const nlohmann::json& payload,
                                                 const std::string& stem,
                                                 RegistryCausalPilotWorkerNotebook* nb) {
  if (nb == nullptr) {
    return;
  }
  auto walk_zone = [&](const nlohmann::json& zone) {
    for (const auto& r : zone.value("representatives", nlohmann::json::array())) {
      pilot_collect_json_target(nb, r);
    }
    for (const auto& e : zone.value("edges", nlohmann::json::array())) {
      pilot_collect_json_target(nb, e);
    }
    for (const auto& e : zone.value("ports", nlohmann::json::array())) {
      pilot_collect_json_target(nb, e);
    }
    const auto mech = zone.value("mechanism", nlohmann::json::object());
    for (const char* slot : {"trigger", "state", "effect"}) {
      if (mech.contains(slot)) {
        pilot_collect_json_target(nb, mech[slot]);
      }
    }
    for (const auto& group : zone.value("anchors", nlohmann::json::array())) {
      if (group.is_array()) {
        for (const auto& a : group) {
          pilot_collect_json_target(nb, a);
        }
      } else {
        pilot_collect_json_target(nb, group);
      }
    }
    for (const auto& trail : zone.value("trails", nlohmann::json::array())) {
      for (const auto& step : trail.value("path", nlohmann::json::array())) {
        pilot_collect_json_target(nb, step);
      }
    }
    for (const auto& role_key : {"writers", "readers", "controls"}) {
      const auto roles = zone.value("roles", nlohmann::json::object());
      if (roles.contains(role_key)) {
        for (const auto& item : roles[role_key]) {
          if (item.is_string()) {
            pilot_notebook_push(nb, item.get<std::string>());
          }
        }
      }
    }
  };
  auto zone_has_stem = [&](const nlohmann::json& zone) {
    if (stem.empty()) {
      return true;
    }
    const std::string want = causal_lower(stem);
    if (causal_lower(zone.value("id", "")) == want ||
        causal_lower(zone.value("owns", "")) == want) {
      return true;
    }
    for (const char* key : {"primary_stems", "core_stems", "context_stems"}) {
      for (const auto& s : zone.value(key, nlohmann::json::array())) {
        if (s.is_string() && causal_lower(s.get<std::string>()) == want) {
          return true;
        }
      }
    }
    return false;
  };
  if (payload.contains("zones") && payload["zones"].is_array()) {
    bool any = false;
    for (const auto& zone : payload["zones"]) {
      if (!zone_has_stem(zone)) {
        continue;
      }
      walk_zone(zone);
      any = true;
    }
    if (!any) {
      for (const auto& zone : payload["zones"]) {
        walk_zone(zone);
      }
    }
  } else {
    walk_zone(payload);
  }
}

std::string registry_causal_pilot_allowed_markdown(const RegistryCausalPilotWorkerNotebook& nb) {
  std::ostringstream out;
  out << "## Símbolos permitidos (ficha + tools)\n";
  int n = 0;
  for (const auto& t : nb.allowed_targets) {
    if (n >= 24) {
      out << "- …\n";
      break;
    }
    out << "- " << t << "\n";
    ++n;
  }
  if (n == 0) {
    out << "(ninguno: usa outline del archivo del stem)\n";
  } else {
    out << "El why/brief del informe debe incluir el nombre de uno de estos símbolos.\n";
  }
  return out.str();
}

std::string registry_causal_pilot_follow_markdown(const nlohmann::json& payload,
                                                  const std::string& target,
                                                  const std::string& direction,
                                                  const std::string& stem,
                                                  std::vector<std::string>* new_targets,
                                                  std::string* port_to) {
  const bool incoming = causal_lower(direction) == "incoming";
  std::ostringstream out;
  out << "follow " << (incoming ? "incoming" : "outgoing") << " " << target << "\n";
  int hits = 0;
  auto consider = [&](const std::string& from, const std::string& to, const std::string& kind) {
    const std::string focus = incoming ? to : from;
    const std::string other = incoming ? from : to;
    if (other.empty() || !pilot_targets_match(focus, target)) {
      return;
    }
    ++hits;
    out << "- " << from << " -" << kind << "-> " << to << "\n";
    if (new_targets != nullptr) {
      new_targets->push_back(other);
    }
    if (port_to != nullptr && !other.empty() && !stem.empty()) {
      const std::string ost = causal_lower(registry_stem_of(pilot_target_path(other).empty()
                                                               ? other
                                                               : pilot_target_path(other)));
      const std::string ost2 = causal_lower(registry_stem_of(other));
      const std::string want = causal_lower(stem);
      if (!ost.empty() && ost != want && ost.find('.') == std::string::npos) {
        *port_to = registry_stem_of(other);
      } else if (!ost2.empty() && ost2 != want && other.find("::") != std::string::npos) {
        const auto col = other.find("::");
        *port_to = other.substr(0, col);
      }
    }
  };
  auto walk_zone = [&](const nlohmann::json& zone) {
    for (const auto& e : zone.value("edges", nlohmann::json::array())) {
      consider(e.value("from", ""), e.value("to", ""), e.value("kind", "call"));
    }
    for (const auto& e : zone.value("ports", nlohmann::json::array())) {
      consider(e.value("from", ""), e.value("to", ""), e.value("kind", "port"));
    }
    const auto mech = zone.value("mechanism", nlohmann::json::object());
    for (const char* slot : {"trigger", "state", "effect"}) {
      if (!mech.contains(slot) || !mech[slot].is_object()) {
        continue;
      }
      consider(mech[slot].value("from", ""), mech[slot].value("to", ""), slot);
    }
  };
  if (payload.contains("zones") && payload["zones"].is_array()) {
    for (const auto& zone : payload["zones"]) {
      walk_zone(zone);
    }
  } else {
    walk_zone(payload);
  }
  if (hits == 0) {
    out << "(sin hops en la ficha para ese símbolo)\n";
  }
  return out.str();
}

std::string registry_causal_pilot_causal_mermaid(
    const std::vector<std::pair<std::string, std::string>>& edges) {
  auto node_id = [](std::string s) {
    for (char& c : s) {
      if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
        c = '_';
      }
    }
    if (s.empty()) {
      s = "n";
    }
    if (std::isdigit(static_cast<unsigned char>(s.front()))) {
      s.insert(s.begin(), 'n');
    }
    return s;
  };
  std::ostringstream out;
  out << "```mermaid\nflowchart TD\n";
  if (edges.empty()) {
    out << "  empty[sin aristas de codigo]\n";
  }
  for (const auto& e : edges) {
    out << "  " << node_id(e.first) << "[\"" << e.first << "\"] --> " << node_id(e.second)
        << "[\"" << e.second << "\"]\n";
  }
  out << "```\n";
  return out.str();
}

namespace {

bool aguas_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::string aguas_trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  return s;
}

std::string aguas_compact(std::string s, std::size_t max_n) {
  std::string out;
  bool sp = false;
  for (char c : s) {
    if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
      sp = true;
      continue;
    }
    if (sp && !out.empty()) {
      out.push_back(' ');
    }
    sp = false;
    out.push_back(c);
  }
  if (max_n > 0 && out.size() > max_n) {
    out.resize(max_n);
    out += "…";
  }
  return out;
}

std::string aguas_bare_symbol(std::string name) {
  const auto slash = name.find_last_of("/\\");
  if (slash != std::string::npos) {
    name = name.substr(slash + 1);
  }
  const auto colon = name.rfind(':');
  if (colon != std::string::npos) {
    name = name.substr(colon + 1);
  }
  return name;
}

bool aguas_skip_member(const std::string& name) {
  static const char* kSkip[] = {"this", "std", "string", "vector", "optional", "unique_ptr",
                                "shared_ptr", "npos", "mutex", "lock", "lock_guard"};
  for (const char* s : kSkip) {
    if (name == s) {
      return true;
    }
  }
  if (name.size() <= 1) {
    return true;
  }
  if (name.find("mutex") != std::string::npos) {
    return true;
  }
  return false;
}

std::string aguas_rhs_after_name(const std::string& line, const std::string& name) {
  const auto pos = line.find(name);
  if (pos == std::string::npos) {
    return {};
  }
  std::size_t i = pos + name.size();
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
    ++i;
  }
  if (i + 1 < line.size() &&
      ((line[i] == '+' && line[i + 1] == '+') || (line[i] == '-' && line[i + 1] == '-'))) {
    return line[i] == '+' ? "incrementa" : "decrementa";
  }
  if (i + 1 < line.size() &&
      (line[i] == '+' || line[i] == '-' || line[i] == '*' || line[i] == '/') &&
      line[i + 1] == '=') {
    return line[i] == '+' ? "incrementa" : (line[i] == '-' ? "decrementa" : line.substr(i, 2));
  }
  if (i < line.size() && line[i] == '.' ) {
    std::size_t j = i + 1;
    while (j < line.size() && aguas_ident_char(line[j])) {
      ++j;
    }
    const std::string method = line.substr(i + 1, j - (i + 1));
    if (method == "store" || method == "exchange") {
      std::size_t k = j;
      while (k < line.size() && (std::isspace(static_cast<unsigned char>(line[k])) ||
                                 line[k] == '(')) {
        ++k;
      }
      std::size_t e = k;
      while (e < line.size() && line[e] != ')' && line[e] != ',' && line[e] != ';') {
        ++e;
      }
      const std::string arg = aguas_trim(line.substr(k, e - k));
      return arg.empty() ? "escribe" : arg;
    }
  }
  if (i >= line.size() || line[i] != '=' || (i + 1 < line.size() && line[i + 1] == '=')) {
    return {};
  }
  ++i;
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
    ++i;
  }
  std::size_t e = i;
  while (e < line.size() && line[e] != ';') {
    ++e;
  }
  return aguas_compact(line.substr(i, e - i), 24);
}

void aguas_collect_members(const std::string& text, std::vector<std::string>* names) {
  if (names == nullptr) {
    return;
  }
  auto consider = [&](const std::string& name) {
    if (aguas_skip_member(name)) {
      return;
    }
    if (std::find(names->begin(), names->end(), name) == names->end()) {
      names->push_back(name);
    }
  };
  for (std::size_t i = 0; i < text.size(); ++i) {
    std::size_t j = std::string::npos;
    if (text[i] == '.') {
      j = i + 1;
    } else if (text[i] == '-' && i + 1 < text.size() && text[i + 1] == '>') {
      j = i + 2;
    }
    if (j == std::string::npos) {
      continue;
    }
    while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) {
      ++j;
    }
    if (j >= text.size() ||
        !(std::isalpha(static_cast<unsigned char>(text[j])) != 0 || text[j] == '_')) {
      continue;
    }
    std::size_t k = j;
    while (k < text.size() && aguas_ident_char(text[k])) {
      ++k;
    }
    std::size_t p = k;
    while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p]))) {
      ++p;
    }
    if (p < text.size() && text[p] == '(') {
      i = k - 1;
      continue;
    }
    consider(text.substr(j, k - j));
    i = k - 1;
  }
  // Trailing-underscore fields: agent_cancel_.store / foo_ =
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (!(std::isalpha(static_cast<unsigned char>(text[i])) != 0 || text[i] == '_')) {
      continue;
    }
    if (i > 0 && aguas_ident_char(text[i - 1])) {
      continue;
    }
    std::size_t k = i;
    while (k < text.size() && aguas_ident_char(text[k])) {
      ++k;
    }
    if (k == i || text[k - 1] != '_') {
      i = k - 1;
      continue;
    }
    std::size_t p = k;
    while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p]))) {
      ++p;
    }
    bool fieldish = false;
    if (p < text.size() && text[p] == '=') {
      fieldish = p + 1 >= text.size() || text[p + 1] != '=';
    } else if (p < text.size() && text[p] == '.') {
      std::size_t m = p + 1;
      while (m < text.size() && aguas_ident_char(text[m])) {
        ++m;
      }
      const std::string method = text.substr(p + 1, m - (p + 1));
      fieldish = method == "store" || method == "load" || method == "exchange" ||
                 method == "compare_exchange_weak" || method == "compare_exchange_strong" ||
                 method == "fetch_add" || method == "fetch_sub";
    }
    if (fieldish) {
      consider(text.substr(i, k - i));
    }
    i = k - 1;
  }
}

}  // namespace

std::string registry_causal_pilot_aguas_arriba_markdown(
    const std::string& workspace_root, const std::string& focus_path,
    const std::string& focus_symbol,
    const std::function<std::vector<ATrailSearchHit>(const std::string& symbol)>& search,
    std::vector<std::string>* incoming_targets) {
  const std::string target =
      focus_path.empty() ? focus_symbol : (focus_path + ":" + focus_symbol);
  std::ostringstream out;
  out << "----- aguas_arriba " << target << " -----\n";
  if (!search || focus_symbol.empty()) {
    out << "(sin caller en este stem)\n";
    return out.str();
  }
  const auto stacks =
      a_trail_build_full_stacks(workspace_root, focus_symbol, focus_path, search, 2, 2);
  int shown = 0;
  for (const auto& st : stacks) {
    if (st.hops.size() < 2) {
      continue;
    }
    const ATrailHop& caller = st.hops[st.hops.size() - 2];
    if (caller.symbol.empty() || caller.symbol == focus_symbol) {
      continue;
    }
    const std::string focus_stem = registry_stem_of(focus_path);
    const std::string caller_stem = registry_stem_of(caller.path);
    if (incoming_targets != nullptr) {
      for (std::size_t i = 0; i + 1 < st.hops.size(); ++i) {
        const auto& hop = st.hops[i];
        if (hop.symbol.empty() || hop.symbol == focus_symbol) {
          continue;
        }
        const std::string t =
            hop.path.empty() ? hop.symbol : (hop.path + ":" + hop.symbol);
        if (std::find(incoming_targets->begin(), incoming_targets->end(), t) ==
            incoming_targets->end()) {
          incoming_targets->push_back(t);
        }
      }
    }
    if (focus_stem.empty() || caller_stem != focus_stem) {
      continue;
    }
    std::string quien;
    for (const auto& hop : st.hops) {
      const std::string name = aguas_bare_symbol(hop.symbol.empty() ? hop.anchor : hop.symbol);
      if (name.empty()) {
        continue;
      }
      if (!quien.empty()) {
        quien += " → ";
      }
      quien += name;
    }
    std::string cuando = aguas_compact(caller.control_cond, 72);
    if (cuando.empty()) {
      cuando = aguas_compact(caller.control_chain, 72);
    }
    if (cuando.empty()) {
      cuando = "(sin cond en el call site)";
    }
    out << "cuando: " << cuando << "\n";
    out << "quien:  " << quien << "\n";
    ++shown;
  }
  if (shown == 0) {
    out << "(sin caller en este stem)\n";
  }
  return out.str();
}

std::string registry_causal_pilot_aguas_abajo_markdown(const std::string& display_target,
                                                       const std::string& body_text,
                                                       bool truncated) {
  std::ostringstream out;
  out << "----- aguas_abajo " << display_target << " -----\n";
  if (truncated) {
    out << "solo el recorte enviado\n";
  }
  const std::string stem = registry_stem_of(display_target);
  std::vector<std::string> members;
  aguas_collect_members(body_text, &members);
  members.erase(std::remove_if(members.begin(), members.end(),
                               [&](const std::string& n) { return !stem.empty() && n == stem; }),
                members.end());
  if (members.empty()) {
    out << "(sin miembros en este recorte)\n";
    return out.str();
  }
  struct Attr {
    bool wrote = false;
    bool read = false;
    std::string polarity;
    bool wrote_true = false;
    bool wrote_false = false;
  };
  std::vector<std::pair<std::string, Attr>> attrs;
  std::string line;
  auto flush_line = [&]() {
    if (line.empty()) {
      return;
    }
    for (auto& pair : attrs) {
      const auto kind = a_dataflow_classify_line(line, pair.first);
      if (kind == ADataFlowKind::Write) {
        pair.second.wrote = true;
        const std::string rhs = aguas_rhs_after_name(line, pair.first);
        if (!rhs.empty() && pair.second.polarity.empty()) {
          pair.second.polarity = rhs;
        }
        const std::string low = causal_lower(rhs);
        if (low == "true" || low == "1") {
          pair.second.wrote_true = true;
        }
        if (low == "false" || low == "0" || low == "nullptr" || low == "null") {
          pair.second.wrote_false = true;
        }
      } else if (kind == ADataFlowKind::Read) {
        pair.second.read = true;
      }
    }
    line.clear();
  };
  for (const auto& m : members) {
    attrs.push_back({m, Attr{}});
  }
  for (char c : body_text) {
    if (c == '\n') {
      flush_line();
    } else if (c != '\r') {
      line.push_back(c);
    }
  }
  flush_line();
  int n_attr = 0;
  for (const auto& pair : attrs) {
    if (!pair.second.wrote && !pair.second.read) {
      continue;
    }
    if (n_attr >= 8) {
      break;
    }
    out << (stem.empty() ? std::string{} : stem + " ") << pair.first << ":  ";
    if (pair.second.wrote) {
      out << "escribe";
      if (!pair.second.polarity.empty()) {
        out << " " << pair.second.polarity;
      }
      if (pair.second.read) {
        out << "; lee";
      }
    } else {
      out << "lee";
    }
    out << "\n";
    ++n_attr;
  }
  if (n_attr == 0) {
    out << "(sin miembros en este recorte)\n";
  }
  auto looks_latch_field = [](const std::string& n) {
    auto ends = [&](const char* suf, std::size_t n_suf) {
      return n.size() >= n_suf && n.compare(n.size() - n_suf, n_suf, suf) == 0;
    };
    return n == "busy" || n == "active" || n == "running" || n == "enabled" || n == "halted" ||
           n == "pending" || ends("_busy", 5) || ends("_active", 7) || ends("_halted", 7) ||
           ends("_pending", 8) || n.find("pending_") == 0;
  };
  bool emitted_next = false;
  for (const auto& pair : attrs) {
    const bool missing_clear = looks_latch_field(pair.first) && !pair.second.wrote_false &&
                               (pair.second.wrote || pair.second.read);
    if (missing_clear) {
      out << "no hay clear de " << pair.first << " en este cuerpo\n";
      if (!emitted_next) {
        out << "siguiente: {\"action\":\"causal_pilot_dataflow\",\"target\":\"" << pair.first
            << "\"}\n";
        out << "una llamada en este recorte no es el clear\n";
        emitted_next = true;
      }
    }
  }
  return out.str();
}

std::string registry_causal_pilot_worker_system_prompt(const std::string& kind,
                                                       const std::string& stem) {
  std::ostringstream out;
  out << "Eres un TRABAJADOR SORDO. No ves otros barrios ni al piloto. Solo el stem `" << stem
      << "`.\n";
  out << "Tu trabajo: enumerar los pasos por los que pasa la CONSULTA DEL USUARIO en este "
         "stem, SIN DUDAS. El veredicto sale de esa enumeración, no al revés.\n";
  out << "El owns de la ficha NO es la respuesta. Las aristas de la ficha NO son evidencia. "
         "Un follow de hops no es un paso.\n";
  out << "Un nombre que el flujo o la ficha apuntó y cuyo cuerpo no has abierto es una DUDA: "
         "pide need_code, o márcala duda:Symbol en el walk. No la listes como paso comprendido.\n";
  out << "Si la consulta pregunta quién escribe o limpia un campo y en el cuerpo leído no ves "
         "el clear, pide dataflow de ese campo (nombra a los writers) y need_code de cada uno. "
         "Un hop de la ficha no es el walk.\n";
  out << "Tras need_code hay ----- aguas_arriba ----- (quién te llama y bajo qué cond) y "
         "----- aguas_abajo ----- (qué campos escribe o lee ESTE recorte). aguas_arriba no es "
         "el walk de quién escribe o limpia. Un nombre solo ahí no está leído. El walk de un "
         "campo sale de aguas_abajo y de los writers del dataflow (need_code de cada fn=). "
         "Una llamada en el recorte no es el clear. Un paso comprendido exige "
         "----- need_code ----- de ESE símbolo; ver el nombre en otro cuerpo no cuenta. "
         "Tras dataflow, need_code de cada fn= write de este stem. Si aguas_abajo pide "
         "dataflow, el siguiente JSON es esa tool.\n";
  out << "Catálogo — si aún no hay ----- need_code ----- en notas, responde SOLO una tool:\n";
  out << "{\"action\":\"causal_pilot_need_code\",\"target\":\"path.cpp:Symbol\"}\n";
  out << "{\"action\":\"causal_pilot_dataflow\",\"target\":\"member_or_var\"}\n";
  out << "{\"action\":\"causal_pilot_causal\",\"target\":\"path.cpp:Symbol\"}\n";
  out << "{\"action\":\"causal_pilot_outline\",\"target\":\"path.cpp\"}\n";
  out << "{\"action\":\"causal_pilot_follow\",\"target\":\"path.cpp:Symbol\","
         "\"direction\":\"outgoing\"}\n";
  out << "PROHIBIDO inventar símbolos. PROHIBIDO why/walk de plantilla.\n";
  out << "walk: enumera lo acumulado en ## Notas, no una plantilla de dos hops. "
         "Cada variable del acto: quién la escribe, observa o limpia en este stem; "
         "ramas con -> o ;. Duda LOCAL: duda:Symbol y pide tool. Lo que entra de "
         "otro barrio: duda:Symbol y port_to en el JSON; no pidas su need_code. "
         "Nombres reales de ## Notas, nunca el placeholder sym. Si te queda una duda "
         "LOCAL, pide tool; no_cubre no es rendirse. brief: 2 frases para el PILOTO "
         "derivadas del árbol (variables, cómo se relacionan, si algo entra de fuera).\n\n";
  if (kind == "cubre") {
    out << "cubre: los pasos del walk son o no el objeto de la consulta. No follow. Tokens "
           "temáticos (asistente, chat, panel, IA) no bastan. El encargo nombra este stem a "
           "propósito: eso NO es cubre.\n";
    out << "Informe: {\"action\":\"causal_pilot_worker_v1\",\"kind\":\"cubre\","
           "\"walk\":\"NombreFn: qué hace\",\"verdict\":\"cubre|no_cubre\",\"owns\":\""
        << stem << "\",\"why\":\"…símbolo…\",\"brief\":\"2 frases para el piloto\"}\n";
  } else if (kind == "como") {
    out << "como: comprende el acto EN este stem. Sin saber la forma de antemano: tira del "
           "hilo (cuerpo, aguas, dataflow, writers de ESTE stem) y rellena huecos. El walk "
           "es el árbol de lo que leíste: cada variable del acto, quién la escribe o limpia "
           "aquí, ramas, y duda+port_to si un hilo entra de fuera. No copies hops. Una "
           "llamada keep-alive no es el clear. No emitas chain con una duda LOCAL pendiente. "
           "Dos tools no bastan si aguas o dataflow aún nombran un hueco de este barrio.\n";
    out << "Informe: {\"action\":\"causal_pilot_worker_v1\",\"kind\":\"como\","
           "\"walk\":\"…árbol de este stem…\","
           "\"verdict\":\"chain|no_cubre\","
           "\"path_symbol\":\"…\",\"chain\":\"…\",\"port_to\":\"\",\"why\":\"…símbolo…\","
           "\"brief\":\"2 frases para el piloto\"}\n";
    out << "verdict de informe nunca es need_code. Una ----- nota ----- de árbitro no es "
           "lectura.\n";
  } else {
    out << "gap: la enumeración se corta en un call que sale de este stem, o en una duda que "
           "no puedes leer aquí. Si el call sale, port_to.\n";
    out << "Informe: {\"action\":\"causal_pilot_worker_v1\",\"kind\":\"gap\","
           "\"walk\":\"NombreFn: acto -> duda:OtraFn\",\"verdict\":\"missing|found\","
           "\"port_to\":\"\",\"why\":\"…símbolo…\",\"brief\":\"2 frases para el piloto\"}\n";
    out << "why/walk DEBEN citar un símbolo de ## Símbolos permitidos (el que leíste). Prosa "
           "tematica sin símbolo se rechaza.\n";
  }
  out << "Responde SOLO JSON.";
  return out.str();
}

std::string registry_causal_pilot_worker_user_prompt(const std::string& kind,
                                                     const std::string& question,
                                                     const std::string& zone_markdown,
                                                     const std::string& notes,
                                                     const std::string& allowed_markdown,
                                                     bool last_turn, const std::string& stem) {
  std::ostringstream out;
  out << "kind=" << kind << "\n";
  const std::string unread_writer =
      (kind == "como" || kind == "gap") ? pilot_unread_dataflow_writer(notes, stem) : std::string{};
  if (last_turn) {
    out << "ÚLTIMO TURNO. Emite causal_pilot_worker_v1 ahora. walk con las dudas que queden "
           "(duda:Symbol). why/brief citan un símbolo de ## Símbolos permitidos. Si no cerraste "
           "el acto: verdict no_cubre o missing. PROHIBIDO pedir tool.\n\n";
  } else if (pilot_notes_need_tool_now(notes)) {
    out << "PROHIBIDO causal_pilot_worker_v1. Responde SOLO una tool del catálogo "
           "(causal_pilot_need_code con target de ## Símbolos permitidos). Follow no es "
           "lectura. No reemitas un informe.\n";
    if (!pilot_notes_have_body(notes)) {
      out << "Sin ----- need_code ----- en ## Notas aún no has leído código.\n";
    } else if (!unread_writer.empty()) {
      out << "Tras dataflow el siguiente JSON es "
             "{\"action\":\"causal_pilot_need_code\",\"target\":\""
          << unread_writer
          << "\"}. Un fn= write no está leído hasta su fence. Un lector no es el clear.\n";
    } else {
      out << "El walk anterior nombró un símbolo que no abriste. need_code de ESE símbolo o "
             "dataflow del campo.\n";
    }
    out << "\n";
  } else if (kind == "cubre" && pilot_notes_have_body(notes)) {
    out << "Ya hay ----- need_code -----. Emite causal_pilot_worker_v1 con cubre|no_cubre. "
           "PROHIBIDO repetir need_code del mismo símbolo (salvo #tail|#mid o un rango de "
           "líneas omitidas). cubre no recorre writers.\n\n";
  } else if ((kind == "como" || kind == "gap") && !unread_writer.empty()) {
    out << "PROHIBIDO causal_pilot_worker_v1. Tras dataflow hay un writer de este stem sin "
           "abrir. Responde SOLO {\"action\":\"causal_pilot_need_code\",\"target\":\""
        << unread_writer
        << "\"}. Un fn= write no está en el walk hasta su ----- need_code -----. Un lector no "
           "es el clear.\n\n";
  } else if ((kind == "como" || kind == "gap") && !pilot_notes_have_flow(notes)) {
    const std::string field = pilot_missing_clear_field(notes);
    if (!field.empty()) {
      out << "PROHIBIDO causal_pilot_worker_v1. aguas_abajo dice que no hay clear. Responde "
             "SOLO {\"action\":\"causal_pilot_dataflow\",\"target\":\""
          << field << "\"}. Una llamada en el recorte no es el clear.\n\n";
    } else {
      out << "Enumera los pasos por los que pasa la consulta en este stem, sin dudas. Si un "
             "paso es un nombre que no has abierto (no hay ----- need_code ----- de ese símbolo), "
             "es duda: pide tool (no emitas no_cubre con duda pendiente). Follow no es lectura. "
             "No copies hops de la ficha al walk. "
             "El informe causal_pilot_worker_v1 lo emites TÚ cuando el árbol de ESTE stem no "
             "tenga dudas locales; no cuando haya dos tools ni cuando el walk sea una cadena "
             "de dos hops si aguas o dataflow aún nombran un hueco.\n";
      out << "Hay un cuerpo. Si la consulta pregunta quién escribe o limpia y no viste el "
             "clear en ese cuerpo, pide causal_pilot_dataflow del campo. Un hop de la ficha "
             "no cierra esa duda.\n\n";
    }
  } else {
    out << "Enumera los pasos por los que pasa la consulta en este stem, sin dudas. Si un "
           "paso es un nombre que no has abierto (no hay ----- need_code ----- de ese símbolo), "
           "es duda: pide tool (no emitas no_cubre con duda pendiente). Follow no es lectura. "
           "No copies hops de la ficha al walk. "
           "El informe causal_pilot_worker_v1 lo emites TÚ cuando el árbol de ESTE stem no "
           "tenga dudas locales; no cuando haya dos tools ni cuando el walk sea una cadena "
           "de dos hops si aguas o dataflow aún nombran un hueco.\n";
    if (notes.find("----- dataflow ") == std::string::npos &&
        notes.find("----- causal ") == std::string::npos) {
      out << "Hay un cuerpo. Si la consulta pregunta quién escribe o limpia y no viste el "
             "clear en ese cuerpo, pide causal_pilot_dataflow del campo. Un hop de la ficha "
             "no cierra esa duda.\n";
    }
    out << "\n";
  }
  if (!question.empty()) {
    out << "## Consulta del usuario\n" << question << "\n\n";
  }
  if (!allowed_markdown.empty()) {
    out << allowed_markdown << "\n";
  }
  out << "## Ficha del barrio\n" << zone_markdown;
  if (!notes.empty()) {
    out << "\n\n## Notas (acumulado)\n" << notes;
  }
  return out.str();
}

RegistryCausalPilotWorkerReport registry_parse_causal_pilot_worker(
    const std::string& raw, const std::string& expected_kind, const std::string& expected_stem,
    const std::string& query, const RegistryCausalPilotWorkerNotebook* notebook) {
  (void)query;
  RegistryCausalPilotWorkerReport out;
  out.raw = raw;
  out.kind = causal_lower(expected_kind);
  out.stem = expected_stem;
  out.owns = expected_stem;
  const auto begin = raw.find('{');
  const auto end = raw.rfind('}');
  if (begin == std::string::npos || end == std::string::npos || end <= begin) {
    out.error = "worker sin objeto JSON";
    return out;
  }
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(raw.substr(begin, end - begin + 1));
  } catch (const std::exception& e) {
    out.error = std::string("JSON worker inválido: ") + e.what();
    return out;
  }
  out.action = j.value("action", "");
  if (out.action == "need_code" || out.action == "causal_pilot_need_code") {
    out.action = "causal_pilot_need_code";
  } else if (out.action == "outline" || out.action == "causal_pilot_outline") {
    out.action = "causal_pilot_outline";
  } else if (out.action == "follow" || out.action == "causal_pilot_follow") {
    out.action = "causal_pilot_follow";
  } else if (out.action == "dataflow" || out.action == "causal_pilot_dataflow") {
    out.action = "causal_pilot_dataflow";
  } else if (out.action == "causal" || out.action == "causal_pilot_causal") {
    out.action = "causal_pilot_causal";
  } else if (out.action == "worker_v1" || out.action == "causal_pilot_worker" ||
             out.action == "causal_pilot_worker_v1") {
    out.action = "causal_pilot_worker_v1";
  }
  auto finish_tool = [&](const std::string& tool, const std::string& err_empty) {
    out.tool = tool;
    out.is_tool = true;
    out.need_code = tool == "need_code";
    if (out.target.size() < 4) {
      out.error = err_empty;
      out.is_tool = false;
      out.need_code = false;
      return false;
    }
    return true;
  };
  if (out.action == "causal_pilot_need_code") {
    out.target = hyp_json_string(j, "target");
    if (out.target.empty()) {
      out.target = hyp_json_string(j, "path_symbol");
    }
    if (!finish_tool("need_code", "need_code sin target")) {
      return out;
    }
    if (!registry_causal_pilot_target_in_stem(out.target, expected_stem)) {
      out.error = "need_code fuera del stem";
      out.is_tool = false;
      out.need_code = false;
      return out;
    }
    if (notebook != nullptr && !notebook->allowed_targets.empty() &&
        !registry_causal_pilot_target_in_notebook(out.target, *notebook)) {
      out.error = "need_code no está en la ficha";
      out.is_tool = false;
      out.need_code = false;
      return out;
    }
    if (notebook != nullptr && !pilot_target_is_windowed(out.target) &&
        pilot_notes_have_tool_target(notebook->notes, "need_code", out.target)) {
      out.error = "need_code repetido";
      out.is_tool = false;
      out.need_code = false;
      return out;
    }
    out.ok = true;
    return out;
  }
  if (out.action == "causal_pilot_outline") {
    out.target = hyp_json_string(j, "target");
    if (out.target.empty()) {
      out.target = hyp_json_string(j, "path");
    }
    if (!finish_tool("outline", "outline sin target")) {
      return out;
    }
    if (!registry_causal_pilot_target_in_stem(out.target, expected_stem)) {
      out.error = "outline fuera del stem";
      out.is_tool = false;
      out.need_code = false;
      return out;
    }
    out.ok = true;
    return out;
  }
  if (out.action == "causal_pilot_follow") {
    if (out.kind == "cubre") {
      out.error = "cubre no usa follow";
      return out;
    }
    out.target = hyp_json_string(j, "target");
    if (out.target.empty()) {
      out.target = hyp_json_string(j, "path_symbol");
    }
    out.direction = causal_lower(hyp_json_string(j, "direction"));
    if (out.direction != "incoming") {
      out.direction = "outgoing";
    }
    if (!finish_tool("follow", "follow sin target")) {
      return out;
    }
    if (notebook != nullptr && !notebook->allowed_targets.empty()) {
      if (!registry_causal_pilot_target_in_notebook(out.target, *notebook)) {
        out.error = "follow no está en la ficha";
        out.is_tool = false;
        out.need_code = false;
        return out;
      }
    } else if (!registry_causal_pilot_target_in_stem(out.target, expected_stem)) {
      out.error = "follow fuera del stem";
      out.is_tool = false;
      out.need_code = false;
      return out;
    }
    out.ok = true;
    return out;
  }
  if (out.action == "causal_pilot_dataflow") {
    out.target = hyp_json_string(j, "target");
    if (out.target.empty()) {
      out.target = hyp_json_string(j, "member");
    }
    const std::string path = hyp_json_string(j, "path");
    if (!path.empty()) {
      out.path_symbol = path;
    }
    {
      const auto slash = out.target.find_last_of("/\\");
      const auto colon = out.target.rfind(':');
      if (colon != std::string::npos && slash != std::string::npos && colon > slash) {
        if (out.path_symbol.empty()) {
          out.path_symbol = out.target.substr(0, colon);
        }
        out.target = out.target.substr(colon + 1);
      }
    }
    if (out.target.size() < 3) {
      out.error = "dataflow sin target";
      return out;
    }
    out.tool = "dataflow";
    out.is_tool = true;
    if (!out.path_symbol.empty() &&
        !registry_causal_pilot_target_in_stem(out.path_symbol, expected_stem)) {
      out.error = "dataflow fuera del stem";
      out.is_tool = false;
      return out;
    }
    if (notebook != nullptr &&
        pilot_notes_have_tool_target(notebook->notes, "dataflow", out.target)) {
      out.error = "dataflow repetido";
      out.is_tool = false;
      return out;
    }
    out.ok = true;
    return out;
  }
  if (out.action == "causal_pilot_causal") {
    out.target = hyp_json_string(j, "target");
    if (out.target.empty()) {
      out.target = hyp_json_string(j, "path_symbol");
    }
    if (!finish_tool("causal", "causal sin target")) {
      return out;
    }
    if (!registry_causal_pilot_target_in_stem(out.target, expected_stem)) {
      out.error = "causal fuera del stem";
      out.is_tool = false;
      out.need_code = false;
      return out;
    }
    if (notebook != nullptr && !notebook->allowed_targets.empty() &&
        !registry_causal_pilot_target_in_notebook(out.target, *notebook)) {
      out.error = "causal no está en la ficha";
      out.is_tool = false;
      out.need_code = false;
      return out;
    }
    out.ok = true;
    return out;
  }
  if (out.action != "causal_pilot_worker_v1") {
    out.error = "contrato causal_pilot_worker_v1 inválido";
    return out;
  }
  const std::string kind = causal_lower(hyp_json_string(j, "kind"));
  if (!kind.empty() && kind != out.kind) {
    out.kind = kind;
  }
  out.verdict = causal_lower(hyp_json_string(j, "verdict"));
  {
    const auto bar = out.verdict.find('|');
    if (bar != std::string::npos) {
      out.verdict = out.verdict.substr(0, bar);
    }
  }
  out.owns = hyp_json_string(j, "owns");
  if (out.owns.empty()) {
    out.owns = expected_stem;
  }
  out.why = hyp_json_string(j, "why");
  if (out.why.size() > 240) {
    out.why.resize(240);
  }
  out.brief = hyp_json_string(j, "brief");
  if (out.brief.empty()) {
    out.brief = hyp_json_string(j, "summary");
  }
  if (out.brief.empty() && pilot_text_is_brief(out.why)) {
    out.brief = out.why;
  }
  if (out.brief.size() > 400) {
    out.brief.resize(400);
  }
  out.chain = hyp_json_string(j, "chain");
  if (out.chain == "trigger→estado→efecto" || out.chain == "trigger->estado->efecto") {
    out.chain.clear();
  }
  out.walk = hyp_json_string(j, "walk");
  if (out.walk.empty() && j.contains("walk") && j["walk"].is_array()) {
    std::ostringstream w;
    for (const auto& step : j["walk"]) {
      if (!step.is_string()) {
        continue;
      }
      const std::string s = step.get<std::string>();
      if (s.empty()) {
        continue;
      }
      if (!w.str().empty()) {
        w << " -> ";
      }
      w << s;
    }
    out.walk = w.str();
  }
  if (out.walk.empty()) {
    out.walk = out.chain;
  }
  if (out.chain.empty()) {
    out.chain = out.walk;
  }
  if (causal_lower(out.walk).find("sym:") != std::string::npos) {
    out.error = "walk de plantilla";
    return out;
  }
  if (out.why.size() < 8 && out.walk.size() >= 8) {
    out.why = out.walk;
    if (out.why.size() > 240) {
      out.why.resize(240);
    }
  }
  if (out.brief.empty() && pilot_text_is_brief(out.walk)) {
    out.brief = out.walk;
    if (out.brief.size() > 400) {
      out.brief.resize(400);
    }
  }
  out.path_symbol = hyp_json_string(j, "path_symbol");
  if (out.path_symbol == "path.cpp:fn" || out.path_symbol == "src/foo.cpp:Symbol" ||
      (out.path_symbol.size() >= 3 &&
       out.path_symbol.compare(out.path_symbol.size() - 3, 3, ":Fn") == 0) ||
      out.path_symbol.find("append_top_level_tabs_header") != std::string::npos) {
    out.path_symbol.clear();
  }
  if (out.chain.size() < 8 && out.why.size() >= 12) {
    out.chain = out.why;
  }
  out.port_to = hyp_json_string(j, "port_to");
  {
    const std::string pl = causal_lower(out.port_to);
    if (pl.empty() || pl == "stem_vecino" || pl == "vecino" || pl == "none" || pl == "n/a" ||
        pl.find(' ') != std::string::npos) {
      out.port_to.clear();
    }
  }
  if (notebook != nullptr) {
    if (out.kind == "cubre" && notebook->allowed_targets.empty() && !notebook->used_tool()) {
      out.error = "ficha vacía: pide outline o need_code";
      return out;
    }
    const bool closing = pilot_notes_are_closing(notebook->notes);
    if (!closing && !pilot_notes_have_body(notebook->notes)) {
      out.error = "sin lectura de código";
      return out;
    }
    if (out.walk.empty() && out.why.size() >= 8) {
      out.walk = out.why;
      if (out.chain.empty()) {
        out.chain = out.why;
      }
    }
    const std::string cite_hay = out.why + " " + out.brief + " " + out.walk + " " + out.chain +
                                 " " + out.path_symbol;
    if (!notebook->allowed_targets.empty() && !pilot_why_cites_targets(cite_hay, notebook->allowed_targets)) {
      out.error = "why no cita un símbolo de la ficha";
      return out;
    }
    if ((out.kind == "como" || out.kind == "gap") &&
        (out.verdict == "chain" || out.verdict == "found") &&
        !pilot_notes_are_closing(notebook->notes) && !pilot_notes_have_flow(notebook->notes)) {
      const std::string field = pilot_missing_clear_field(notebook->notes);
      if (!field.empty()) {
        out.error = "falta dataflow del campo: " + field;
        return out;
      }
    }
    if ((out.kind == "como" || out.kind == "gap") &&
        (out.verdict == "chain" || out.verdict == "found") &&
        !pilot_notes_are_closing(notebook->notes)) {
      const std::string writer = pilot_unread_dataflow_writer(notebook->notes, expected_stem);
      if (!writer.empty() && !pilot_walk_has_duda_of(out.walk, writer)) {
        out.error = "falta need_code del writer: " + writer;
        return out;
      }
    }
    if (out.verdict == "cubre" || out.verdict == "chain" || out.verdict == "found") {
      const std::string unread = pilot_walk_first_unread(out.walk, *notebook);
      if (!unread.empty()) {
        out.error = "walk nombra un símbolo no leído: " + unread;
        return out;
      }
    }
    if (!pilot_text_is_brief(out.brief)) {
      out.error = "brief corto: 2 frases para el piloto";
      return out;
    }
  }
  if (out.kind == "cubre") {
    if (out.verdict != "cubre" && out.verdict != "no_cubre") {
      out.error = "verdict cubre inválido";
      return out;
    }
    out.covers = out.verdict == "cubre";
  } else if (out.kind == "como") {
    if (out.verdict != "chain" && out.verdict != "no_cubre") {
      out.error = "verdict como inválido";
      return out;
    }
    out.covers = out.verdict == "chain";
    if (out.chain.size() < 8 && out.verdict == "chain") {
      out.error = "chain demasiado corta";
      return out;
    }
    if (out.verdict == "no_cubre" && notebook != nullptr &&
        notebook->notes.find("sin hops") != std::string::npos &&
        pilot_notes_unique_code_targets(notebook->notes) <= 1) {
      out.error = "follow vacío no es no_cubre";
      out.covers = false;
      return out;
    }
  } else if (out.kind == "gap") {
    if (out.verdict != "missing" && out.verdict != "found") {
      out.error = "verdict gap inválido";
      return out;
    }
  } else {
    out.error = "kind worker desconocido";
    return out;
  }
  if (out.why.size() < 8) {
    out.error = "why worker corto";
    return out;
  }
  out.ok = true;
  return out;
}

nlohmann::json registry_causal_pilot_worker_to_json(const RegistryCausalPilotWorkerReport& r) {
  return {{"ok", r.ok},
          {"need_code", r.need_code},
          {"is_tool", r.is_tool},
          {"tool", r.tool},
          {"action", r.action},
          {"kind", r.kind},
          {"zone", r.zone},
          {"stem", r.stem},
          {"verdict", r.verdict},
          {"covers", r.covers},
          {"owns", r.owns},
          {"walk", r.walk},
          {"chain", r.chain},
          {"path_symbol", r.path_symbol},
          {"port_to", r.port_to},
          {"target", r.target},
          {"direction", r.direction},
          {"why", r.why},
          {"brief", r.brief},
          {"error", r.error},
          {"steps", r.steps}};
}

std::string registry_causal_pilot_worker_markdown(const RegistryCausalPilotWorkerReport& r) {
  std::ostringstream out;
  out << "# worker " << r.kind << " " << r.zone << " stem=" << r.stem << "\n";
  out << "ok=" << (r.ok ? "yes" : "no");
  if (r.is_tool) {
    out << " tool=" << r.tool << " target=" << r.target;
    if (!r.direction.empty()) {
      out << " dir=" << r.direction;
    }
  } else if (r.kind == "cubre") {
    out << " RESULTADO=" << (r.covers ? "CUBRE" : "NO_CUBRE");
  } else if (r.kind == "como") {
    out << " RESULTADO=" << (r.covers ? "CADENA" : "SIN_CADENA");
  } else {
    out << " RESULTADO=" << r.verdict;
  }
  if (r.need_code && !r.is_tool) {
    out << " need_code=" << r.target;
  }
  out << " steps=" << r.steps << "\n";
  if (!r.brief.empty()) {
    out << "brief: " << r.brief << "\n";
  }
  if (!r.walk.empty()) {
    out << "walk: " << r.walk << "\n";
  }
  if (!r.chain.empty() && r.chain != r.walk) {
    out << "chain: " << r.chain << "\n";
  }
  if (!r.path_symbol.empty()) {
    out << "path_symbol: " << r.path_symbol << "\n";
  }
  if (!r.port_to.empty()) {
    out << "port_to: " << r.port_to << "\n";
  }
  if (!r.why.empty()) {
    out << "why: " << r.why << "\n";
  }
  if (!r.error.empty()) {
    out << "error: " << r.error << "\n";
  }
  return out.str();
}

std::string registry_causal_pilot_plenary_system_prompt() {
  return R"(Eres el PILOTO en plenario. Recibes informes de trabajadores sordos. NO lees fichas.
cubre=sí en un barrio y cubre=no en otro NO es contradicción: es el filtro de trampas.

verdict:
- entiendo: al menos un cubre=cubre nombra el objeto de la consulta.
  keep = SOLO esos stems (nunca un como/chain). drop = no_cubre, como en trampa, gaps.
- no_entiendo: TODOS los cubre son no_cubre. como=chain NO basta para keep.
- abandono: los barrios no tienen nada que ver (chrome, toast, sha256).

JSON:
{"action":"causal_pilot_plenary_v1","verdict":"entiendo","keep":["stem"],"drop":["stem"],"why":"…"})";
}

std::string registry_causal_pilot_plenary_user_prompt(const std::string& reports_markdown) {
  return "Lee los informes (veredicto + brief) y decide. keep/drop solo stems que aparecen.\n\n" +
         reports_markdown;
}

RegistryCausalPilotPlenary registry_parse_causal_pilot_plenary(
    const std::string& raw, const std::vector<std::string>& allowed_stems) {
  RegistryCausalPilotPlenary out;
  out.raw = raw;
  out.action = "causal_pilot_plenary_v1";
  const auto begin = raw.find('{');
  const auto end = raw.rfind('}');
  if (begin == std::string::npos || end == std::string::npos || end <= begin) {
    out.error = "plenario sin objeto JSON";
    return out;
  }
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(raw.substr(begin, end - begin + 1));
  } catch (const std::exception& e) {
    out.error = std::string("JSON plenario inválido: ") + e.what();
    return out;
  }
  if (j.value("action", "") != "causal_pilot_plenary_v1") {
    out.error = "contrato causal_pilot_plenary_v1 inválido";
    return out;
  }
  out.verdict = causal_lower(hyp_json_string(j, "verdict"));
  static const std::unordered_set<std::string> kV = {"entiendo", "no_entiendo", "abandono"};
  if (!kV.count(out.verdict)) {
    out.error = "verdict plenario inválido";
    return out;
  }
  std::unordered_set<std::string> allowed;
  for (const auto& s : allowed_stems) {
    if (!s.empty()) {
      allowed.insert(causal_lower(s));
    }
  }
  auto take_list = [&](const char* key, std::vector<std::string>* dest) {
    if (!j.contains(key) || !j[key].is_array()) {
      return;
    }
    for (const auto& item : j[key]) {
      std::string s = item.is_string() ? item.get<std::string>() : std::string{};
      if (s.empty() || !allowed.count(causal_lower(s))) {
        continue;
      }
      dest->push_back(s);
    }
  };
  take_list("keep", &out.keep);
  take_list("drop", &out.drop);
  out.why = hyp_json_string(j, "why");
  if (out.why.size() < 8) {
    out.error = "why plenario corto";
    return out;
  }
  if (out.why.size() > 240) {
    out.why.resize(240);
  }
  if (out.verdict == "entiendo" && out.keep.empty()) {
    out.error = "entiendo sin keep";
    return out;
  }
  if (out.verdict == "no_entiendo" && !out.keep.empty()) {
    out.verdict = "entiendo";
  }
  out.ok = true;
  return out;
}

void registry_tally_pilot_plenary(RegistryCausalPilotPlenary* plenary,
                                  const std::vector<RegistryCausalPilotWorkerReport>& reports) {
  if (plenary == nullptr || plenary->verdict == "abandono") {
    return;
  }
  std::vector<std::string> keep;
  std::vector<std::string> drop;
  std::unordered_set<std::string> seen_keep;
  std::unordered_set<std::string> seen_drop;
  for (const auto& r : reports) {
    if (!r.ok || r.stem.empty()) {
      continue;
    }
    const std::string sl = causal_lower(r.stem);
    const bool cubre_yes = r.kind == "cubre" && r.covers && r.verdict == "cubre";
    if (cubre_yes) {
      if (seen_keep.insert(sl).second) {
        keep.push_back(r.stem);
      }
    } else if (r.verdict == "no_cubre" || r.verdict == "missing") {
      if (seen_drop.insert(sl).second) {
        drop.push_back(r.stem);
      }
    }
  }
  std::vector<std::string> drop_only;
  for (const auto& s : drop) {
    if (seen_keep.count(causal_lower(s)) == 0) {
      drop_only.push_back(s);
    }
  }
  plenary->keep = keep;
  plenary->drop = drop_only;
  if (!plenary->keep.empty()) {
    plenary->verdict = "entiendo";
    plenary->ok = true;
    plenary->error.clear();
    if (plenary->why.size() < 8) {
      plenary->why = "tally: keep solo de cubre=cubre";
    }
  } else if (plenary->verdict == "entiendo" || plenary->verdict.empty()) {
    plenary->verdict = "no_entiendo";
    if (plenary->why.size() < 8) {
      plenary->why = "tally: ningún cubre=cubre";
    }
  }
}

nlohmann::json registry_causal_pilot_plenary_to_json(const RegistryCausalPilotPlenary& p) {
  return {{"ok", p.ok},   {"action", p.action}, {"verdict", p.verdict}, {"keep", p.keep},
          {"drop", p.drop}, {"why", p.why},       {"error", p.error}};
}

std::string registry_causal_pilot_plenary_markdown(const RegistryCausalPilotPlenary& p) {
  std::ostringstream out;
  out << "# causal_pilot_plenary_v1\n";
  out << "ok=" << (p.ok ? "yes" : "no") << " verdict=" << p.verdict << "\n";
  if (!p.why.empty()) {
    out << "why: " << p.why << "\n";
  }
  if (!p.error.empty()) {
    out << "error: " << p.error << "\n";
  }
  out << "keep:";
  for (const auto& s : p.keep) {
    out << " " << s;
  }
  out << "\n drop:";
  for (const auto& s : p.drop) {
    out << " " << s;
  }
  out << "\n";
  return out.str();
}

std::string registry_causal_judge_system_prompt() {
  return R"(Eres un juez de localización causal de código.
Recibirás una consulta y fichas de zonas construidas exclusivamente con evidencia del repositorio.

Decide cada zona por su MECANISMO, no por su score:
- select: explica directamente una parte necesaria del síntoma/objetivo.
- reject: trata otro comportamiento, es infraestructura genérica o su relación es solo léxica.

Usa anchors, writes/reads, aristas, outlines, closure, trails y riesgos. Un score alto es solo un prior.
No inventes llamadas ni estado ausente. Selecciona hasta 3 zonas solo si sus contribuciones son
causalmente distintas y necesarias (por ejemplo trigger, state_owner, cleanup, consumer o boundary).
Debe haber exactamente una role=primary entre las seleccionadas. Si una primary ya cubre el
comportamiento pedido, rechaza zonas meramente relacionadas; no las conserves "por si acaso".
Antes de declarar complete, separa todos los objetivos explícitos de la consulta. El conjunto
seleccionado debe cubrirlos todos. Una zona de estado o visualización no cubre por sí sola una acción
de cancelar, reiniciar, limpiar, persistir o abrir. Si otra ficha contiene una función cuyo nombre
coincide directamente con una de esas acciones solicitadas, selecciónala con el role complementario
adecuado; no la rechaces solo porque no posee núcleo de estado.
Una zona select puede ser complete o partial. partial exige explicar el enlace ausente y desde
qué símbolos expandir. Evalúa TODOS los ids M* mostrados.
Responde SOLO un objeto JSON, sin markdown:
Claves raíz: action, zones, selected, next, why.
action debe ser causal_zone_judge.
zones debe ser un OBJETO cuyas claves sean todos los ids M*. Cada valor contiene el veredicto;
no repitas id dentro. selected es lista de ids y why raíz es una frase.
verdict es select o reject. confidence está entre 0.05 y 1.
Para select: role=primary|trigger|state_owner|cleanup|consumer|boundary,
completeness=complete|partial y contribution describe su aporte exclusivo.
Para reject emite SOLO id, verdict, confidence y why; el runtime completa el resto como none/vacío.
why debe citar evidencia concreta de la ficha (símbolos, estado o aristas), nunca frases genéricas.
why de select máximo 140 caracteres; why de reject máximo 80; contribution máximo 80;
missing_link máximo 100.
No repitas la consulta ni describas toda la zona.
select+partial exige missing_link concreto y expand_from con 1–4 símbolos de ESA ficha.
select+complete exige missing_link="" y expand_from=[].
selected contiene exactamente los ids con verdict=select.
next=expand si alguna seleccionada es partial; verify si todas son complete; none si no hay select.)";
}

std::string registry_causal_judge_user_prompt(const std::string& cards_markdown) {
  const auto ids = registry_causal_judge_zone_ids(cards_markdown);
  std::ostringstream required;
  for (std::size_t i = 0; i < ids.size(); ++i) {
    required << (i ? "," : "") << ids[i];
  }
  return "## Fichas zonales\n" + cards_markdown +
         "\n## Restricciones de salida\nDebes emitir exactamente " +
         std::to_string(ids.size()) + " veredictos, uno por id y en este orden: " +
         required.str() +
         ".\nCada why debe mencionar al menos un símbolo o estado visible en ESA zona. "
         "Compara contra la primary: no selecciones una zona secundaria si no añade una "
         "contribución causal distinta. "
         "No copies texto del contrato. No uses conocimiento fuera de estas fichas.\n";
}

std::string registry_causal_synth_system_prompt() {
  return R"(Eres un sintetizador causal: confirmas o falsificas una HIPÓTESIS con fichas expandidas.
No elijas temas relacionados. Pregunta primero: ¿la evidencia confirma, sostiene parcialmente o
falsifica la hipótesis recibida?

Regla clave de falsificación:
- incompleto ≠ falso. Si la zona es un eslabón necesario (entrada, owner, trigger) pero falta un
  brazo, usa select+partial (o hypothesis_status=partial) y pide expand_from; NO la rechaces.
- falsified solo si el mecanismo de la ficha es OTRO comportamiento o la relación es solo léxica.

Luego emite veredictos por zona:
- select: pieza causalmente necesaria del síntoma bajo esa hipótesis (aunque falte un enlace).
- reject: otro comportamiento, infraestructura genérica o solo coincidencia léxica.

Máximo 3 select con roles causalmente distintos y exactamente una primary.
Roles válidos SOLO: primary|trigger|state_owner|cleanup|consumer|boundary.
confidence es un NÚMERO entre 0.05 y 1 (nunca texto).
why y contribution deben citar símbolos/estado REALES de la ficha; PROHIBIDO copiar plantillas
vacías. select+partial exige missing_link y expand_from (1–4 targets exactos de ESA ficha).
select+complete: missing_link="" y expand_from=[].
Responde SOLO JSON válido sin comas finales. Claves raíz obligatorias:
action=causal_zone_judge, hypothesis_status, reinvestigate_need, zones (objeto M*→veredicto),
selected, next, why.
Cada zona select: verdict,role,completeness,confidence,why,contribution.
Cada zona reject: verdict,confidence,why.
hypothesis_status=confirmed → next=verify (sin partial) o expand (con partial).
hypothesis_status=partial → next=expand o verify; debe haber al menos un select.
hypothesis_status=falsified → next=reinvestigate y reinvestigate_need concreto (12–160 chars);
solo cuando el hilo es el mecanismo equivocado.
Si no hay select y no falsificas: next=none.)";
}

std::string registry_causal_synth_user_prompt(const std::string& cards_markdown,
                                             const std::string& hypothesis,
                                             const std::string& anchor_why) {
  const auto ids = registry_causal_judge_zone_ids(cards_markdown);
  std::ostringstream required;
  for (std::size_t i = 0; i < ids.size(); ++i) {
    required << (i ? "," : "") << ids[i];
  }
  std::ostringstream out;
  out << "## Hipótesis a comprobar\n" << hypothesis << "\n";
  if (!anchor_why.empty()) {
    out << "## Por qué se ancló\n" << anchor_why << "\n";
  }
  out << "## Fichas zonales expandidas\n" << cards_markdown
      << "\n## Restricciones\nEmite exactamente " << ids.size()
      << " veredictos en este orden de ids: " << required.str()
      << ".\nPrimero hypothesis_status; luego select/reject. "
         "Cada why/contribution debe mencionar un símbolo o estado visible en ESA ficha. "
         "No inventes evidencia ni copies frases vacías del contrato.\n";
  return out.str();
}

std::vector<std::string> registry_causal_judge_zone_ids(const std::string& cards_markdown) {
  std::vector<std::string> ids;
  std::unordered_set<std::string> seen;
  std::istringstream in(cards_markdown);
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("## M", 0) != 0) {
      continue;
    }
    const auto end = line.find(' ', 3);
    const std::string id = line.substr(3, end == std::string::npos ? std::string::npos : end - 3);
    if (!id.empty() && seen.insert(id).second) {
      ids.push_back(id);
    }
  }
  return ids;
}

namespace {

bool judge_text_is_placeholder(const std::string& text) {
  static const char* kPlaceholders[] = {
      "evidencia concreta",
      "evidencia causal concreta",
      "evidencia parcial",
      "aporte",
      "aporte parcial",
      "síntesis",
      "sintesis",
      "síntesis breve",
      "síntesis con evidencia",
      "síntesis parcial con evidencia",
      "razón concreta",
  };
  for (const char* placeholder : kPlaceholders) {
    if (text == placeholder) {
      return true;
    }
  }
  return false;
}

}  // namespace

RegistryCausalJudgeDecision registry_parse_causal_judge_decision(
    const std::string& raw, const std::vector<std::string>& allowed_zone_ids) {
  RegistryCausalJudgeDecision out;
  out.raw = raw;
  const auto begin = raw.find('{');
  const auto end = raw.rfind('}');
  if (begin == std::string::npos || end == std::string::npos || end <= begin) {
    out.error = "respuesta sin objeto JSON";
    return out;
  }
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(raw.substr(begin, end - begin + 1));
  } catch (const std::exception& e) {
    out.error = std::string("JSON inválido: ") + e.what();
    return out;
  }
  if (j.value("action", "") != "causal_zone_judge" || !j.contains("zones")) {
    out.error = "contrato causal_zone_judge inválido";
    return out;
  }
  const std::unordered_set<std::string> allowed(allowed_zone_ids.begin(), allowed_zone_ids.end());
  std::unordered_set<std::string> seen;
  std::vector<nlohmann::json> zone_items;
  if (j["zones"].is_object()) {
    for (auto it = j["zones"].begin(); it != j["zones"].end(); ++it) {
      nlohmann::json item;
      if (it.value().is_string()) {
        const std::string verdict = it.value().get<std::string>();
        std::string why = j.value("why", "");
        if (why.size() > 140) {
          why.resize(140);
        }
        item = {{"verdict", verdict},
                {"role", verdict == "select" ? "primary" : "none"},
                {"completeness", verdict == "select" ? "complete" : "none"},
                {"confidence", 0.8f},
                {"why", why},
                {"contribution", verdict == "select" ? why : ""},
                {"missing_link", ""},
                {"expand_from", nlohmann::json::array()}};
      } else if (it.value().is_object()) {
        item = it.value();
      } else {
        out.error = "veredicto de zona no es objeto";
        return out;
      }
      item["id"] = it.key();
      zone_items.push_back(std::move(item));
    }
  } else if (j["zones"].is_array()) {
    const bool compact_index =
        !j["zones"].empty() && j["zones"].front().is_string() && j.contains("why") &&
        j["why"].is_object();
    for (const auto& raw_item : j["zones"]) {
      if (compact_index && raw_item.is_string()) {
        const std::string id = raw_item.get<std::string>();
        if (!j["why"].contains(id) || !j["why"][id].is_object()) {
          out.error = "falta veredicto compacto para " + id;
          return out;
        }
        nlohmann::json item = j["why"][id];
        item["id"] = id;
        zone_items.push_back(std::move(item));
      } else {
        zone_items.push_back(raw_item);
      }
    }
  } else {
    out.error = "zones no es objeto ni array";
    return out;
  }
  for (const auto& item : zone_items) {
    if (!item.is_object()) {
      out.error = "veredicto de zona no es objeto";
      return out;
    }
    RegistryZoneVerdict verdict;
    verdict.id = item.value("id", "");
    verdict.verdict = item.value("verdict", "");
    if (verdict.verdict.empty() && item.contains("veredict") && item["veredict"].is_string()) {
      verdict.verdict = item["veredict"].get<std::string>();
    }
    if (verdict.verdict.empty() && item.contains("select")) {
      verdict.verdict = "select";
      if (item["select"].is_string() && verdict.contribution.empty()) {
        // 7B a veces pone el símbolo en "select" en lugar de verdict.
        verdict.contribution = item["select"].get<std::string>();
      }
    }
    if (verdict.verdict.empty() && item.contains("reject")) {
      verdict.verdict = "reject";
    }
    verdict.role = item.value("role", verdict.verdict == "reject" ? "none" : "");
    if (verdict.role == "mutator" || verdict.role == "writer" || verdict.role == "owner") {
      verdict.role = "state_owner";
    } else if (verdict.role == "entry" || verdict.role == "handler") {
      verdict.role = "trigger";
    } else if (verdict.role == "reader" || verdict.role == "user") {
      verdict.role = "consumer";
    }
    verdict.completeness =
        item.value("completeness", verdict.verdict == "reject" ? "none" : "");
    verdict.confidence = 0.f;
    if (item.contains("confidence")) {
      const auto& conf = item["confidence"];
      if (conf.is_number()) {
        verdict.confidence = conf.get<float>();
      } else if (conf.is_string()) {
        const std::string label = conf.get<std::string>();
        if (label == "high" || label == "alta" || label == "High") {
          verdict.confidence = 0.85f;
        } else if (label == "medium" || label == "media" || label == "Medium") {
          verdict.confidence = 0.6f;
        } else if (label == "low" || label == "baja" || label == "Low") {
          verdict.confidence = 0.35f;
        } else {
          try {
            verdict.confidence = std::stof(label);
          } catch (...) {
            verdict.confidence = 0.f;
          }
        }
      }
    }
    verdict.why = item.value("why", "");
    verdict.contribution = item.value("contribution", verdict.contribution);
    verdict.missing_link = item.value("missing_link", "");
    if (verdict.why.empty() && !verdict.contribution.empty()) {
      verdict.why = verdict.contribution;
    }
    if (item.contains("expand_from") && item["expand_from"].is_array()) {
      for (const auto& value : item["expand_from"]) {
        if (value.is_string() && !value.get<std::string>().empty() &&
            verdict.expand_from.size() < 4) {
          verdict.expand_from.push_back(value.get<std::string>());
        }
      }
    }
    if (verdict.verdict == "select" && verdict.contribution.empty() && !verdict.why.empty()) {
      verdict.contribution = verdict.why.size() > 140 ? verdict.why.substr(0, 140) : verdict.why;
    }
    if (verdict.verdict == "select" &&
        (verdict.contribution.size() < 8 || judge_text_is_placeholder(verdict.contribution)) &&
        !judge_text_is_placeholder(verdict.why) && verdict.why.size() >= 12) {
      verdict.contribution = verdict.why.size() > 140 ? verdict.why.substr(0, 140) : verdict.why;
    }
    if (verdict.verdict == "select" && verdict.role.empty()) {
      verdict.role = "primary";
    }
    if (verdict.verdict == "select" && verdict.completeness.empty()) {
      verdict.completeness = "complete";
    }
    if (verdict.verdict == "select" && verdict.completeness == "partial" &&
        verdict.expand_from.empty()) {
      verdict.completeness = "complete";
      verdict.missing_link.clear();
    }
    if (!allowed.count(verdict.id) || !seen.insert(verdict.id).second) {
      out.error = "id de zona desconocido o repetido: " + verdict.id;
      return out;
    }
    if (verdict.verdict != "select" && verdict.verdict != "reject") {
      out.error = "verdict inválido para " + verdict.id;
      return out;
    }
    if (verdict.confidence < 0.05f || verdict.confidence > 1.f || verdict.why.size() < 12 ||
        verdict.why.size() > 240 || judge_text_is_placeholder(verdict.why)) {
      out.error = "confidence/why inválido para " + verdict.id;
      return out;
    }
    if (verdict.verdict == "reject" && verdict.why.size() > 120) {
      truncate_utf8(&verdict.why, 120);
    }
    const bool valid_select_role =
        verdict.role == "primary" || verdict.role == "trigger" ||
        verdict.role == "state_owner" || verdict.role == "cleanup" ||
        verdict.role == "consumer" || verdict.role == "boundary";
    if (verdict.verdict == "select") {
      if (!valid_select_role ||
          (verdict.completeness != "complete" && verdict.completeness != "partial") ||
          verdict.contribution.size() < 8 || verdict.contribution.size() > 140 ||
          judge_text_is_placeholder(verdict.contribution)) {
        out.error = "role/completeness/contribution inválido para " + verdict.id;
        return out;
      }
      if (verdict.completeness == "partial") {
        if (verdict.missing_link.size() < 12 || verdict.missing_link.size() > 160 ||
            verdict.expand_from.empty()) {
          out.error = "select partial sin enlace/expansión para " + verdict.id;
          return out;
        }
      } else if (!verdict.missing_link.empty() || !verdict.expand_from.empty()) {
        out.error = "select complete contiene expansión para " + verdict.id;
        return out;
      }
      out.selected.push_back(verdict.id);
    } else {
      // El 7B a menudo deja contribution/role en reject; se ignoran.
      verdict.role = "none";
      verdict.completeness = "none";
      verdict.contribution.clear();
      verdict.missing_link.clear();
      verdict.expand_from.clear();
    }
    out.zones.push_back(std::move(verdict));
  }
  if (seen.size() != allowed.size()) {
    out.error = "faltan veredictos: recibidos=" + std::to_string(seen.size()) +
                " esperados=" + std::to_string(allowed.size());
    return out;
  }
  if (out.selected.size() > 3) {
    out.error = "más de 3 zonas seleccionadas";
    return out;
  }
  int primary_n = 0;
  int partial_n = 0;
  std::unordered_set<std::string> contributions;
  for (auto& verdict : out.zones) {
    if (verdict.verdict != "select") {
      continue;
    }
    primary_n += verdict.role == "primary" ? 1 : 0;
    partial_n += verdict.completeness == "partial" ? 1 : 0;
    if (!contributions.insert(verdict.contribution).second) {
      out.error = "contribuciones seleccionadas duplicadas";
      return out;
    }
  }
  if (!out.selected.empty() && primary_n == 0) {
    auto best = out.zones.end();
    for (auto it = out.zones.begin(); it != out.zones.end(); ++it) {
      if (it->verdict == "select" &&
          (best == out.zones.end() || it->confidence > best->confidence)) {
        best = it;
      }
    }
    if (best != out.zones.end()) {
      best->role = "primary";
      primary_n = 1;
    }
  }
  if ((!out.selected.empty() && primary_n != 1) || (out.selected.empty() && primary_n != 0)) {
    out.error = "debe haber una única primary si hay selección";
    return out;
  }
  if (!j.contains("selected")) {
    // 7B a veces omite selected: se deriva de verdict=select.
  } else if (j["selected"].is_string()) {
    // accepted; derived wins below
  } else if (j["selected"].is_array()) {
    for (const auto& value : j["selected"]) {
      if (!value.is_string()) {
        out.error = "selected inválido";
        return out;
      }
    }
  } else {
    out.error = "selected inválido";
    return out;
  }
  // La fuente de verdad es verdict=select (out.selected ya derivado).
  out.next = j.value("next", "");
  out.hypothesis_status = j.value("hypothesis_status", "");
  out.reinvestigate_need = j.value("reinvestigate_need", "");
  if (out.reinvestigate_need.size() > 160) {
    truncate_utf8(&out.reinvestigate_need, 160);
  }
  // Si hay selección útil, ignorar need residual del 7B.
  if (!out.selected.empty() && out.next != "reinvestigate") {
    out.reinvestigate_need.clear();
    if (out.hypothesis_status == "falsified") {
      out.hypothesis_status = partial_n > 0 ? "partial" : "confirmed";
    }
  }
  // El 7B a veces marca partial/expand sin select pero con need → reapertura.
  if (out.selected.empty() && !out.reinvestigate_need.empty() &&
      (out.next == "expand" || out.next == "reinvestigate" || out.next == "none" ||
       out.hypothesis_status == "partial" || out.hypothesis_status == "falsified")) {
    out.hypothesis_status = "falsified";
    out.next = "reinvestigate";
  }
  if (j.contains("why") && j["why"].is_string()) {
    out.why = j["why"].get<std::string>();
  } else {
    for (const auto& verdict : out.zones) {
      if (verdict.role == "primary") {
        out.why = verdict.contribution.empty() ? verdict.why : verdict.contribution;
        break;
      }
    }
  }
  if (out.why.size() < 12 || judge_text_is_placeholder(out.why)) {
    for (const auto& verdict : out.zones) {
      if (verdict.role == "primary" && verdict.why.size() >= 12 &&
          !judge_text_is_placeholder(verdict.why)) {
        out.why = verdict.why;
        break;
      }
    }
  }
  if (out.why.size() > 240) {
    truncate_utf8(&out.why, 240);
  }
  if ((out.next != "verify" && out.next != "expand" && out.next != "none" &&
       out.next != "reinvestigate") ||
      out.why.size() < 12 || out.why.size() > 240 || out.why == "síntesis breve") {
    out.error = "next/why global inválido";
    return out;
  }
  if (!out.hypothesis_status.empty() && out.hypothesis_status != "confirmed" &&
      out.hypothesis_status != "partial" && out.hypothesis_status != "falsified") {
    out.error = "hypothesis_status inválido";
    return out;
  }
  if (out.next == "reinvestigate") {
    if (out.hypothesis_status != "falsified" || out.reinvestigate_need.size() < 12 ||
        out.reinvestigate_need.size() > 160) {
      out.error = "reinvestigate sin falsificación/need";
      return out;
    }
  } else {
    if (!out.reinvestigate_need.empty()) {
      out.error = "reinvestigate_need solo con next=reinvestigate";
      return out;
    }
    const std::string expected_next =
        out.selected.empty() ? "none" : (partial_n > 0 ? "expand" : "verify");
    if (out.next != expected_next) {
      // El 7B mezcla partial/verify con frecuencia; alinear al estado real.
      out.next = expected_next;
    }
    if (out.hypothesis_status == "falsified") {
      out.error = "falsified exige next=reinvestigate";
      return out;
    }
  }
  out.ok = true;
  return out;
}

nlohmann::json registry_causal_judge_decision_to_json(
    const RegistryCausalJudgeDecision& decision) {
  nlohmann::json zones = nlohmann::json::array();
  for (const auto& verdict : decision.zones) {
    zones.push_back({{"id", verdict.id},
                     {"verdict", verdict.verdict},
                     {"role", verdict.role},
                     {"completeness", verdict.completeness},
                     {"confidence", verdict.confidence},
                     {"why", verdict.why},
                     {"contribution", verdict.contribution},
                     {"missing_link", verdict.missing_link},
                     {"expand_from", verdict.expand_from}});
  }
  return {{"ok", decision.ok},
          {"action", "causal_zone_judge"},
          {"zones", zones},
          {"selected", decision.selected},
          {"next", decision.next},
          {"hypothesis_status", decision.hypothesis_status},
          {"reinvestigate_need", decision.reinvestigate_need},
          {"why", decision.why},
          {"error", decision.error}};
}

nlohmann::json registry_node_to_json(const RegistryNodeRow& n) {
  nlohmann::json j = {{"id", n.id},
                      {"kind", n.kind},
                      {"path", n.path},
                      {"symbol", n.symbol},
                      {"stem", n.stem},
                      {"line", n.line},
                      {"parent_fn", n.parent_fn},
                      {"ctrl_kind", n.ctrl_kind},
                      {"cond", n.cond},
                      {"origin", n.origin},
                      {"cold", n.cold},
                      {"card_hash", n.card_hash},
                      {"seen_n", n.seen_n},
                      {"tombstone_reason", n.tombstone_reason}};
  if (!n.card_json.empty()) {
    try {
      j["card"] = nlohmann::json::parse(n.card_json);
    } catch (...) {
      j["card_json"] = n.card_json;
    }
  }
  return j;
}

nlohmann::json registry_stats_to_json(const RegistryStats& s) {
  return {{"queries", s.queries},
          {"files", s.files},
          {"pending_inventory", s.pending_inventory},
          {"nodes", s.nodes},
          {"fn", s.fns},
          {"ctrl", s.ctrls},
          {"latch", s.latches},
          {"handoff", s.handoffs},
          {"facts", s.facts},
          {"tombstones", s.tombstones},
          {"embeddings", s.embeddings}};
}

}  // namespace tuide
