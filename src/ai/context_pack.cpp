#include "ai/context_pack.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "parser/tree_sitter_service.hpp"
#include "search/workspace_search.hpp"
#include "symbols/call_hierarchy.hpp"
#include "util/include_tree.hpp"

namespace fs = std::filesystem;

namespace tuide {
namespace {

std::string read_lines(const std::string& path, int start_1, int end_1, int max_lines) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream out;
  std::string line;
  int n = 0;
  int emitted = 0;
  while (std::getline(in, line)) {
    ++n;
    if (n < start_1) {
      continue;
    }
    if (n > end_1) {
      break;
    }
    out << line << '\n';
    if (++emitted >= max_lines) {
      out << "…\n";
      break;
    }
  }
  return out.str();
}

std::string resolve_path(const std::string& root, const std::string& file) {
  if (file.empty()) {
    return {};
  }
  fs::path p(file);
  if (p.is_absolute()) {
    return p.lexically_normal().string();
  }
  if (root.empty()) {
    return file;
  }
  return (fs::path(root) / p).lexically_normal().string();
}

}  // namespace

std::string ContextPack::dump_text() const {
  std::ostringstream out;
  out << "=== ContextPack ===\n";
  out << "seeds:";
  if (seeds.empty()) {
    out << " (none)";
  } else {
    for (const auto& s : seeds) {
      out << " `" << s << "`";
    }
  }
  out << '\n';
  if (!seed_note.empty()) {
    out << "note: " << seed_note << '\n';
  }
  out << "fragments: " << fragments.size() << '\n';
  for (const auto& f : fragments) {
    out << "--- [" << f.kind << "] " << f.path << ':' << f.start_line << '-' << f.end_line
        << " ---\n";
    out << f.text;
    if (!f.text.empty() && f.text.back() != '\n') {
      out << '\n';
    }
  }
  return out.str();
}

ContextPackAssembler::ContextPackAssembler(WorkspaceModel* workspace, WorkspaceIndexer* indexer,
                                           std::shared_ptr<ISymbolProvider> symbols)
    : workspace_(workspace), indexer_(indexer), symbols_(std::move(symbols)) {}

ContextPack ContextPackAssembler::assemble(const ContextPackOptions& opts) const {
  ContextPack pack;
  pack.seeds = opts.seeds;
  const std::string root = workspace_ != nullptr ? workspace_->root : std::string{};

  if (workspace_ != nullptr && !workspace_->buffer.path.empty()) {
    ContextPackFragment sel;
    sel.path = workspace_->buffer.path;
    sel.kind = "selection";
    const int line = std::clamp(workspace_->buffer.primary_line(), 0,
                                std::max(0, workspace_->buffer.lines.size() - 1));
    sel.start_line = line + 1;
    sel.end_line = line + 1;
    sel.text = workspace_->buffer.lines[line];
    pack.fragments.push_back(std::move(sel));

    if (symbols_) {
      const auto diags = symbols_->diagnostics_for_file(workspace_->buffer.path);
      if (!diags.items.empty()) {
        ContextPackFragment d;
        d.path = workspace_->buffer.path;
        d.kind = "diagnostic";
        d.start_line = 1;
        d.end_line = 1;
        std::ostringstream oss;
        const int limit = std::min<int>(12, static_cast<int>(diags.items.size()));
        for (int i = 0; i < limit; ++i) {
          const auto& item = diags.items[static_cast<std::size_t>(i)];
          oss << item.line << ':' << item.start_col << " " << item.message << '\n';
        }
        d.text = oss.str();
        pack.fragments.push_back(std::move(d));
      }
    }
  }

  if (opts.seeds.empty()) {
    pack.seed_note = "sin seeds (S0 cursor si hay archivo activo)";
    return pack;
  }

  WorkspaceSearchOptions search_opts;
  search_opts.workspace_root = root;
  if (indexer_ != nullptr) {
    const auto snap = indexer_->snapshot();
    if (snap) {
      search_opts.files = snap->files;
    }
  }

  auto& ts = TreeSitterService::instance();

  for (const auto& seed : opts.seeds) {
    if (static_cast<int>(pack.fragments.size()) >= opts.max_fragments) {
      break;
    }
    search_opts.needle = seed;
    auto hits = search_workspace(search_opts);
    const int hit_limit =
        std::min(opts.max_hits_per_seed, static_cast<int>(hits.size()));

    // Confirm via workspace_symbols when available (S3).
    if (symbols_ && !root.empty()) {
      const auto syms = symbols_->workspace_symbols(root, seed);
      for (const auto& sym : syms) {
        if (static_cast<int>(pack.fragments.size()) >= opts.max_fragments) {
          break;
        }
        const std::string abs = resolve_path(root, sym.file.empty() ? std::string{} : sym.file);
        if (abs.empty()) {
          continue;
        }
        int start = std::max(1, sym.line);
        int end = sym.end_line > 0 ? sym.end_line : start;
        end = std::min(end, start + opts.max_body_lines - 1);
        ContextPackFragment frag;
        frag.path = abs;
        frag.kind = "hit_body";
        frag.start_line = start;
        frag.end_line = end;
        // Prefer TS body if we can load source.
        std::ifstream in(abs);
        std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!source.empty()) {
          const auto file_syms = ts.symbols_for_file(abs, source);
          for (const auto& fs_sym : file_syms) {
            if (fs_sym.name == sym.name && fs_sym.line > 0) {
              start = fs_sym.line;
              end = fs_sym.end_line > 0 ? fs_sym.end_line : start;
              end = std::min(end, start + opts.max_body_lines - 1);
              frag.start_line = start;
              frag.end_line = end;
              break;
            }
          }
        }
        frag.text = read_lines(abs, frag.start_line, frag.end_line, opts.max_body_lines);
        if (!frag.text.empty()) {
          pack.fragments.push_back(std::move(frag));
        }

        if (opts.include_incoming_calls && symbols_) {
          CallHierarchyParams ch;
          ch.path = abs;
          ch.text = source;
          ch.line = std::max(0, sym.line - 1);
          ch.character = 0;
          const auto prepared = symbols_->prepare_call_hierarchy(ch);
          if (!prepared.empty()) {
            const auto callers = symbols_->incoming_calls(prepared.front());
            int caller_n = 0;
            for (const auto& caller : callers) {
              if (caller_n++ >= 5 ||
                  static_cast<int>(pack.fragments.size()) >= opts.max_fragments) {
                break;
              }
              ContextPackFragment cf;
              cf.path = caller.path;
              cf.kind = "caller";
              cf.start_line = std::max(1, caller.call_site_line > 0 ? caller.call_site_line
                                                                   : caller.line);
              cf.end_line = cf.start_line;
              cf.text = caller.name + (caller.detail.empty() ? "" : (" — " + caller.detail));
              pack.fragments.push_back(std::move(cf));
            }
          }
        }
      }
    }

    for (int i = 0; i < hit_limit; ++i) {
      if (static_cast<int>(pack.fragments.size()) >= opts.max_fragments) {
        break;
      }
      const auto& hit = hits[static_cast<std::size_t>(i)];
      const std::string abs = resolve_path(root, hit.file);
      ContextPackFragment frag;
      frag.path = abs;
      frag.kind = "hit_body";
      frag.start_line = std::max(1, hit.line - 2);
      frag.end_line = hit.line + 12;
      frag.text = read_lines(abs, frag.start_line, frag.end_line, opts.max_body_lines);
      if (frag.text.empty()) {
        frag.text = hit.preview;
      }
      pack.fragments.push_back(std::move(frag));
    }
  }

  if (opts.include_headers && workspace_ != nullptr && !workspace_->buffer.path.empty() &&
      indexer_ != nullptr) {
    const auto snap = indexer_->snapshot();
    if (snap) {
      const auto headers =
          build_include_tree(workspace_->buffer.path, root, snap->files,
                             workspace_->buffer.lines.to_string());
      int n = 0;
      for (const auto& h : headers) {
        if (h == workspace_->buffer.path) {
          continue;
        }
        if (++n > 6 || static_cast<int>(pack.fragments.size()) >= opts.max_fragments) {
          break;
        }
        ContextPackFragment frag;
        frag.path = h;
        frag.kind = "header";
        frag.start_line = 1;
        frag.end_line = 40;
        frag.text = read_lines(h, 1, 40, 40);
        if (!frag.text.empty()) {
          pack.fragments.push_back(std::move(frag));
        }
      }
    }
  }

  pack.seed_note = "pipeline: seeds → rg/workspace_symbols → TS bodies → callers → headers";
  return pack;
}

}  // namespace tuide
