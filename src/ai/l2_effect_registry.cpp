#include "ai/l2_effect_registry.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <sqlite3.h>

#include "ai/l2_effect_summary.hpp"
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

std::string registry_card_passage(const RegistryNodeRow& n) {
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
    const std::string passage = registry_card_passage(n);
    if (passage.empty()) {
      continue;
    }
    ++report->considered;
    if (!opts.force) {
      std::string cached;
      if (cached_hash(r->db, n.id, opts.model, &cached) && cached == n.card_hash &&
          !n.card_hash.empty()) {
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
    sqlite3_reset(ins);
    sqlite3_bind_text(ins, 1, jobs[i].n.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 2, opts.model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(ins, 3, static_cast<int>(vecs[i].size()));
    sqlite3_bind_text(ins, 4, jobs[i].n.card_hash.c_str(), -1, SQLITE_TRANSIENT);
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
  if (!embed) {
    set_err(err, "falta embedder");
    return false;
  }
  if (query.empty()) {
    set_err(err, "query vacía");
    return false;
  }
  *out = {};
  std::vector<float> qvec;
  if (!embed(true, query, &qvec) || qvec.empty()) {
    set_err(err, "embed query falló");
    return false;
  }

  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(r->db,
                     "SELECT e.node_id, e.blob, n.kind FROM embeddings e "
                     "JOIN nodes n ON n.id=e.node_id WHERE e.model=?1 AND n.tombstone_reason=''",
                     -1, &st, nullptr);
  sqlite3_bind_text(st, 1, opts.model.c_str(), -1, SQLITE_TRANSIENT);
  auto seed_kind_ok = [&](const std::string& kind) {
    static const char* kDefault[] = {"fn", "latch", "handoff"};
    if (opts.seed_kinds.empty()) {
      for (const char* k : kDefault) {
        if (kind == k) {
          return true;
        }
      }
      return false;
    }
    return std::find(opts.seed_kinds.begin(), opts.seed_kinds.end(), kind) != opts.seed_kinds.end();
  };
  struct Scored {
    std::string id;
    float cos = 0.f;
  };
  std::vector<Scored> scored;
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
    s.cos = cosine_similarity(qvec, v);
    scored.push_back(std::move(s));
  }
  sqlite3_finalize(st);
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
    if (taken.count(s.id) || s.id.rfind("fn:", 0) != 0) {
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
        if (s.id.rfind("fn:", 0) != 0 || stem_of_node_id(s.id) != st) {
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
      if (s.id.rfind("fn:", 0) != 0 || stem_of_node_id(s.id) != stem) {
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
    sqlite3_bind_text(es, 2, opts.model.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(es) == SQLITE_ROW) {
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
  nlohmann::json zones = nlohmann::json::array();
  std::unordered_set<std::string> explained_anchors;
  std::vector<std::unordered_set<std::string>> emitted_zone_nodes;
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
      int score = 0;
    };
    std::vector<RankedFact> ranked_facts;
    for (const auto& fact : facts) {
      if (!zone_ids.count(fact.from) || !zone_ids.count(fact.to)) {
        continue;
      }
      int score = 10;
      if (fact.kind == "write" || fact.kind == "read") {
        score = 100;
      } else if (fact.kind == "handoff") {
        score = 90;
      } else if (fact.kind == "then" || fact.kind == "else" || fact.kind == "case") {
        score = 80;
      } else if (fact.kind == "call") {
        score = 60;
      } else if (fact.kind == "enter_ctrl") {
        score = 50;
      }
      if (anchor_ids.count(fact.from) || anchor_ids.count(fact.to)) {
        score += 70;
      }
      if (direct_ids.count(fact.from) || direct_ids.count(fact.to)) {
        score += 20;
      }
      if (call_degree[fact.from] > 12 || call_degree[fact.to] > 12) {
        score -= 25;
      }
      ranked_facts.push_back({&fact, score});
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
    nlohmann::json edges = nlohmann::json::array();
    for (const auto& ranked : ranked_facts) {
      if (static_cast<int>(edges.size()) >= std::max(0, opts.max_edges)) {
        break;
      }
      const JudgeFact& fact = *ranked.fact;
      auto from = nodes.find(fact.from);
      auto to = nodes.find(fact.to);
      if (from == nodes.end() || to == nodes.end()) {
        continue;
      }
      nlohmann::json edge = {{"from", node_target(from->second->node)},
                             {"kind", fact.kind},
                             {"to", node_target(to->second->node)}};
      if (!fact.member.empty()) {
        edge["member"] = fact.member;
      }
      if (!to->second->node.cond.empty()) {
        edge["cond"] = to->second->node.cond.size() > 100
                           ? to->second->node.cond.substr(0, 100) + "…"
                           : to->second->node.cond;
      }
      edges.push_back(std::move(edge));
    }
    emitted_edges += static_cast<int>(edges.size());

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

    zones.push_back(
        {{"id", macro.id},
         {"rank", zi + 1},
         {"score", macro.score},
         {"score_margin", margin},
         {"mass_coverage", macro.mass_coverage},
         {"primary_stems", macro.primary_stems},
         {"core_stems", std::vector<std::string>(core_stems.begin(), core_stems.end())},
         {"context_stems",
          std::vector<std::string>(context_stems.begin(), context_stems.end())},
         {"merge_witnesses", macro.merge_witnesses},
         {"merge_strength", macro.merge_strength},
         {"why", macro.why},
         {"nuclei", nuclei},
         {"anchors", anchor_groups},
         {"roles", roles},
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
    for (const auto& seed : result.seeds) {
      if (static_cast<int>(zones.size()) >= std::max(0, opts.max_zones)) {
        break;
      }
      if (explained_anchors.count(seed.node.id) || seed.node.kind != "fn") {
        continue;
      }
      const std::string id = "M" + std::to_string(zones.size() + 1);
      nlohmann::json stems = nlohmann::json::array();
      if (!seed.node.stem.empty()) {
        stems.push_back(seed.node.stem);
      }
      zones.push_back(
          {{"id", id},
           {"rank", zones.size() + 1},
           {"score", std::max(0.f, seed.cosine) * 0.5f},
           {"score_margin", 0.f},
           {"mass_coverage", seed.mass},
           {"primary_stems", stems},
           {"why", "uncovered query candidate"},
           {"nuclei", nlohmann::json::array()},
           {"anchors", nlohmann::json::array({nlohmann::json::array({compact_ref(seed)})})},
           {"roles",
            {{"writers", nlohmann::json::array()},
             {"readers", nlohmann::json::array()},
             {"controls", nlohmann::json::array()},
             {"handoffs", nlohmann::json::array()}}},
           {"edges", nlohmann::json::array()},
           {"representatives", nlohmann::json::array({compact_card(seed.node, true)})},
           {"trails", nlohmann::json::array()},
           {"closure", {{"groups", 1}, {"closed", 0}, {"closed_without_hubs", 0}, {"max_hops", 4}}},
           {"hub_nodes", nlohmann::json::array()},
           {"overlap_previous", 0.f},
           {"risks", nlohmann::json::array({"uncovered_candidate", "no_state_nucleus"})},
           {"zone_nodes", 1}});
    }
  }
  *out = {{"schema", "causal_judge_v1"},
          {"query", query},
          {"gate",
           {{"max_cosine", result.max_cosine},
            {"map_boosted", result.map_boosted},
            {"weak", result.weak_gate},
            {"holes", result.holes}}},
          {"zones", zones},
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

RegistryCausalTriageDecision registry_parse_causal_triage_decision(
    const std::string& raw, const std::vector<std::string>& allowed_zone_ids,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets) {
  RegistryCausalTriageDecision out;
  out.raw = raw;
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
    if (item.contains("expand_from") && item["expand_from"].is_array()) {
      for (const auto& value : item["expand_from"]) {
        if (value.is_string()) {
          zone.expand_from.push_back(value.get<std::string>());
        }
      }
    }
    if (!zone.expand_from.empty()) {
      std::string inferred;
      for (const auto& target : zone.expand_from) {
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
        zone.id = inferred;
      }
    }
    if (!allowed.count(zone.id) || zone.need.empty() || zone.need.size() > 160 ||
        zone.expand_from.size() > 4) {
      out.error = "inspect inválido para " + zone.id;
      return out;
    }
    const auto ait = allowed_targets.find(zone.id);
    std::unordered_set<std::string> targets;
    if (ait != allowed_targets.end()) {
      targets.insert(ait->second.begin(), ait->second.end());
    }
    for (const auto& target : zone.expand_from) {
      if (!targets.count(target)) {
        out.error = "target de expansión ajeno a " + zone.id + ": " + target;
        return out;
      }
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

nlohmann::json registry_causal_triage_decision_to_json(
    const RegistryCausalTriageDecision& decision) {
  nlohmann::json zones = nlohmann::json::array();
  for (const auto& zone : decision.zones) {
    zones.push_back({{"id", zone.id},
                     {"verdict", zone.verdict},
                     {"need", zone.need},
                     {"expand_from", zone.expand_from}});
  }
  return {{"action", "causal_zone_triage_v1"},
          {"ok", decision.ok},
          {"zones", zones},
          {"shortlist", decision.shortlist},
          {"retrieval_needed", decision.retrieval_needed},
          {"why", decision.why},
          {"error", decision.error}};
}

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
    out << "causal edges:\n";
    for (const auto& edge : zone.value("edges", nlohmann::json::array())) {
      out << "- " << short_target(edge.value("from", "?")) << " -"
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
    verdict.role = item.value("role", verdict.verdict == "reject" ? "none" : "");
    verdict.completeness =
        item.value("completeness", verdict.verdict == "reject" ? "none" : "");
    verdict.confidence = item.value("confidence", 0.f);
    verdict.why = item.value("why", "");
    verdict.contribution = item.value("contribution", "");
    verdict.missing_link = item.value("missing_link", "");
    if (verdict.why.empty() && !verdict.contribution.empty()) {
      verdict.why = verdict.contribution;
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
        verdict.why.size() > 240 ||
        verdict.why == "evidencia causal concreta") {
      out.error = "confidence/why inválido para " + verdict.id;
      return out;
    }
    if (verdict.verdict == "reject" && verdict.why.size() > 120) {
      out.error = "why de reject demasiado largo para " + verdict.id;
      return out;
    }
    if (item.contains("expand_from") && item["expand_from"].is_array()) {
      for (const auto& value : item["expand_from"]) {
        if (value.is_string() && !value.get<std::string>().empty() &&
            verdict.expand_from.size() < 4) {
          verdict.expand_from.push_back(value.get<std::string>());
        }
      }
    }
    const bool valid_select_role =
        verdict.role == "primary" || verdict.role == "trigger" ||
        verdict.role == "state_owner" || verdict.role == "cleanup" ||
        verdict.role == "consumer" || verdict.role == "boundary";
    if (verdict.verdict == "select") {
      if (!valid_select_role ||
          (verdict.completeness != "complete" && verdict.completeness != "partial") ||
          verdict.contribution.size() < 8 || verdict.contribution.size() > 140) {
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
    } else if (verdict.role != "none" || verdict.completeness != "none" ||
               !verdict.contribution.empty() || !verdict.missing_link.empty() ||
               !verdict.expand_from.empty()) {
      out.error = "reject contiene selección/expansión para " + verdict.id;
      return out;
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
  if (!j.contains("selected") || !j["selected"].is_array()) {
    out.error = "selected ausente";
    return out;
  }
  std::unordered_set<std::string> declared_selected;
  for (const auto& value : j["selected"]) {
    if (!value.is_string() || !declared_selected.insert(value.get<std::string>()).second) {
      out.error = "selected inválido";
      return out;
    }
  }
  const std::unordered_set<std::string> derived_selected(out.selected.begin(), out.selected.end());
  if (declared_selected != derived_selected) {
    out.error = "selected no coincide con verdict=select";
    return out;
  }
  out.next = j.value("next", "");
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
  if ((out.next != "verify" && out.next != "expand" && out.next != "none") ||
      out.why.size() < 12 || out.why.size() > 240 || out.why == "síntesis breve") {
    out.error = "next/why global inválido";
    return out;
  }
  const std::string expected_next =
      out.selected.empty() ? "none" : (partial_n > 0 ? "expand" : "verify");
  if (out.next != expected_next) {
    out.error = "next no coincide con selección";
    return out;
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
