#include "ai/level0_intent_index.hpp"

#include "ai/ai_trace.hpp"
#include "ai/vector_math.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace tuide {
namespace {

std::string fnv1a_hex(const std::string& s) {
  std::uint64_t h = 14695981039346656037ull;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ull;
  }
  std::ostringstream oss;
  oss << std::hex << h;
  return oss.str();
}

std::string read_file(const std::string& path) {
  std::ifstream ifs(path);
  if (!ifs) {
    return {};
  }
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

bool write_bytes(const std::string& path, const std::string& data) {
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  std::ofstream ofs(path, std::ios::binary);
  if (!ofs) {
    return false;
  }
  ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(ofs);
}

std::string cache_path_for(const std::string& cache_dir, const std::string& model_id,
                           const std::string& catalog_hash) {
  return (fs::path(cache_dir) / "embed" / "intent" /
          ("intent_index_" + model_id + "_" + catalog_hash + ".json"))
      .string();
}

}  // namespace

std::string Level0IntentIndex::default_catalog_json() {
  return
#include "ai/level0_intent_catalog.inc"
      ;
}

std::string Level0IntentIndex::resolve_default_catalog_path() {
  if (const char* env = std::getenv("TUIDE_LEVEL0_INTENTS_PATH");
      env != nullptr && env[0] != '\0') {
    return env;
  }
#ifdef TUIDE_LEVEL0_INTENTS_PATH
  return TUIDE_LEVEL0_INTENTS_PATH;
#else
  return "data/ai/level0_intents.json";
#endif
}

bool Level0IntentIndex::load_catalog(const std::string& path, std::string* error) {
  std::string json_text;
  catalog_path_ = path;
  if (!path.empty()) {
    json_text = read_file(path);
  }
  if (json_text.empty()) {
    const std::string fallback = resolve_default_catalog_path();
    json_text = read_file(fallback);
    if (!json_text.empty()) {
      catalog_path_ = fallback;
    }
  }
  if (json_text.empty()) {
    json_text = default_catalog_json();
    catalog_path_ = "(embedded)";
  }
  catalog_hash_ = fnv1a_hex(json_text);

  try {
    const auto doc = nlohmann::json::parse(json_text);
    if (!doc.contains("intents") || !doc["intents"].is_array()) {
      if (error) {
        *error = "catálogo sin intents[]";
      }
      return false;
    }
    examples_.clear();
    for (const auto& intent : doc["intents"]) {
      Level0IntentExample base;
      base.intent_id = intent.value("id", "");
      const std::string kind = intent.value("kind", "tool");
      base.is_task = (kind == "task");
      base.name = intent.value("name", "");
      base.arg_policy = intent.value("arg_policy", "none");
      if (base.name.empty()) {
        continue;
      }
      if (!intent.contains("examples") || !intent["examples"].is_array()) {
        continue;
      }
      for (const auto& ex : intent["examples"]) {
        if (!ex.is_string()) {
          continue;
        }
        Level0IntentExample row = base;
        row.example = ex.get<std::string>();
        if (!row.example.empty()) {
          examples_.push_back(std::move(row));
        }
      }
    }
    if (examples_.empty()) {
      if (error) {
        *error = "catálogo de intents vacío";
      }
      return false;
    }
    ready_ = false;
    return true;
  } catch (const std::exception& ex) {
    if (error) {
      *error = std::string("parse catálogo: ") + ex.what();
    }
    return false;
  }
}

void Level0IntentIndex::set_examples_for_test(std::vector<Level0IntentExample> examples) {
  examples_ = std::move(examples);
  ready_ = !examples_.empty();
  catalog_path_ = "(test)";
  catalog_hash_ = "test";
}

bool Level0IntentIndex::build(EmbeddingBackend* backend, const std::string& cache_dir,
                              const std::string& model_id, const ProgressFn& on_progress,
                              std::string* error) {
  if (backend == nullptr || !backend->ready()) {
    if (error) {
      *error = "embedding backend no ready";
    }
    return false;
  }
  if (examples_.empty()) {
    if (!load_catalog({}, error)) {
      return false;
    }
  }

  const std::string cache_file = cache_path_for(cache_dir, model_id, catalog_hash_);
  const std::string cached = read_file(cache_file);
  if (!cached.empty()) {
    try {
      const auto doc = nlohmann::json::parse(cached);
      if (doc.value("catalog_hash", "") == catalog_hash_ && doc.contains("rows") &&
          doc["rows"].is_array() && doc["rows"].size() == examples_.size()) {
        std::size_t i = 0;
        for (const auto& row : doc["rows"]) {
          if (i >= examples_.size()) {
            break;
          }
          examples_[i].embedding.clear();
          for (const auto& v : row["embedding"]) {
            examples_[i].embedding.push_back(v.get<float>());
          }
          ++i;
        }
        bool ok = true;
        for (const auto& ex : examples_) {
          if (ex.embedding.empty()) {
            ok = false;
            break;
          }
        }
        if (ok) {
          ready_ = true;
                    ai_trace(AiTraceChannel::Embed, "cache_hit", "{\"n\":" + std::to_string(examples_.size()) + ",\"file\":\"" +
                             ai_trace_escape(cache_file) + "\"}");
                    if (on_progress) {
            on_progress("L0 intent index: cache hit (" + std::to_string(examples_.size()) +
                        " ejemplos)");
          }
          return true;
        }
      }
    } catch (...) {
      // rebuild
    }
  }

  if (on_progress) {
    on_progress("L0 intent index: embebiendo " + std::to_string(examples_.size()) + " ejemplos…");
  }
  ai_trace(AiTraceChannel::Embed, "embed_passages_begin",
           "{\"n\":" + std::to_string(examples_.size()) + "}");
  {
    std::vector<std::string> passages;
    passages.reserve(examples_.size());
    for (const auto& ex : examples_) {
      passages.push_back(ex.example);
    }
    std::vector<std::vector<float>> vecs;
    std::string emb_err;
    if (!backend->embed_passages(passages, &vecs, &emb_err) || vecs.size() != examples_.size()) {
      if (error) {
        *error = "embed passages batch falló: " + emb_err;
      }
      ai_trace(AiTraceChannel::Embed, "embed_passage_fail",
               "{\"err\":\"" + ai_trace_escape(emb_err) + "\"}");
      ready_ = false;
      return false;
    }
    for (std::size_t i = 0; i < examples_.size(); ++i) {
      examples_[i].embedding = std::move(vecs[i]);
    }
  }

  nlohmann::json out;
  out["catalog_hash"] = catalog_hash_;
  out["model_id"] = model_id;
  out["rows"] = nlohmann::json::array();
  for (const auto& ex : examples_) {
    out["rows"].push_back({{"id", ex.intent_id},
                           {"name", ex.name},
                           {"example", ex.example},
                           {"embedding", ex.embedding}});
  }
  if (!write_bytes(cache_file, out.dump())) {
    if (on_progress) {
      on_progress("L0 intent index: no se pudo escribir cache " + cache_file);
    }
  } else if (on_progress) {
    on_progress("L0 intent index: cache " + cache_file);
  }
  ready_ = true;
  return true;
}

Level0IntentMatch Level0IntentIndex::match_precomputed(const std::vector<float>& query_embedding,
                                                       float min_score, float min_margin) const {
  Level0IntentMatch result;
  if (query_embedding.empty() || examples_.empty()) {
        ai_trace(AiTraceChannel::Embed, "empty_input", "{\"qdim\":" + std::to_string(query_embedding.size()) +
                       ",\"n\":" + std::to_string(examples_.size()) + "}");
        return result;
  }

  float best = -1.0f;
  std::size_t best_i = 0;
  for (std::size_t i = 0; i < examples_.size(); ++i) {
    if (examples_[i].embedding.empty()) {
      continue;
    }
    const float s = cosine_similarity(query_embedding, examples_[i].embedding);
    if (s > best) {
      best = s;
      best_i = i;
    }
  }
  if (best < 0.0f) {
    return result;
  }

  // Margin vs best score of a *different* intent (same-intent neighbors must not kill the match).
  float best_other = -1.0f;
  const std::string& best_name = examples_[best_i].name;
  for (std::size_t i = 0; i < examples_.size(); ++i) {
    if (i == best_i || examples_[i].embedding.empty() || examples_[i].name == best_name) {
      continue;
    }
    const float s = cosine_similarity(query_embedding, examples_[i].embedding);
    if (s > best_other) {
      best_other = s;
    }
  }
  const float margin = best - (best_other < 0.0f ? 0.0f : best_other);
  result.score = best;
  result.margin = margin;
  result.name = best_name;
  result.arg_policy = examples_[best_i].arg_policy;
  result.is_task = examples_[best_i].is_task;

    ai_trace(AiTraceChannel::Embed, "scores", "{\"best\":" + std::to_string(best) + ",\"best_other\":" + std::to_string(best_other) +
          ",\"margin\":" + std::to_string(margin) + ",\"name\":\"" +
          ai_trace_escape(best_name) + "\",\"qdim\":" +
          std::to_string(query_embedding.size()) + ",\"edim\":" +
          std::to_string(examples_[best_i].embedding.size()) + ",\"min_score\":" +
          std::to_string(min_score) + ",\"min_margin\":" + std::to_string(min_margin) + "}");
  
  if (best < min_score || margin < min_margin) {
    return result;  // ok stays false; score/margin filled for diagnostics
  }
  result.ok = true;
  return result;
}

Level0IntentMatch Level0IntentIndex::match(const std::string& query, EmbeddingBackend* backend,
                                           float min_score, float min_margin,
                                           std::string* error) const {
  Level0IntentMatch result;
  if (!ready_ || backend == nullptr || !backend->ready()) {
    if (error) {
      *error = "intent index no ready";
    }
    return result;
  }
  std::vector<float> q;
  if (!backend->embed_query(query, &q, error)) {
    return result;
  }
  return match_precomputed(q, min_score, min_margin);
}

}  // namespace tuide
