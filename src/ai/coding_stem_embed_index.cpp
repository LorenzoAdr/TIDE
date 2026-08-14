#include "ai/coding_stem_embed_index.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "ai/ai_trace.hpp"
#include "ai/embedding_backend.hpp"
#include "ai/vector_math.hpp"
#include "indexer/symbol_workspace_indexer.hpp"

namespace fs = std::filesystem;

namespace tuide {
namespace {

struct PassageOpts {
  std::size_t max_paths = 3;
  std::size_t max_names = 24;
  std::size_t cap = 480;
  bool type_first = false;
  bool label_types = false;
  bool include_sigs = false;
  std::size_t max_sig_chars = 60;
  std::size_t max_sigs = 6;
  bool include_hdr_docs = false;
  std::size_t max_docs = 3;
  std::size_t max_doc_chars = 100;
  bool include_module_blurb = false;
  std::size_t max_blurb_chars = 120;
  bool prefer_hpp_paths = false;
};

PassageOpts opts_for(StemPassageProfileId id) {
  PassageOpts o;
  switch (id) {
    case StemPassageProfileId::Baseline:
      break;
    case StemPassageProfileId::TypeFirst:
      o.type_first = true;
      o.label_types = true;
      break;
    case StemPassageProfileId::SigSnip:
      o.type_first = true;
      o.label_types = true;
      o.include_sigs = true;
      o.prefer_hpp_paths = true;
      break;
    case StemPassageProfileId::HdrDoc:
      o.type_first = true;
      o.label_types = true;
      o.include_hdr_docs = true;
      o.prefer_hpp_paths = true;
      break;
    case StemPassageProfileId::ModuleBlurb:
      o.type_first = true;
      o.label_types = true;
      o.include_module_blurb = true;
      o.prefer_hpp_paths = true;
      break;
    case StemPassageProfileId::Rich480:
      o.type_first = true;
      o.label_types = true;
      o.include_sigs = true;
      o.include_hdr_docs = true;
      o.include_module_blurb = true;
      o.prefer_hpp_paths = true;
      o.cap = 480;
      break;
    case StemPassageProfileId::Rich720:
      o.type_first = true;
      o.label_types = true;
      o.include_sigs = true;
      o.include_hdr_docs = true;
      o.include_module_blurb = true;
      o.prefer_hpp_paths = true;
      o.cap = 720;
      o.max_names = 32;
      o.max_sigs = 8;
      o.max_docs = 4;
      break;
    case StemPassageProfileId::KitchenSink:
      o.type_first = true;
      o.label_types = true;
      o.include_sigs = true;
      o.include_hdr_docs = true;
      o.include_module_blurb = true;
      o.prefer_hpp_paths = true;
      o.cap = 960;
      o.max_names = 48;
      o.max_sigs = 12;
      o.max_docs = 6;
      o.max_doc_chars = 140;
      o.max_blurb_chars = 200;
      o.max_sig_chars = 80;
      break;
  }
  return o;
}

std::string path_stem(const std::string& file) {
  if (file.empty()) {
    return {};
  }
  std::string base = file;
  const auto slash = base.find_last_of("/\\");
  if (slash != std::string::npos) {
    base = base.substr(slash + 1);
  }
  const auto dot = base.find_last_of('.');
  if (dot != std::string::npos && dot > 0) {
    base = base.substr(0, dot);
  }
  return base;
}

bool is_header_path(const std::string& path) {
  const auto dot = path.find_last_of('.');
  if (dot == std::string::npos) {
    return false;
  }
  std::string ext = path.substr(dot);
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return ext == ".hpp" || ext == ".h" || ext == ".hh" || ext == ".hxx";
}

bool is_type_kind(SymbolKind kind) {
  return kind == SymbolKind::kClass || kind == SymbolKind::kStruct;
}

int kind_rank(SymbolKind kind) {
  if (is_type_kind(kind)) {
    return 0;
  }
  if (kind == SymbolKind::kFunction || kind == SymbolKind::kMethod) {
    return 1;
  }
  if (kind == SymbolKind::kNamespace) {
    return 2;
  }
  return 3;
}

std::string bare_name(std::string nm) {
  const auto colon = nm.rfind("::");
  if (colon != std::string::npos) {
    nm = nm.substr(colon + 2);
  }
  return nm;
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
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

bool write_bytes(const std::string& path, const std::string& body) {
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    return false;
  }
  out << body;
  return static_cast<bool>(out);
}

std::string cache_file_for(const std::string& cache_dir, const std::string& model_id,
                           const std::string& content_hash) {
  std::string mid = model_id.empty() ? "default" : model_id;
  for (char& c : mid) {
    if (c == '/' || c == '\\' || c == ' ') {
      c = '_';
    }
  }
  return (fs::path(cache_dir) / "embed" / "coding_stems" / (mid + "-" + content_hash + ".json")).string();
}

void append_stem_tokens(std::string* out, const std::string& stem) {
  std::string tok;
  auto flush = [&] {
    if (tok.size() >= 3) {
      out->push_back(' ');
      *out += tok;
    }
    tok.clear();
  };
  for (char ch : stem) {
    if (ch == '_' || ch == '-') {
      flush();
    } else {
      tok.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
  }
  flush();
}

void append_path_bits(std::string* out, const std::string& path) {
  if (path.empty()) {
    return;
  }
  out->push_back(' ');
  *out += path;
  const auto slash = path.find_last_of('/');
  if (slash != std::string::npos && slash > 0) {
    const auto prev = path.find_last_of('/', slash - 1);
    const std::size_t begin = (prev == std::string::npos) ? 0 : prev + 1;
    if (slash > begin) {
      const std::string parent = path.substr(begin, slash - begin);
      if (parent.size() >= 2 && parent != "src" && parent != "include") {
        out->push_back(' ');
        *out += parent;
      }
    }
  }
}

std::string trim_ws(std::string s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
    s.erase(s.begin());
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
    s.pop_back();
  }
  return s;
}

std::string truncate_at(std::string s, std::size_t max_n) {
  if (s.size() <= max_n) {
    return s;
  }
  s.resize(max_n);
  // Do not split UTF-8 codepoints (JSON dump + embedder are strict).
  while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) {
    s.pop_back();
  }
  if (!s.empty()) {
    const unsigned char c = static_cast<unsigned char>(s.back());
    if ((c & 0xE0) == 0xC0 || (c & 0xF0) == 0xE0 || (c & 0xF8) == 0xF0) {
      s.pop_back();
    }
  }
  return s;
}

bool looks_like_doc_comment(const std::string& line) {
  std::size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
    ++i;
  }
  if (i >= line.size()) {
    return false;
  }
  if (line.compare(i, 3, "///") == 0 || line.compare(i, 3, "//!") == 0) {
    return true;
  }
  if (line.compare(i, 2, "//") == 0) {
    return true;
  }
  if (line.compare(i, 2, "/*") == 0 || line.compare(i, 1, "*") == 0) {
    return true;
  }
  return false;
}

struct FileLines {
  std::vector<std::string> lines;  // 1-based; [0] unused
  bool loaded = false;
};

FileLines& load_lines(std::unordered_map<std::string, FileLines>* cache, const std::string& abs) {
  auto& slot = (*cache)[abs];
  if (slot.loaded) {
    return slot;
  }
  slot.loaded = true;
  std::ifstream in(abs);
  if (!in) {
    return slot;
  }
  slot.lines.emplace_back();
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    slot.lines.push_back(std::move(line));
  }
  return slot;
}

std::string extract_doc_near(const FileLines& fl, int line, std::size_t max_chars) {
  if (line <= 1 || fl.lines.size() <= 1) {
    return {};
  }
  const int start = std::max(1, line - 4);
  for (int i = line - 1; i >= start; --i) {
    if (i <= 0 || static_cast<std::size_t>(i) >= fl.lines.size()) {
      continue;
    }
    const std::string& L = fl.lines[static_cast<std::size_t>(i)];
    if (L.find_first_not_of(" \t") == std::string::npos) {
      continue;
    }
    if (looks_like_doc_comment(L)) {
      std::string t = trim_ws(L);
      // Strip comment markers for denser embedding signal.
      while (!t.empty() && (t[0] == '/' || t[0] == '*' || t[0] == '!' || t[0] == ' ')) {
        t.erase(t.begin());
      }
      return truncate_at(trim_ws(t), max_chars);
    }
    break;
  }
  return {};
}

std::string extract_module_blurb(const FileLines& fl, std::size_t max_chars) {
  if (fl.lines.size() <= 1) {
    return {};
  }
  std::ostringstream acc;
  std::size_t used = 0;
  bool in_block = false;
  for (std::size_t i = 1; i < fl.lines.size() && i <= 40; ++i) {
    const std::string& L = fl.lines[i];
    const auto first = L.find_first_not_of(" \t");
    if (first == std::string::npos) {
      if (used > 0) {
        break;
      }
      continue;
    }
    const std::string body = L.substr(first);
    if (!in_block && body.rfind("#", 0) == 0) {
      // Skip include guards / pragmas at top.
      continue;
    }
    if (!in_block && body.rfind("/*", 0) == 0) {
      in_block = true;
    }
    if (in_block || body.rfind("//", 0) == 0 || body.rfind("*", 0) == 0) {
      std::string t = body;
      while (!t.empty() && (t[0] == '/' || t[0] == '*' || t[0] == '!' || t[0] == ' ')) {
        t.erase(t.begin());
      }
      t = trim_ws(t);
      if (!t.empty() && t != "/") {
        if (used) {
          acc << ' ';
        }
        acc << t;
        used = acc.str().size();
        if (used >= max_chars) {
          break;
        }
      }
      if (in_block && body.find("*/") != std::string::npos) {
        break;
      }
      continue;
    }
    // Hit real code.
    break;
  }
  return truncate_at(acc.str(), max_chars);
}

std::string abs_for(const std::string& root, const std::string& rel) {
  if (rel.empty()) {
    return {};
  }
  if (!root.empty() && rel[0] != '/') {
    return (fs::path(root) / rel).string();
  }
  return rel;
}

std::string pick_header_path(const std::vector<std::string>& paths) {
  for (const auto& p : paths) {
    if (is_header_path(p)) {
      return p;
    }
  }
  return paths.empty() ? std::string{} : paths.front();
}

void append_unique_token(std::string* out, std::unordered_set<std::string>* seen,
                         const std::string& tok) {
  if (tok.empty() || seen->count(tok)) {
    return;
  }
  seen->insert(tok);
  out->push_back(' ');
  *out += tok;
}

}  // namespace

StemPassageProfileId parse_stem_passage_profile(const std::string& name) {
  std::string n = name;
  for (char& c : n) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (n == "baseline" || n.empty()) {
    return StemPassageProfileId::Baseline;
  }
  if (n == "type_first" || n == "type-first") {
    return StemPassageProfileId::TypeFirst;
  }
  if (n == "sig_snip" || n == "sig-snip") {
    return StemPassageProfileId::SigSnip;
  }
  if (n == "hdr_doc" || n == "hdr-doc") {
    return StemPassageProfileId::HdrDoc;
  }
  if (n == "module_blurb" || n == "module-blurb") {
    return StemPassageProfileId::ModuleBlurb;
  }
  if (n == "rich_480" || n == "rich-480") {
    return StemPassageProfileId::Rich480;
  }
  if (n == "rich_720" || n == "rich-720") {
    return StemPassageProfileId::Rich720;
  }
  if (n == "kitchen_sink" || n == "kitchen-sink") {
    return StemPassageProfileId::KitchenSink;
  }
  return StemPassageProfileId::Baseline;
}

const char* stem_passage_profile_name(StemPassageProfileId id) {
  switch (id) {
    case StemPassageProfileId::Baseline:
      return "baseline";
    case StemPassageProfileId::TypeFirst:
      return "type_first";
    case StemPassageProfileId::SigSnip:
      return "sig_snip";
    case StemPassageProfileId::HdrDoc:
      return "hdr_doc";
    case StemPassageProfileId::ModuleBlurb:
      return "module_blurb";
    case StemPassageProfileId::Rich480:
      return "rich_480";
    case StemPassageProfileId::Rich720:
      return "rich_720";
    case StemPassageProfileId::KitchenSink:
      return "kitchen_sink";
  }
  return "baseline";
}

StemPassageProfileId default_stem_passage_profile() {
  // Battery 2026-08-15 (pure cosine top_k): baseline best MRR/Hit@3; richer
  // passages diluted identity signal. See tests/fixtures/stem_embed_battery/RESULTS.md.
  return StemPassageProfileId::Baseline;
}

std::string build_coding_stem_index_passage(const std::string& stem,
                                            const std::vector<std::string>& paths,
                                            const std::vector<std::string>& names) {
  StemPassageBuildInput in;
  in.stem = stem;
  in.paths = paths;
  for (const auto& n : names) {
    StemPassageSymbol s;
    s.name = n;
    in.symbols.push_back(std::move(s));
  }
  return build_coding_stem_index_passage(in, StemPassageProfileId::Baseline);
}

std::string build_coding_stem_index_passage(const StemPassageBuildInput& in,
                                            StemPassageProfileId profile) {
  const PassageOpts opt = opts_for(profile);
  std::string out = in.stem;
  append_stem_tokens(&out, in.stem);

  std::vector<std::string> paths = in.paths;
  if (opt.prefer_hpp_paths) {
    std::stable_sort(paths.begin(), paths.end(), [](const std::string& a, const std::string& b) {
      const bool ah = is_header_path(a);
      const bool bh = is_header_path(b);
      if (ah != bh) {
        return ah;
      }
      return a < b;
    });
  }
  const std::size_t np = std::min(paths.size(), opt.max_paths);
  for (std::size_t i = 0; i < np; ++i) {
    append_path_bits(&out, paths[i]);
  }

  std::vector<StemPassageSymbol> syms = in.symbols;
  if (opt.type_first) {
    std::stable_sort(syms.begin(), syms.end(), [](const StemPassageSymbol& a,
                                                  const StemPassageSymbol& b) {
      const int ra = kind_rank(a.kind);
      const int rb = kind_rank(b.kind);
      if (ra != rb) {
        return ra < rb;
      }
      const bool ah = is_header_path(a.file);
      const bool bh = is_header_path(b.file);
      if (ah != bh) {
        return ah;
      }
      return a.name < b.name;
    });
  }

  std::unordered_set<std::string> seen_names;
  std::size_t name_count = 0;
  for (const auto& s : syms) {
    if (name_count >= opt.max_names) {
      break;
    }
    const std::string nm = bare_name(s.name);
    if (nm.empty() || seen_names.count(nm)) {
      continue;
    }
    seen_names.insert(nm);
    ++name_count;
    out.push_back(' ');
    if (opt.label_types && is_type_kind(s.kind)) {
      out += "class:";
    }
    out += nm;
  }

  std::unordered_map<std::string, FileLines> file_cache;
  std::unordered_set<std::string> seen_extra;

  if (opt.include_module_blurb) {
    const std::string hdr = pick_header_path(paths);
    if (!hdr.empty()) {
      const auto& fl = load_lines(&file_cache, abs_for(in.workspace_root, hdr));
      const std::string blurb = extract_module_blurb(fl, opt.max_blurb_chars);
      if (!blurb.empty()) {
        append_unique_token(&out, &seen_extra, blurb);
      }
    }
  }

  if (opt.include_hdr_docs) {
    std::size_t docs = 0;
    for (const auto& s : syms) {
      if (docs >= opt.max_docs) {
        break;
      }
      if (!is_type_kind(s.kind) && !(s.kind == SymbolKind::kFunction || s.kind == SymbolKind::kMethod)) {
        continue;
      }
      // Prefer twin header docs.
      if (!s.file.empty() && !is_header_path(s.file) && !paths.empty()) {
        // still allow cpp docs if no header symbol
      }
      const std::string prefer =
          (!s.file.empty() && is_header_path(s.file)) ? s.file : pick_header_path(paths);
      if (prefer.empty() || s.line <= 0) {
        continue;
      }
      // If symbol is on cpp but we have a header path, try header only when same stem.
      std::string use_file = s.file;
      int use_line = s.line;
      if (!is_header_path(use_file) && is_header_path(prefer) && path_stem(prefer) == path_stem(use_file)) {
        // Keep cpp line docs; twin API comments usually sit on header decls — scan header
        // symbols separately via their own entries. For cpp-only, use cpp docs.
      }
      if (!is_header_path(use_file) && prefer != use_file) {
        // Skip non-header symbols when a header exists; header symbols carry the docs.
        bool has_header_sym = false;
        for (const auto& h : syms) {
          if (is_header_path(h.file) && bare_name(h.name) == bare_name(s.name)) {
            has_header_sym = true;
            break;
          }
        }
        if (has_header_sym || is_header_path(prefer)) {
          if (!is_header_path(use_file)) {
            continue;
          }
        }
      }
      const auto& fl = load_lines(&file_cache, abs_for(in.workspace_root, use_file));
      const std::string doc = extract_doc_near(fl, use_line, opt.max_doc_chars);
      if (!doc.empty()) {
        append_unique_token(&out, &seen_extra, doc);
        ++docs;
      }
    }
  }

  if (opt.include_sigs) {
    std::size_t sigs = 0;
    for (const auto& s : syms) {
      if (sigs >= opt.max_sigs) {
        break;
      }
      if (s.signature.empty()) {
        continue;
      }
      if (!(is_type_kind(s.kind) || s.kind == SymbolKind::kFunction || s.kind == SymbolKind::kMethod)) {
        continue;
      }
      // Prefer signatures from headers when available.
      if (opt.prefer_hpp_paths && !s.file.empty() && !is_header_path(s.file)) {
        bool has_hdr = false;
        for (const auto& p : paths) {
          if (is_header_path(p)) {
            has_hdr = true;
            break;
          }
        }
        if (has_hdr && !is_type_kind(s.kind)) {
          continue;
        }
      }
      std::string sig = trim_ws(s.signature);
      sig = truncate_at(sig, opt.max_sig_chars);
      append_unique_token(&out, &seen_extra, sig);
      ++sigs;
    }
  }

  if (out.size() > opt.cap) {
    out = truncate_at(std::move(out), opt.cap);
  }
  return out;
}

void CodingStemEmbedIndex::set_rows_unlocked(std::vector<CodingStemEmbedRow> rows,
                                             const std::string& content_hash,
                                             StemPassageProfileId profile) {
  rows_ = std::move(rows);
  by_stem_.clear();
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    by_stem_[rows_[i].stem] = i;
  }
  ready_ = !rows_.empty();
  content_hash_ = content_hash;
  profile_ = profile;
}

void CodingStemEmbedIndex::set_rows_for_test(std::vector<CodingStemEmbedRow> rows) {
  std::lock_guard<std::mutex> lock(mu_);
  set_rows_unlocked(std::move(rows), "test", StemPassageProfileId::Baseline);
}

void CodingStemEmbedIndex::invalidate() {
  std::lock_guard<std::mutex> lock(mu_);
  rows_.clear();
  by_stem_.clear();
  content_hash_.clear();
  ready_ = false;
  profile_ = StemPassageProfileId::Baseline;
}

bool CodingStemEmbedIndex::embed_query_vec(const std::string& query, EmbeddingBackend* backend,
                                           const EmbedFn& test_embed, std::vector<float>* out,
                                           std::string* error) const {
  if (out == nullptr || query.empty()) {
    return false;
  }
  if (test_embed) {
    return test_embed(true, query, out);
  }
  if (backend == nullptr || !backend->ready()) {
    if (error) {
      *error = "embedding backend no ready";
    }
    return false;
  }
  return backend->embed_query(query, out, error);
}

float CodingStemEmbedIndex::cosine_for_stem(const std::vector<float>& query_embedding,
                                            const std::string& stem) const {
  auto it = by_stem_.find(stem);
  if (it == by_stem_.end()) {
    return 0.0f;
  }
  const auto& emb = rows_[it->second].embedding;
  if (emb.empty() || query_embedding.empty()) {
    return 0.0f;
  }
  return cosine_similarity(query_embedding, emb);
}

const std::string* CodingStemEmbedIndex::passage_for(const std::string& stem) const {
  auto it = by_stem_.find(stem);
  if (it == by_stem_.end()) {
    return nullptr;
  }
  return &rows_[it->second].passage;
}

std::vector<std::pair<std::string, float>> CodingStemEmbedIndex::top_k(
    const std::vector<float>& query_embedding, std::size_t k) const {
  std::vector<std::pair<std::string, float>> scored;
  if (query_embedding.empty() || rows_.empty() || k == 0) {
    return scored;
  }
  scored.reserve(rows_.size());
  for (const auto& row : rows_) {
    if (row.embedding.empty()) {
      continue;
    }
    scored.emplace_back(row.stem, cosine_similarity(query_embedding, row.embedding));
  }
  std::partial_sort(scored.begin(), scored.begin() + std::min(k, scored.size()), scored.end(),
                    [](const auto& a, const auto& b) { return a.second > b.second; });
  if (scored.size() > k) {
    scored.resize(k);
  }
  return scored;
}

std::vector<CodingStemEmbedRow> CodingStemEmbedIndex::build_passages(
    const SymbolIndexSnapshot* snapshot, StemPassageProfileId profile) {
  std::vector<CodingStemEmbedRow> built;
  if (snapshot == nullptr || snapshot->symbols.empty()) {
    return built;
  }

  struct Acc {
    std::vector<std::string> paths;
    std::vector<StemPassageSymbol> symbols;
  };
  std::unordered_map<std::string, Acc> by_stem;
  for (const auto& sym : snapshot->symbols) {
    const std::string stem = path_stem(sym.file);
    if (stem.empty()) {
      continue;
    }
    auto& acc = by_stem[stem];
    if (acc.paths.size() < 8) {
      bool seen = false;
      for (const auto& p : acc.paths) {
        if (p == sym.file) {
          seen = true;
          break;
        }
      }
      if (!seen) {
        acc.paths.push_back(sym.file);
      }
    }
    StemPassageSymbol ps;
    ps.name = bare_name(!sym.name.empty() ? sym.name : sym.display_name);
    ps.kind = sym.kind;
    ps.signature = sym.signature;
    ps.line = sym.line;
    ps.file = sym.file;
    if (ps.name.empty()) {
      continue;
    }
    bool seen = false;
    for (const auto& n : acc.symbols) {
      if (n.name == ps.name && n.file == ps.file) {
        seen = true;
        break;
      }
    }
    if (!seen && acc.symbols.size() < 64) {
      acc.symbols.push_back(std::move(ps));
    }
  }

  built.reserve(by_stem.size());
  for (const auto& kv : by_stem) {
    StemPassageBuildInput in;
    in.stem = kv.first;
    in.paths = kv.second.paths;
    in.symbols = kv.second.symbols;
    in.workspace_root = snapshot->workspace_root;
    CodingStemEmbedRow row;
    row.stem = kv.first;
    row.passage = build_coding_stem_index_passage(in, profile);
    built.push_back(std::move(row));
  }
  std::sort(built.begin(), built.end(),
            [](const CodingStemEmbedRow& a, const CodingStemEmbedRow& b) { return a.stem < b.stem; });
  constexpr std::size_t kMaxStems = 800;
  if (built.size() > kMaxStems) {
    built.resize(kMaxStems);
  }
  return built;
}

bool CodingStemEmbedIndex::ensure(const SymbolIndexSnapshot* snapshot, EmbeddingBackend* backend,
                                  const std::string& cache_dir, const std::string& model_id,
                                  const ProgressFn& on_progress, std::string* error,
                                  StemPassageProfileId profile) {
  std::lock_guard<std::mutex> lock(mu_);
  if (snapshot == nullptr || snapshot->symbols.empty()) {
    if (error) {
      *error = "snapshot vacío";
    }
    return false;
  }
  if (backend == nullptr || !backend->ready()) {
    if (error) {
      *error = "embedding backend no ready";
    }
    return false;
  }

  std::vector<CodingStemEmbedRow> built = build_passages(snapshot, profile);
  if (built.empty()) {
    if (error) {
      *error = "ningún stem construido";
    }
    return false;
  }

  std::ostringstream hash_src;
  hash_src << "profile:" << stem_passage_profile_name(profile) << '\n';
  hash_src << snapshot->workspace_root << '\n';
  for (const auto& row : built) {
    hash_src << row.stem << '\n' << row.passage << '\n';
  }

  const std::string content_hash = fnv1a_hex(hash_src.str());
  if (ready_ && content_hash_ == content_hash && profile_ == profile && !rows_.empty()) {
    return true;
  }

  const std::string cache_file = cache_file_for(cache_dir, model_id, content_hash);
  const std::string cached = read_file(cache_file);
  if (!cached.empty()) {
    try {
      const auto doc = nlohmann::json::parse(cached);
      if (doc.contains("rows") && doc["rows"].is_array()) {
        std::vector<CodingStemEmbedRow> loaded;
        for (const auto& j : doc["rows"]) {
          CodingStemEmbedRow row;
          row.stem = j.value("stem", "");
          row.passage = j.value("passage", "");
          if (j.contains("embedding") && j["embedding"].is_array()) {
            for (const auto& v : j["embedding"]) {
              row.embedding.push_back(v.get<float>());
            }
          }
          if (!row.stem.empty() && !row.embedding.empty()) {
            loaded.push_back(std::move(row));
          }
        }
        if (loaded.size() == built.size()) {
          set_rows_unlocked(std::move(loaded), content_hash, profile);
          ai_trace(AiTraceChannel::Embed, "coding_stem_cache_hit",
                   "{\"n\":" + std::to_string(rows_.size()) + ",\"profile\":\"" +
                       stem_passage_profile_name(profile) + "\",\"file\":\"" +
                       ai_trace_escape(cache_file) + "\"}");
          if (on_progress) {
            on_progress("coding stem embed: cache hit (" + std::to_string(rows_.size()) + " " +
                        stem_passage_profile_name(profile) + ")");
          }
          return true;
        }
      }
    } catch (...) {
      // rebuild
    }
  }

  if (on_progress) {
    on_progress("coding stem embed: indexing " + std::to_string(built.size()) + " stems [" +
                stem_passage_profile_name(profile) + "]…");
  }
  {
    constexpr std::size_t kChunk = 16;
    for (std::size_t base = 0; base < built.size(); base += kChunk) {
      const std::size_t n = std::min(kChunk, built.size() - base);
      std::vector<std::string> passages;
      passages.reserve(n);
      for (std::size_t i = 0; i < n; ++i) {
        passages.push_back(built[base + i].passage);
      }
      std::vector<std::vector<float>> vecs;
      std::string emb_err;
      if (!backend->embed_passages(passages, &vecs, &emb_err) || vecs.size() != n) {
        if (error) {
          *error = "embed stems batch falló: " + emb_err + " @" + std::to_string(base) + "/" +
                   std::to_string(built.size());
        }
        return false;
      }
      for (std::size_t i = 0; i < n; ++i) {
        built[base + i].embedding = std::move(vecs[i]);
      }
      if (on_progress) {
        on_progress("coding stem embed: " + std::to_string(base + n) + "/" +
                    std::to_string(built.size()));
      }
    }
  }

  nlohmann::json out = nlohmann::json::object();
  out["model_id"] = model_id;
  out["profile"] = stem_passage_profile_name(profile);
  out["content_hash"] = content_hash;
  out["rows"] = nlohmann::json::array();
  for (const auto& row : built) {
    out["rows"].push_back(
        {{"stem", row.stem}, {"passage", row.passage}, {"embedding", row.embedding}});
  }
  if (!write_bytes(cache_file, out.dump())) {
    if (on_progress) {
      on_progress("coding stem embed: no se pudo escribir cache " + cache_file);
    }
  } else if (on_progress) {
    on_progress("coding stem embed: cache " + cache_file);
  }

  set_rows_unlocked(std::move(built), content_hash, profile);
  return true;
}

}  // namespace tuide
