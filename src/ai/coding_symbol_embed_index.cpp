#include "ai/coding_symbol_embed_index.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "ai/ai_trace.hpp"
#include "ai/embedding_backend.hpp"
#include "ai/vector_math.hpp"
#include "indexer/symbol_workspace_indexer.hpp"

namespace fs = std::filesystem;

namespace tuide {
namespace {

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
  return (fs::path(cache_dir) / "embed" / "coding_symbols" / (mid + "-" + content_hash + ".json"))
      .string();
}

const char* kind_to_str(SymbolKind k) {
  switch (k) {
    case SymbolKind::kNamespace:
      return "ns";
    case SymbolKind::kClass:
      return "class";
    case SymbolKind::kStruct:
      return "struct";
    case SymbolKind::kFunction:
      return "fn";
    case SymbolKind::kMethod:
      return "method";
    case SymbolKind::kVariable:
      return "var";
  }
  return "sym";
}

SymbolKind kind_from_str(const std::string& s) {
  if (s == "class") {
    return SymbolKind::kClass;
  }
  if (s == "struct") {
    return SymbolKind::kStruct;
  }
  if (s == "method") {
    return SymbolKind::kMethod;
  }
  if (s == "var") {
    return SymbolKind::kVariable;
  }
  if (s == "ns") {
    return SymbolKind::kNamespace;
  }
  return SymbolKind::kFunction;
}

bool usable_symbol(const IndexedSymbol& s) {
  if (s.file.empty() || s.name.empty()) {
    return false;
  }
  if (s.file.rfind("third_party/", 0) == 0) {
    return false;
  }
  if (s.name.size() < 2) {
    return false;
  }
  return true;
}

}  // namespace

std::string coding_symbol_index_passage(const std::string& file, const std::string& name,
                                        const std::string& signature) {
  std::string out = file;
  if (!name.empty()) {
    out.push_back(' ');
    out += name;
  }
  if (!signature.empty()) {
    out.push_back(' ');
    const std::size_t cap = 160;
    if (signature.size() <= cap) {
      out += signature;
    } else {
      out.append(signature.data(), cap);
    }
  }
  if (out.size() > 800) {
    out.resize(800);
  }
  return out;
}

void CodingSymbolEmbedIndex::set_rows_for_test(std::vector<CodingSymbolEmbedRow> rows) {
  rows_ = std::move(rows);
  ready_ = !rows_.empty();
  content_hash_ = "test";
}

void CodingSymbolEmbedIndex::invalidate() {
  rows_.clear();
  content_hash_.clear();
  ready_ = false;
}

bool CodingSymbolEmbedIndex::embed_query_vec(const std::string& query, EmbeddingBackend* backend,
                                             std::vector<float>* out, std::string* error) const {
  if (out == nullptr || query.empty()) {
    return false;
  }
  if (backend == nullptr || !backend->ready()) {
    if (error) {
      *error = "embedding backend no ready";
    }
    return false;
  }
  return backend->embed_query(query, out, error);
}

std::vector<RepoMapEntry> CodingSymbolEmbedIndex::top_entries(
    const std::vector<float>& query_embedding, std::size_t k) const {
  std::vector<RepoMapEntry> out;
  if (!ready_ || query_embedding.empty() || rows_.empty() || k == 0) {
    return out;
  }
  struct Scored {
    std::size_t idx = 0;
    float cos = 0.0f;
  };
  std::vector<Scored> scored;
  scored.reserve(rows_.size());
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].embedding.empty()) {
      continue;
    }
    scored.push_back({i, cosine_similarity(query_embedding, rows_[i].embedding)});
  }
  const std::size_t take = std::min(k, scored.size());
  if (take == 0) {
    return out;
  }
  std::partial_sort(scored.begin(), scored.begin() + static_cast<std::ptrdiff_t>(take), scored.end(),
                    [](const Scored& a, const Scored& b) { return a.cos > b.cos; });
  out.reserve(take);
  for (std::size_t i = 0; i < take; ++i) {
    const auto& row = rows_[scored[i].idx];
    RepoMapEntry e;
    e.file = row.file;
    e.name = row.name;
    e.kind = row.kind;
    e.line = row.line;
    e.signature = row.signature;
    e.score = static_cast<int>(std::lround(static_cast<double>(scored[i].cos) * 1000000.0));
    out.push_back(std::move(e));
  }
  return out;
}

bool CodingSymbolEmbedIndex::ensure(const SymbolIndexSnapshot* snapshot, EmbeddingBackend* backend,
                                    const std::string& cache_dir, const std::string& model_id,
                                    const ProgressFn& on_progress, std::string* error,
                                    const PercentFn& on_percent) {
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

  if (on_percent) {
    on_percent(0, 0);
  }

  std::vector<CodingSymbolEmbedRow> built;
  built.reserve(snapshot->symbols.size());
  std::ostringstream hash_src;
  hash_src << snapshot->workspace_root << '\n';
  if (on_progress) {
    on_progress("coding symbol embed: preparando corpus (" +
                std::to_string(snapshot->symbols.size()) + " en snapshot)…");
  }
  for (const auto& s : snapshot->symbols) {
    if (!usable_symbol(s)) {
      continue;
    }
    CodingSymbolEmbedRow row;
    row.file = s.file;
    row.name = s.name;
    row.kind = s.kind;
    row.line = s.line;
    row.signature = s.signature;
    row.passage = coding_symbol_index_passage(s.file, s.name, s.signature);
    hash_src << row.file << '\t' << row.line << '\t' << row.name << '\t' << row.signature << '\n';
    built.push_back(std::move(row));
  }
  if (built.empty()) {
    if (error) {
      *error = "sin símbolos utilizables";
    }
    return false;
  }
  if (on_progress) {
    on_progress("coding symbol embed: " + std::to_string(built.size()) +
                " símbolos utilizables");
  }

  const std::string content_hash = fnv1a_hex(hash_src.str());
  if (ready_ && content_hash_ == content_hash && rows_.size() == built.size()) {
    if (on_progress) {
      on_progress("coding symbol embed: ya listo (" + std::to_string(rows_.size()) + ")");
    }
    if (on_percent) {
      on_percent(rows_.size(), rows_.size());
    }
    return true;
  }

  const std::string cache_file = cache_file_for(cache_dir, model_id, content_hash);
  {
    const std::string raw = read_file(cache_file);
    if (!raw.empty()) {
      try {
        const auto doc = nlohmann::json::parse(raw);
        if (doc.value("content_hash", "") == content_hash && doc.contains("rows") &&
            doc["rows"].is_array()) {
          std::vector<CodingSymbolEmbedRow> loaded;
          loaded.reserve(doc["rows"].size());
          for (const auto& j : doc["rows"]) {
            CodingSymbolEmbedRow row;
            row.file = j.value("file", "");
            row.name = j.value("name", "");
            row.kind = kind_from_str(j.value("kind", "fn"));
            row.line = j.value("line", 0);
            row.signature = j.value("signature", "");
            row.passage = j.value("passage", "");
            if (j.contains("embedding") && j["embedding"].is_array()) {
              for (const auto& v : j["embedding"]) {
                row.embedding.push_back(v.get<float>());
              }
            }
            if (!row.file.empty() && !row.name.empty() && !row.embedding.empty()) {
              loaded.push_back(std::move(row));
            }
          }
          if (loaded.size() == built.size()) {
            rows_ = std::move(loaded);
            content_hash_ = content_hash;
            ready_ = true;
            ai_trace(AiTraceChannel::Embed, "coding_symbol_cache_hit",
                     "{\"n\":" + std::to_string(rows_.size()) + ",\"file\":\"" +
                         ai_trace_escape(cache_file) + "\"}");
            if (on_progress) {
              on_progress("coding symbol embed: cache hit (" + std::to_string(rows_.size()) + ")");
            }
            if (on_percent) {
              on_percent(rows_.size(), rows_.size());
            }
            return true;
          }
        }
      } catch (...) {
        // rebuild
      }
    }
  }

  if (on_progress) {
    on_progress("coding symbol embed: indexing " + std::to_string(built.size()) + " símbolos…");
  }
  if (on_percent) {
    on_percent(0, built.size());
  }
  ai_trace(AiTraceChannel::Embed, "coding_symbol_embed_begin",
           "{\"n\":" + std::to_string(built.size()) + "}");

  std::vector<std::string> passages;
  passages.reserve(built.size());
  for (const auto& row : built) {
    passages.push_back(row.passage);
  }
  // Embed in chunks so progress updates regularly (backend also chunks at http_batch).
  constexpr std::size_t kProgressChunk = 256;
  for (std::size_t base = 0; base < passages.size(); base += kProgressChunk) {
    const std::size_t n = std::min(kProgressChunk, passages.size() - base);
    std::vector<std::string> slice(passages.begin() + static_cast<std::ptrdiff_t>(base),
                                   passages.begin() + static_cast<std::ptrdiff_t>(base + n));
    std::vector<std::vector<float>> vecs;
    std::string emb_err;
    if (!backend->embed_passages(slice, &vecs, &emb_err) || vecs.size() != n) {
      if (error) {
        *error = "embed symbols batch falló: " + emb_err;
      }
      ai_trace(AiTraceChannel::Embed, "coding_symbol_embed_fail",
               "{\"err\":\"" + ai_trace_escape(emb_err) + "\"}");
      return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
      built[base + i].embedding = std::move(vecs[i]);
    }
    if (on_progress) {
      const std::size_t done = base + n;
      const int pct = static_cast<int>((done * 100) / passages.size());
      on_progress("coding symbol embed: " + std::to_string(done) + "/" +
                  std::to_string(passages.size()) + " (" + std::to_string(pct) + "%)");
    }
    if (on_percent) {
      on_percent(base + n, passages.size());
    }
  }

  nlohmann::json out = nlohmann::json::object();
  out["model_id"] = model_id;
  out["content_hash"] = content_hash;
  out["rows"] = nlohmann::json::array();
  for (const auto& row : built) {
    out["rows"].push_back({{"file", row.file},
                           {"name", row.name},
                           {"kind", kind_to_str(row.kind)},
                           {"line", row.line},
                           {"signature", row.signature},
                           {"passage", row.passage},
                           {"embedding", row.embedding}});
  }
  if (!write_bytes(cache_file, out.dump())) {
    if (on_progress) {
      on_progress("coding symbol embed: no se pudo escribir cache " + cache_file);
    }
  } else if (on_progress) {
    on_progress("coding symbol embed: cache " + cache_file);
  }

  rows_ = std::move(built);
  content_hash_ = content_hash;
  ready_ = true;
  ai_trace(AiTraceChannel::Embed, "coding_symbol_embed_done",
           "{\"n\":" + std::to_string(rows_.size()) + "}");
  if (on_progress) {
    on_progress("coding symbol embed: listo (" + std::to_string(rows_.size()) + " símbolos)");
  }
  return true;
}

}  // namespace tuide
