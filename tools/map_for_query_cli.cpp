// Write .tuide/ai/map_last.md for a NL query using the same offline ranking pipeline
// as stem_boost_battery (lexical map + stem priors + enrich). No L1 LLM needles.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "ai/ai_types.hpp"
#include "ai/coding_embed_rerank.hpp"
#include "ai/coding_stem_embed_index.hpp"
#include "ai/embedding_backend.hpp"
#include "ai/get_code_of.hpp"
#include "ai/model_store.hpp"
#include "ai/repo_map.hpp"
#include "indexer/index_rules.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "parser/tree_sitter_tags.hpp"

namespace fs = std::filesystem;

namespace tuide {
std::string join_editor_lines_from_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}
}  // namespace tuide

namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

std::string path_stem_of(const std::string& file) {
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

tuide::SymbolIndexSnapshot build_snapshot(const std::string& root) {
  tuide::SymbolIndexSnapshot snap;
  snap.workspace_root = root;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(root, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec || !it->is_regular_file(ec)) {
      continue;
    }
    std::string rel = fs::relative(it->path(), root, ec).generic_string();
    if (ec || rel.empty() || !tuide::should_index_relative_path(rel)) {
      continue;
    }
    const std::string abs = it->path().string();
    const std::string source = read_file(abs);
    if (source.empty()) {
      continue;
    }
    for (const auto& tag : tuide::extract_repo_map_tags(abs, rel, source)) {
      if (tag.tag_kind != tuide::RepoMapTagKind::Def) {
        continue;
      }
      tuide::IndexedSymbol entry;
      entry.display_name = tag.name;
      entry.name = tag.name;
      entry.kind = tag.symbol_kind;
      entry.line = tag.line;
      entry.file = rel;
      entry.signature = tag.signature;
      snap.symbols.push_back(std::move(entry));
    }
  }
  return snap;
}

}  // namespace

int main(int argc, char** argv) {
  std::string workspace = fs::current_path().string();
  std::string query;
  std::string out_path;
  std::string seeds_out_path;
  std::string cache_dir = tuide::ModelStore::default_cache_dir();

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto need = [&](const char* f) -> std::string {
      if (i + 1 >= argc) {
        std::cerr << "missing " << f << "\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--workspace") {
      workspace = need("--workspace");
    } else if (a == "--query") {
      query = need("--query");
    } else if (a == "--out") {
      out_path = need("--out");
    } else if (a == "--seeds-out") {
      seeds_out_path = need("--seeds-out");
    } else if (a == "--cache") {
      cache_dir = need("--cache");
    } else if (a == "-h" || a == "--help") {
      std::cerr << "Usage: map_for_query_cli --query \"…\" [--workspace ROOT] [--out PATH]\n";
      return 0;
    } else {
      std::cerr << "unknown " << a << "\n";
      return 2;
    }
  }
  if (query.empty()) {
    std::cerr << "need --query\n";
    return 2;
  }
  if (out_path.empty()) {
    out_path = (fs::path(workspace) / ".tuide" / "ai" / "map_last.md").string();
  }

  const auto snap = build_snapshot(workspace);

  tuide::AiSettings settings;
  settings.level0.embeddings.model_path =
      tuide::ModelStore(cache_dir).intent_embed_model_path(tuide::default_intent_embed_model());
  tuide::EmbeddingBackend backend;
  std::string err;
  auto progress = [](const std::string& line) { std::cerr << line << '\n'; };
  if (!backend.ensure_ready(settings, progress, &err)) {
    std::cerr << "embed: " << err << '\n';
    return 1;
  }

  tuide::CodingStemEmbedIndex stem_index;
  if (!stem_index.ensure(&snap, &backend, cache_dir, settings.level0.embeddings.model_id, progress,
                         &err, tuide::default_stem_passage_profile())) {
    std::cerr << "stem index: " << err << '\n';
    return 1;
  }

  tuide::RepoMapOptions opts;
  opts.query = query;
  opts.max_symbols = 96;
  opts.max_files = 32;

  tuide::RepoMap map = tuide::build_repo_map(&snap, opts);
  map.coding_embed = &backend;
  map.coding_stem_index = &stem_index;
  auto entries = map.entries;
  for (auto& e : entries) {
    e.stem = path_stem_of(e.file);
    e.score_base = e.score;
  }
  tuide::apply_ranked_map_priors(query, &entries, &stem_index, &backend);
  map.entries = std::move(entries);
  map.enrich_dominant_stem_from_snapshot(&snap, query, 24);

  tuide::RankedMapDumpOptions dump_opts;
  dump_opts.workspace_root = workspace;
  dump_opts.query = query;
  dump_opts.note =
      "priors=1; lex_prefilter=1; src=map_for_query_cli; ranked_map=1; entries=" +
      std::to_string(map.entries.size());
  dump_opts.entries = map.entries;
  dump_opts.include_bodies = false;
  dump_opts.filename = fs::path(out_path).filename().string();

  // dump_ranked_map_md writes under .tuide/ai/ — copy to --out if custom dir.
  const fs::path default_ai = fs::path(workspace) / ".tuide" / "ai";
  const std::string written = tuide::dump_ranked_map_md(dump_opts, &err);
  if (written.empty()) {
    std::cerr << "dump failed: " << err << '\n';
    backend.stop();
    return 1;
  }

  fs::path final_path = out_path;
  if (fs::path(written) != final_path) {
    std::error_code ec;
    fs::create_directories(final_path.parent_path(), ec);
    fs::copy_file(written, final_path, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      std::cerr << "copy to --out failed: " << ec.message() << '\n';
      backend.stop();
      return 1;
    }
  }

  std::cout << "wrote " << final_path.string() << " entries=" << map.entries.size()
            << " context_stem=" << map.context_stem << '\n';

  // --seeds-out: write top-N unique file stems from ranked map as a JSON array.
  // These are used as L1 seeds for the L2 bootstrap to anchor coverage gating.
  if (!seeds_out_path.empty()) {
    std::vector<std::string> stems;
    std::unordered_set<std::string> seen;
    for (const auto& e : map.entries) {
      if (stems.size() >= 8) {
        break;
      }
      const std::string s = path_stem_of(e.file);
      if (s.empty() || !seen.insert(s).second) {
        continue;
      }
      stems.push_back(s);
    }
    std::ostringstream sj;
    sj << '[';
    for (std::size_t i = 0; i < stems.size(); ++i) {
      if (i) {
        sj << ',';
      }
      sj << '"';
      for (char c : stems[i]) {
        if (c == '"' || c == '\\') {
          sj << '\\';
        }
        sj << c;
      }
      sj << '"';
    }
    sj << ']';
    std::ofstream sf(seeds_out_path);
    sf << sj.str() << '\n';
    std::cout << "seeds_out=" << seeds_out_path << " stems=" << stems.size() << '\n';
  }

  backend.stop();
  return 0;
}
