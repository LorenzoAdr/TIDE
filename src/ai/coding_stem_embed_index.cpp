#include "ai/coding_stem_embed_index.hpp"

#include <algorithm>
#include <cctype>
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

}  // namespace

std::string build_coding_stem_index_passage(const std::string& stem,
                                            const std::vector<std::string>& paths,
                                            const std::vector<std::string>& names) {
  std::string out = stem;
  // Underscore tokens help embed "ui_wake_policy" ↔ "wake" / "policy".
  {
    std::string tok;
    auto flush = [&] {
      if (tok.size() >= 3) {
        out.push_back(' ');
        out += tok;
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
  const std::size_t np = std::min<std::size_t>(paths.size(), 3);
  for (std::size_t i = 0; i < np; ++i) {
    if (paths[i].empty()) {
      continue;
    }
    out.push_back(' ');
    out += paths[i];
    // Parent dir (src/ui/foo.hpp → ui) as a soft topic tag.
    const auto slash = paths[i].find_last_of('/');
    if (slash != std::string::npos && slash > 0) {
      const auto prev = paths[i].find_last_of('/', slash - 1);
      const std::size_t begin = (prev == std::string::npos) ? 0 : prev + 1;
      if (slash > begin) {
        const std::string parent = paths[i].substr(begin, slash - begin);
        if (parent.size() >= 2 && parent != "src" && parent != "include") {
          out.push_back(' ');
          out += parent;
        }
      }
    }
  }
  const std::size_t nn = std::min<std::size_t>(names.size(), 24);
  for (std::size_t i = 0; i < nn; ++i) {
    if (names[i].empty()) {
      continue;
    }
    out.push_back(' ');
    out += names[i];
  }
  if (out.size() > 480) {
    out.resize(480);
  }
  return out;
}

void CodingStemEmbedIndex::set_rows_unlocked(std::vector<CodingStemEmbedRow> rows,
                                              const std::string& content_hash) {
  rows_ = std::move(rows);
  by_stem_.clear();
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    by_stem_[rows_[i].stem] = i;
  }
  ready_ = !rows_.empty();
  content_hash_ = content_hash;
}

void CodingStemEmbedIndex::set_rows_for_test(std::vector<CodingStemEmbedRow> rows) {
  std::lock_guard<std::mutex> lock(mu_);
  set_rows_unlocked(std::move(rows), "test");
}

void CodingStemEmbedIndex::invalidate() {
  std::lock_guard<std::mutex> lock(mu_);
  rows_.clear();
  by_stem_.clear();
  content_hash_.clear();
  ready_ = false;
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

bool CodingStemEmbedIndex::ensure(const SymbolIndexSnapshot* snapshot, EmbeddingBackend* backend,
                                  const std::string& cache_dir, const std::string& model_id,
                                  const ProgressFn& on_progress, std::string* error) {
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

  struct Acc {
    std::vector<std::string> paths;
    std::vector<std::string> names;
  };
  std::unordered_map<std::string, Acc> by_stem;
  for (const auto& sym : snapshot->symbols) {
    const std::string stem = path_stem(sym.file);
    if (stem.empty()) {
      continue;
    }
    auto& acc = by_stem[stem];
    if (acc.paths.size() < 3) {
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
    std::string nm = !sym.name.empty() ? sym.name : sym.display_name;
    const auto colon = nm.rfind("::");
    if (colon != std::string::npos) {
      nm = nm.substr(colon + 2);
    }
    if (!nm.empty() && acc.names.size() < 24) {
      bool seen = false;
      for (const auto& n : acc.names) {
        if (n == nm) {
          seen = true;
          break;
        }
      }
      if (!seen) {
        acc.names.push_back(nm);
      }
    }
  }

  std::vector<CodingStemEmbedRow> built;
  built.reserve(by_stem.size());
  std::ostringstream hash_src;
  hash_src << snapshot->workspace_root << '\n';
  for (const auto& kv : by_stem) {
    CodingStemEmbedRow row;
    row.stem = kv.first;
    row.passage = build_coding_stem_index_passage(kv.first, kv.second.paths, kv.second.names);
    hash_src << row.stem << '\n' << row.passage << '\n';
    built.push_back(std::move(row));
  }
  std::sort(built.begin(), built.end(),
            [](const CodingStemEmbedRow& a, const CodingStemEmbedRow& b) { return a.stem < b.stem; });
  // Cap pathological workspaces.
  constexpr std::size_t kMaxStems = 800;
  if (built.size() > kMaxStems) {
    built.resize(kMaxStems);
  }

  const std::string content_hash = fnv1a_hex(hash_src.str());
  if (ready_ && content_hash_ == content_hash && !rows_.empty()) {
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
          set_rows_unlocked(std::move(loaded), content_hash);
          ai_trace(AiTraceChannel::Embed, "coding_stem_cache_hit",
                   "{\"n\":" + std::to_string(rows_.size()) + ",\"file\":\"" +
                       ai_trace_escape(cache_file) + "\"}");
          if (on_progress) {
            on_progress("coding stem embed: cache hit (" + std::to_string(rows_.size()) + ")");
          }
          return true;
        }
      }
    } catch (...) {
      // rebuild
    }
  }

  if (on_progress) {
    on_progress("coding stem embed: indexing " + std::to_string(built.size()) + " stems…");
  }
  {
    // Chunk so the UI gets N/M progress; pause briefly if L1 is using the embed server.
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
  out["content_hash"] = content_hash;
  out["rows"] = nlohmann::json::array();
  for (const auto& row : built) {
    out["rows"].push_back({{"stem", row.stem},
                           {"passage", row.passage},
                           {"embedding", row.embedding}});
  }
  if (!write_bytes(cache_file, out.dump())) {
    if (on_progress) {
      on_progress("coding stem embed: no se pudo escribir cache " + cache_file);
    }
  } else if (on_progress) {
    on_progress("coding stem embed: cache " + cache_file);
  }

  set_rows_unlocked(std::move(built), content_hash);
  return true;
}

}  // namespace tuide
