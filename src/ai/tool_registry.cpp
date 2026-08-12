#include "ai/tool_registry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "ai/context_pack.hpp"
#include "ai/get_code_of.hpp"
#include "ai/ai_trace.hpp"
#include "ai/edit_journal.hpp"
#include "ai/repo_map.hpp"
#include "ai/search_needles.hpp"
#include "git/git_command.hpp"
#include "git/git_log.hpp"
#include "search/workspace_search.hpp"

namespace fs = std::filesystem;

namespace tuide {
namespace {

std::string trim_copy(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::string read_file_limited(const std::string& path, int max_lines = 400) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream out;
  std::string line;
  int count = 0;
  while (std::getline(in, line)) {
    out << line << '\n';
    if (++count >= max_lines) {
      out << "… (truncated)\n";
      break;
    }
  }
  return out.str();
}

std::vector<std::string> split_path_filters(const std::string& arg) {
  std::vector<std::string> filters;
  std::string cur;
  for (char ch : arg) {
    if (ch == ',' || ch == '|') {
      while (!cur.empty() && std::isspace(static_cast<unsigned char>(cur.back()))) {
        cur.pop_back();
      }
      std::size_t start = 0;
      while (start < cur.size() && std::isspace(static_cast<unsigned char>(cur[start]))) {
        ++start;
      }
      if (start < cur.size()) {
        filters.push_back(cur.substr(start));
      }
      cur.clear();
    } else {
      cur.push_back(ch);
    }
  }
  while (!cur.empty() && std::isspace(static_cast<unsigned char>(cur.back()))) {
    cur.pop_back();
  }
  std::size_t start = 0;
  while (start < cur.size() && std::isspace(static_cast<unsigned char>(cur[start]))) {
    ++start;
  }
  if (start < cur.size()) {
    filters.push_back(cur.substr(start));
  }
  return filters;
}

bool path_matches_filters(const std::string& path, const std::vector<std::string>& filters) {
  if (filters.empty()) {
    return true;
  }
  for (const auto& filter : filters) {
    if (path == filter || path.rfind(filter + "/", 0) == 0 || path.rfind(filter, 0) == 0) {
      return true;
    }
  }
  return false;
}

std::string resolve_workspace_path(const std::string& root, const std::string& path) {
  if (path.empty()) {
    return {};
  }
  fs::path p(path);
  if (p.is_absolute()) {
    return p.lexically_normal().string();
  }
  if (root.empty()) {
    return p.lexically_normal().string();
  }
  return (fs::path(root) / p).lexically_normal().string();
}

bool path_inside_workspace(const std::string& root, const std::string& path) {
  if (root.empty()) {
    return true;
  }
  const auto abs = fs::path(path).lexically_normal();
  const auto base = fs::path(root).lexically_normal();
  const auto rel = fs::relative(abs, base);
  return !rel.empty() && rel.native().rfind("..", 0) != 0;
}

}  // namespace

void ToolRegistry::register_tool(std::string name, std::string help, AiToolHandler handler) {
  tools_[std::move(name)] = Entry{std::move(help), std::move(handler)};
}

bool ToolRegistry::has(const std::string& name) const {
  return tools_.find(name) != tools_.end();
}

AiToolResult ToolRegistry::invoke(const std::string& name, const std::string& arg) const {
  const auto it = tools_.find(name);
  if (it == tools_.end() || !it->second.handler) {
    return {false, "tool desconocida: " + name};
  }
  return it->second.handler(arg);
}

std::vector<std::pair<std::string, std::string>> ToolRegistry::list_tools() const {
  std::vector<std::pair<std::string, std::string>> out;
  out.reserve(tools_.size());
  for (const auto& [name, entry] : tools_) {
    out.emplace_back(name, entry.help);
  }
  std::sort(out.begin(), out.end());
  return out;
}

void ToolRegistry::register_builtin_read_tools(ToolRegistry* registry, AiToolContext ctx) {
  if (registry == nullptr) {
    return;
  }

  registry->register_tool("list_tools", "Lista tools registradas", [registry](const std::string&) {
    std::ostringstream out;
    for (const auto& [name, help] : registry->list_tools()) {
      out << "  " << name << " — " << help << '\n';
    }
    return AiToolResult{true, out.str()};
  });

  registry->register_tool("read_file", "Lee un archivo del workspace", [ctx](const std::string& arg) {
    const std::string path = resolve_workspace_path(ctx.workspace_root, arg);
    if (path.empty()) {
      return AiToolResult{false, "ruta vacía"};
    }
    if (!path_inside_workspace(ctx.workspace_root, path)) {
      return AiToolResult{false, "ruta fuera del workspace"};
    }
    if (!fs::exists(path) || !fs::is_regular_file(path)) {
      return AiToolResult{false, "no existe: " + path};
    }
    const std::string text = read_file_limited(path);
    if (text.empty()) {
      return AiToolResult{false, "no se pudo leer: " + path};
    }
    return AiToolResult{true, path + "\n" + text};
  });

  registry->register_tool(
      "get_code_of",
      "Extrae cuerpo de clase/función vía tree-sitter. Arg: path:Symbol | path:line | Symbol",
      [ctx](const std::string& arg) {
        GetCodeOfRequest req = parse_get_code_of_arg(arg, ctx.workspace_root);
        req.workspace_root = ctx.workspace_root;
        if (req.max_lines <= 0) {
          req.max_lines = 120;
        }
        // Resolve bare symbol via symbol index (first strong name match).
        if (req.file.empty() && !req.symbol.empty() && ctx.symbol_indexer != nullptr) {
          const auto snap = ctx.symbol_indexer->snapshot();
          if (snap) {
            std::string want = req.symbol;
            for (char& c : want) {
              c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            for (const auto& sym : snap->symbols) {
              std::string nl = sym.name.empty() ? sym.display_name : sym.name;
              for (char& c : nl) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
              }
              if (nl == want || nl.find(want) != std::string::npos) {
                req.file = sym.file;
                if (req.line <= 0) {
                  req.line = sym.line;
                }
                break;
              }
            }
          }
        }
        if (req.file.empty()) {
          return AiToolResult{false, "get_code_of: indica path:Symbol, path:line o un símbolo del índice"};
        }
        const std::string abs = resolve_workspace_path(ctx.workspace_root, req.file);
        if (!path_inside_workspace(ctx.workspace_root, abs)) {
          return AiToolResult{false, "ruta fuera del workspace"};
        }
        req.file = abs;
        const GetCodeOfResult got = get_code_of(req);
        if (!got.ok) {
          return AiToolResult{false, got.error.empty() ? "get_code_of falló" : got.error};
        }
        std::ostringstream out;
        out << got.path << ':' << got.start_line << '-' << got.end_line;
        if (!got.name.empty()) {
          out << " (" << got.name << ")";
        }
        if (got.truncated) {
          out << " [truncated]";
        }
        out << '\n' << got.text;
        return AiToolResult{true, out.str()};
      });

  registry->register_tool("list_files", "Lista archivos del índice / directorio",
                          [ctx](const std::string& arg) {
                            std::ostringstream out;
                            if (ctx.indexer != nullptr) {
                              const auto snap = ctx.indexer->snapshot();
                              if (snap) {
                                const std::string filter = arg;
                                int shown = 0;
                                for (const auto& file : snap->files) {
                                  if (!filter.empty() && file.find(filter) == std::string::npos) {
                                    continue;
                                  }
                                  out << file << '\n';
                                  if (++shown >= 200) {
                                    out << "… (truncated)\n";
                                    break;
                                  }
                                }
                                if (shown == 0) {
                                  out << "(sin archivos)\n";
                                }
                                return AiToolResult{true, out.str()};
                              }
                            }
                            const std::string dir =
                                arg.empty() ? ctx.workspace_root
                                            : resolve_workspace_path(ctx.workspace_root, arg);
                            std::error_code ec;
                            if (!fs::exists(dir, ec)) {
                              return AiToolResult{false, "directorio no encontrado"};
                            }
                            int shown = 0;
                            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                              out << entry.path().filename().string() << '\n';
                              if (++shown >= 200) {
                                break;
                              }
                            }
                            return AiToolResult{true, out.str()};
                          });

  registry->register_tool(
      "search",
      "Busca 1..N needles (separa con |). Expande snake/Camel genérico; prioriza src/",
      [ctx](const std::string& arg) {
        if (arg.empty()) {
          return AiToolResult{false, "needle vacío"};
        }
        std::string raw = arg;
        std::string path_filter;
        {
          const auto path_pos = raw.find(" path:");
          if (path_pos != std::string::npos) {
            path_filter = trim_copy(raw.substr(path_pos + 6));
            raw = trim_copy(raw.substr(0, path_pos));
          }
        }
        const auto needles = expand_search_needles(raw, 12);
        if (needles.empty()) {
          return AiToolResult{false, "needle vacío"};
        }

        std::vector<std::string> indexed_files;
        if (ctx.indexer != nullptr) {
          const auto snap = ctx.indexer->snapshot();
          if (snap) {
            indexed_files = snap->files;
          }
        }

        struct ScoredHit {
          WorkspaceSearchResult hit;
          std::string needle;
          int score = 0;
          bool name_match = false;
        };
        std::vector<ScoredHit> scored;
        std::ostringstream per_needle;
        std::size_t total_raw = 0;
        int needles_with_hits = 0;

        for (const auto& needle : needles) {
          WorkspaceSearchOptions opts;
          opts.workspace_root = ctx.workspace_root;
          opts.needle = needle;
          opts.path_filter = path_filter;
          opts.files = indexed_files;
          auto hits = search_workspace(opts);
          total_raw += hits.size();
          per_needle << "  " << needle << ": " << hits.size() << '\n';
          if (!hits.empty()) {
            ++needles_with_hits;
          }
          for (auto& h : hits) {
            ScoredHit s;
            s.hit = std::move(h);
            s.needle = needle;
            s.score = score_search_hit(s.hit.file, needle, needles);
            s.name_match = filename_seed_match_score(s.hit.file, needles) > 0;
            scored.push_back(std::move(s));
          }
        }

        std::stable_sort(scored.begin(), scored.end(),
                         [](const ScoredHit& a, const ScoredHit& b) {
                           if (a.score != b.score) {
                             return a.score > b.score;
                           }
                           if (a.hit.file != b.hit.file) {
                             return a.hit.file < b.hit.file;
                           }
                           return a.hit.line < b.hit.line;
                         });

        // Dedupe same file:line across needles (keep best score).
        std::vector<ScoredHit> unique;
        for (auto& s : scored) {
          bool dup = false;
          for (auto& u : unique) {
            if (u.hit.file == s.hit.file && u.hit.line == s.hit.line) {
              if (s.score > u.score) {
                u = std::move(s);
              }
              dup = true;
              break;
            }
          }
          if (!dup) {
            unique.push_back(std::move(s));
          }
        }

        // Cap lines per file so vague needles (explorer, tree…) don't flood L1.
        constexpr int kMaxHitsPerFile = 2;
        constexpr int kNoisyFiles = 25;
        constexpr int kShowHits = 18;
        std::vector<ScoredHit> capped;
        std::unordered_map<std::string, int> per_file;
        for (auto& s : unique) {
          int& n = per_file[s.hit.file];
          if (n >= kMaxHitsPerFile) {
            continue;
          }
          ++n;
          capped.push_back(std::move(s));
        }

        int name_files = 0;
        {
          std::unordered_set<std::string> seen_name;
          for (const auto& s : capped) {
            if (s.name_match && seen_name.insert(s.hit.file).second) {
              ++name_files;
            }
          }
        }
        const int files_hit = static_cast<int>(per_file.size());
        const bool empty = capped.empty();
        // Name-matched files are a strong signal → not "noisy" for L1 even if raw was huge.
        const bool noisy = !empty && name_files == 0 &&
                           (files_hit >= kNoisyFiles || unique.size() >= 80);
        std::ostringstream out;
        out << "needles_tried: " << needles.size() << "  hits: " << capped.size()
            << "  files: " << files_hit << "  name_files: " << name_files
            << "  raw_hits: " << total_raw << "  unique_lines: " << unique.size()
            << "  needles_hit: " << needles_with_hits;
        if (!path_filter.empty()) {
          out << "  path:" << path_filter;
        }
        out << "  quality:" << (noisy ? "noisy" : (empty ? "empty" : "ok")) << '\n';
        out << "needles:\n" << per_needle.str();
        if (empty) {
          out << "hint: 0 hits. Emite OTRA search con needles|distintos "
                 "(órdenes snake, CamelCase, tokens EN). No repitas el mismo set.\n";
        } else if (noisy) {
          out << "hint: demasiados hits sin match de nombre; acota path:src/… "
                 "o needles más específicos (compuestos snake/Camel).\n";
        } else if (name_files > 0) {
          out << "hint: prioriza top_files (nombre contiene seed).\n";
        }

        std::vector<std::string> top_files;
        for (const auto& s : capped) {
          bool seen = false;
          for (const auto& f : top_files) {
            if (f == s.hit.file) {
              seen = true;
              break;
            }
          }
          if (!seen) {
            top_files.push_back(s.hit.file);
          }
          if (top_files.size() >= 10) {
            break;
          }
        }
        if (!top_files.empty()) {
          out << "top_files:\n";
          for (const auto& f : top_files) {
            const bool nm = filename_seed_match_score(f, needles) > 0;
            out << "  " << f << (nm ? "  [name]" : "") << '\n';
          }
        }

        const int limit = std::min<int>(kShowHits, static_cast<int>(capped.size()));
        for (int i = 0; i < limit; ++i) {
          const auto& s = capped[static_cast<std::size_t>(i)];
          out << s.hit.file << ':' << s.hit.line << ':' << s.hit.col << "  [" << s.needle << "]"
              << (s.name_match ? " [name]" : "") << "  " << s.hit.preview << '\n';
        }
        if (capped.size() > static_cast<std::size_t>(limit)) {
          out << "… (+" << (capped.size() - static_cast<std::size_t>(limit)) << " more)\n";
        }
        return AiToolResult{true, out.str()};
      });

  registry->register_tool("diagnostics", "Diagnostics LSP del archivo activo / workspace",
                          [ctx](const std::string& arg) {
                            if (!ctx.symbols) {
                              return AiToolResult{false, "sin ISymbolProvider"};
                            }
                            std::ostringstream out;
                            if (!arg.empty()) {
                              const auto path = resolve_workspace_path(ctx.workspace_root, arg);
                              const auto diags = ctx.symbols->diagnostics_for_file(path);
                              out << "file: " << path << "  count=" << diags.items.size() << '\n';
                              for (const auto& d : diags.items) {
                                out << d.line << ':' << d.start_col << " ["
                                    << diagnostic_severity_label(d.severity) << "] " << d.message
                                    << '\n';
                              }
                              return AiToolResult{true, out.str()};
                            }
                            if (ctx.workspace != nullptr && !ctx.workspace->buffer.path.empty()) {
                              const auto diags =
                                  ctx.symbols->diagnostics_for_file(ctx.workspace->buffer.path);
                              out << "active: " << ctx.workspace->buffer.path
                                  << "  count=" << diags.items.size() << '\n';
                              for (const auto& d : diags.items) {
                                out << d.line << ':' << d.start_col << " " << d.message << '\n';
                              }
                            }
                            const auto all = ctx.symbols->workspace_diagnostics();
                            int total = 0;
                            for (const auto& doc : all) {
                              total += static_cast<int>(doc.items.size());
                            }
                            out << "workspace diagnostics docs=" << all.size()
                                << " items=" << total << '\n';
                            int shown = 0;
                            for (const auto& doc : all) {
                              for (const auto& d : doc.items) {
                                out << doc.path << ':' << d.line << " " << d.message << '\n';
                                if (++shown >= 60) {
                                  out << "…\n";
                                  return AiToolResult{true, out.str()};
                                }
                              }
                            }
                            return AiToolResult{true, out.str()};
                          });

  registry->register_tool("workspace_symbols", "LSP workspace symbols",
                          [ctx](const std::string& arg) {
                            if (!ctx.symbols) {
                              return AiToolResult{false, "sin ISymbolProvider"};
                            }
                            const auto syms =
                                ctx.symbols->workspace_symbols(ctx.workspace_root, arg);
                            std::ostringstream out;
                            out << "symbols: " << syms.size() << "  q=" << arg << '\n';
                            const int limit = std::min<int>(40, static_cast<int>(syms.size()));
                            for (int i = 0; i < limit; ++i) {
                              const auto& s = syms[static_cast<std::size_t>(i)];
                              out << s.name << "  " << s.file << ':' << s.line << '\n';
                            }
                            return AiToolResult{true, out.str()};
                          });

  registry->register_tool("hover", "Hover LSP en cursor o path:line:col",
                          [ctx](const std::string& arg) {
                            if (!ctx.symbols || ctx.workspace == nullptr) {
                              return AiToolResult{false, "sin contexto LSP/editor"};
                            }
                            HoverParams params;
                            params.path = ctx.workspace->buffer.path;
                            params.text = ctx.workspace->buffer.lines.to_string();
                            params.line = ctx.workspace->buffer.primary_line();
                            params.character = ctx.workspace->buffer.primary_col();
                            if (!arg.empty()) {
                              const auto c2 = arg.rfind(':');
                              const auto c1 = c2 == std::string::npos ? std::string::npos
                                                                     : arg.rfind(':', c2 - 1);
                              if (c1 != std::string::npos && c2 != std::string::npos) {
                                params.path =
                                    resolve_workspace_path(ctx.workspace_root, arg.substr(0, c1));
                                params.line = std::atoi(arg.substr(c1 + 1, c2 - c1 - 1).c_str());
                                params.character = std::atoi(arg.substr(c2 + 1).c_str());
                                params.text = read_file_limited(params.path, 20000);
                              }
                            }
                            if (!ctx.symbols->supports_hover()) {
                              return AiToolResult{false, "hover no soportado"};
                            }
                            const auto info = ctx.symbols->hover_at(params);
                            if (!info.valid) {
                              return AiToolResult{true, "(sin hover)"};
                            }
                            std::ostringstream out;
                            if (!info.title.empty()) {
                              out << info.title << '\n';
                            }
                            for (const auto& line : info.body_lines) {
                              out << line << '\n';
                            }
                            return AiToolResult{true, out.str()};
                          });

  registry->register_tool("git_status",
                          "Git status (arg opcional: prefijo de ruta, ej. examples o tools,src)",
                          [ctx](const std::string& arg) {
                            if (ctx.git == nullptr) {
                              return AiToolResult{false, "GitService no disponible"};
                            }
                            if (!ctx.git->is_repo()) {
                              return AiToolResult{false, "no es un repositorio git"};
                            }
                            ctx.git->refresh_status();
                            const auto snap = ctx.git->status();
                            const auto filters = split_path_filters(arg);
                            std::vector<std::string> matched;
                            for (const auto& e : snap.entries) {
                              if (path_matches_filters(e.path, filters)) {
                                matched.push_back(e.path);
                              }
                            }
                            std::ostringstream out;
                            if (!filters.empty()) {
                              std::string label = filters.front();
                              for (std::size_t i = 1; i < filters.size(); ++i) {
                                label += " | " + filters[i];
                              }
                              if (matched.empty()) {
                                out << "No hay archivos modificados bajo `" << label << "`.\n";
                                out << "(repo global: staged=" << snap.staged_count
                                    << " modified=" << snap.unstaged_count
                                    << " untracked=" << snap.untracked_count << ")\n";
                                return AiToolResult{true, out.str()};
                              }
                              out << "Archivos modificados bajo `" << label << "` ("
                                  << matched.size() << "):\n";
                            } else {
                              out << "Estado del repo: staged=" << snap.staged_count
                                  << " modified=" << snap.unstaged_count
                                  << " untracked=" << snap.untracked_count << '\n';
                              const int total = snap.staged_count + snap.unstaged_count +
                                                snap.untracked_count;
                              if (total == 0) {
                                out << "Working tree limpio: no hay cambios pendientes.\n";
                              } else if (total <= 5) {
                                out << "Hay pocos cambios (" << total << " entradas).\n";
                              } else {
                                out << "Sí: hay bastantes cambios (" << total
                                    << " entradas en total).\n";
                              }
                            }
                            for (const auto& path : matched) {
                              out << path << '\n';
                            }
                            if (filters.empty() && matched.empty()) {
                              out << "(working tree clean)\n";
                            }
                            return AiToolResult{true, out.str()};
                          });

  registry->register_tool("git_pull", "Ejecuta git pull y refresca el status",
                          [ctx](const std::string&) {
                            if (ctx.git == nullptr) {
                              return AiToolResult{false, "GitService no disponible"};
                            }
                            if (!ctx.git->is_repo()) {
                              return AiToolResult{false, "no es un repositorio git"};
                            }
                            const auto info = ctx.git->context_repo_info();
                            if (!info.valid || info.root.empty()) {
                              return AiToolResult{false, "no hay repo git activo"};
                            }
                            const GitCommandResult result = run_git(info.root, {"pull"});
                            if (!result.success()) {
                              std::ostringstream err;
                              err << "git pull falló";
                              if (!result.stderr_text.empty()) {
                                err << ":\n" << result.stderr_text;
                              } else if (!result.stdout_text.empty()) {
                                err << ":\n" << result.stdout_text;
                              }
                              return AiToolResult{false, err.str()};
                            }
                            ctx.git->invalidate();
                            ctx.git->refresh_status();
                            std::ostringstream out;
                            const std::string detail =
                                result.stdout_text.empty() ? std::string("Already up to date.")
                                                           : result.stdout_text;
                            out << "Repositorio actualizado con git pull.\n" << detail;
                            if (!detail.empty() && detail.back() != '\n') {
                              out << '\n';
                            }
                            return AiToolResult{true, out.str()};
                          });

  registry->register_tool("git_branches", "Lista ramas locales y remotas", [ctx](const std::string&) {
    if (ctx.git == nullptr) {
      return AiToolResult{false, "GitService no disponible"};
    }
    if (!ctx.git->is_repo()) {
      return AiToolResult{false, "no es un repositorio git"};
    }
    const auto info = ctx.git->context_repo_info();
    if (!info.valid || info.root.empty()) {
      return AiToolResult{false, "no hay repo git activo"};
    }
    const GitCommandResult result = run_git(info.root, {"branch", "-a", "--no-color"});
    if (!result.success()) {
      return AiToolResult{false, result.stderr_text.empty() ? "git branch falló"
                                                            : result.stderr_text};
    }
    const auto branches = parse_git_branches(result.stdout_text);
    std::ostringstream out;
    out << "Ramas del repositorio (" << branches.size() << "):\n";
    int remote_count = 0;
    int local_count = 0;
    for (const auto& b : branches) {
      if (b.remote) {
        ++remote_count;
        out << "  [remote] " << b.name;
      } else {
        ++local_count;
        out << "  [local]  " << b.name;
      }
      if (b.current) {
        out << "  ← actual";
      }
      out << '\n';
    }
    if (branches.empty()) {
      out << "(sin ramas)\n";
    } else {
      out << "Resumen: " << local_count << " locales, " << remote_count << " remotas.\n";
    }
    return AiToolResult{true, out.str()};
  });

  registry->register_tool(
      "git_log", "Historial de commits (arg opcional: ref y/o N, ej. \"main 5\")",
      [ctx](const std::string& arg) {
        if (ctx.git == nullptr) {
          return AiToolResult{false, "GitService no disponible"};
        }
        if (!ctx.git->is_repo()) {
          return AiToolResult{false, "no es un repositorio git"};
        }
        const auto info = ctx.git->context_repo_info();
        if (!info.valid || info.root.empty()) {
          return AiToolResult{false, "no hay repo git activo"};
        }
        std::string ref;
        int n = 12;
        {
          std::istringstream iss(arg);
          std::string tok;
          while (iss >> tok) {
            bool digits = !tok.empty();
            for (char c : tok) {
              if (!std::isdigit(static_cast<unsigned char>(c))) {
                digits = false;
                break;
              }
            }
            if (digits) {
              n = std::stoi(tok);
              if (n < 1) {
                n = 1;
              }
              if (n > 50) {
                n = 50;
              }
            } else {
              ref = tok;
            }
          }
        }
        if (ref == "@active" || ref == "@open") {
          if (ctx.workspace != nullptr && !ctx.workspace->active_file.empty()) {
            ref = ctx.workspace->active_file;
            if (!ctx.workspace_root.empty() && ref.find(ctx.workspace_root) == 0) {
              auto rel = ref.substr(ctx.workspace_root.size());
              while (!rel.empty() && (rel[0] == '/' || rel[0] == '\\')) {
                rel.erase(rel.begin());
              }
              if (!rel.empty()) {
                ref = rel;
              }
            }
          } else {
            return AiToolResult{false, "no hay archivo activo en el editor"};
          }
        }
        const bool path_limit =
            !ref.empty() && (ref.find('/') != std::string::npos || ref.find('.') != std::string::npos);
        std::vector<std::string> git_args = {
            "log", "-n", std::to_string(n), "--decorate=short", "--format=%h|%s|%an|%ar"};
        if (!ref.empty()) {
          if (path_limit) {
            git_args.push_back("--");
          }
          git_args.push_back(ref);
        }
        const GitCommandResult result = run_git(info.root, git_args);
                {
          std::ostringstream data;
          data << "{\"arg\":\"" << ai_trace_escape(arg, 120) << "\",\"ref\":\""
               << ai_trace_escape(ref, 64) << "\",\"n\":" << n
               << ",\"ok\":" << (result.success() ? "true" : "false") << ",\"lines\":"
               << std::count(result.stdout_text.begin(), result.stdout_text.end(), '\n') << "}";
          ai_trace(AiTraceChannel::Tool, "git_log_run", data.str());
        }
                if (!result.success()) {
          return AiToolResult{false, result.stderr_text.empty() ? "git log falló"
                                                                : result.stderr_text};
        }
        std::ostringstream out;
        if (ref.empty()) {
          out << "Últimos " << n << " commits (HEAD):\n";
        } else {
          out << "Últimos " << n << " commits en `" << ref << "`:\n";
        }
        std::istringstream lines(result.stdout_text);
        std::string line;
        int shown = 0;
        while (std::getline(lines, line)) {
          if (line.empty()) {
            continue;
          }
          // %h|%s|%an|%ar
          std::string hash, subject, author, relative;
          std::size_t p1 = line.find('|');
          std::size_t p2 = p1 == std::string::npos ? std::string::npos : line.find('|', p1 + 1);
          std::size_t p3 = p2 == std::string::npos ? std::string::npos : line.find('|', p2 + 1);
          if (p1 != std::string::npos && p2 != std::string::npos && p3 != std::string::npos) {
            hash = line.substr(0, p1);
            subject = line.substr(p1 + 1, p2 - p1 - 1);
            author = line.substr(p2 + 1, p3 - p2 - 1);
            relative = line.substr(p3 + 1);
            out << "  " << hash << "  " << subject << "  (" << author << ", " << relative << ")\n";
          } else {
            out << "  " << line << '\n';
          }
          ++shown;
        }
        if (shown == 0) {
          out << "(sin commits)\n";
        }
        return AiToolResult{true, out.str()};
      });

  registry->register_tool(
      "git_show",
      "Detalle de un commit: mensaje y archivos (arg: HEAD, HEAD~1, hash…)",
      [ctx](const std::string& arg) {
        if (ctx.git == nullptr) {
          return AiToolResult{false, "GitService no disponible"};
        }
        if (!ctx.git->is_repo()) {
          return AiToolResult{false, "no es un repositorio git"};
        }
        const auto info = ctx.git->context_repo_info();
        if (!info.valid || info.root.empty()) {
          return AiToolResult{false, "no hay repo git activo"};
        }
        std::string rev = "HEAD";
        bool want_patch = false;
        {
          std::istringstream iss(arg);
          std::string tok;
          bool got_rev = false;
          while (iss >> tok) {
            if (tok == "patch" || tok == "diff" || tok == "--patch" || tok == "-p") {
              want_patch = true;
              continue;
            }
            bool digits = !tok.empty();
            for (char c : tok) {
              if (!std::isdigit(static_cast<unsigned char>(c))) {
                digits = false;
                break;
              }
            }
            if (digits) {
              continue;
            }
            if (!got_rev) {
              rev = tok;
              got_rev = true;
            }
          }
        }
        const GitCommandResult meta =
            run_git(info.root, {"log", "-n", "1", "--format=%h|%s|%an|%ar", rev});
        GitCommandResult files =
            run_git(info.root, {"diff-tree", "--no-commit-id", "--name-status", "-r", rev});
        // Merges: diff-tree sin padre no lista archivos; usar primer padre.
        bool used_first_parent = false;
        if (files.success() && files.stdout_text.empty()) {
          const GitCommandResult fp =
              run_git(info.root, {"diff-tree", "--no-commit-id", "--name-status", "-r",
                                  rev + "^1", rev});
          if (fp.success() && !fp.stdout_text.empty()) {
            files = fp;
            used_first_parent = true;
          }
        }
        GitCommandResult patch;
        if (want_patch) {
          patch = run_git(info.root, {"show", "--format=", "--first-parent", "-p", "--stat", rev});
          if (!patch.success() || patch.stdout_text.empty()) {
            patch = run_git(info.root, {"diff", rev + "^1", rev});
            used_first_parent = true;
          }
        }
                {
          std::ostringstream data;
          data << "{\"arg\":\"" << ai_trace_escape(arg, 80) << "\",\"rev\":\""
               << ai_trace_escape(rev, 64) << "\",\"meta_ok\":"
               << (meta.success() ? "true" : "false")
               << ",\"files_ok\":" << (files.success() ? "true" : "false")
               << ",\"first_parent\":" << (used_first_parent ? "true" : "false")
               << ",\"want_patch\":" << (want_patch ? "true" : "false") << ",\"patch_lines\":"
               << std::count(patch.stdout_text.begin(), patch.stdout_text.end(), '\n')
               << ",\"file_lines\":"
               << std::count(files.stdout_text.begin(), files.stdout_text.end(), '\n') << "}";
          ai_trace(AiTraceChannel::Tool, "git_show_run", data.str());
        }
                if (!meta.success()) {
          return AiToolResult{false, meta.stderr_text.empty() ? "git show/log falló"
                                                              : meta.stderr_text};
        }
        std::ostringstream out;
        std::string meta_line = meta.stdout_text;
        while (!meta_line.empty() && (meta_line.back() == '\n' || meta_line.back() == '\r')) {
          meta_line.pop_back();
        }
        std::string hash, subject, author, relative;
        const auto p1 = meta_line.find('|');
        const auto p2 = p1 == std::string::npos ? std::string::npos : meta_line.find('|', p1 + 1);
        const auto p3 = p2 == std::string::npos ? std::string::npos : meta_line.find('|', p2 + 1);
        if (p1 != std::string::npos && p2 != std::string::npos && p3 != std::string::npos) {
          hash = meta_line.substr(0, p1);
          subject = meta_line.substr(p1 + 1, p2 - p1 - 1);
          author = meta_line.substr(p2 + 1, p3 - p2 - 1);
          relative = meta_line.substr(p3 + 1);
          out << "Commit " << hash << " (" << rev << "):\n";
          out << "  " << subject << "\n";
          out << "  " << author << ", " << relative << "\n";
          if (used_first_parent) {
            out << "  (merge: archivos vs primer padre)\n";
          }
        } else {
          out << "Commit (" << rev << "):\n  " << meta_line << "\n";
        }
        out << "Archivos:\n";
        if (!files.success() || files.stdout_text.empty()) {
          out << "  (sin archivos)\n";
        } else {
          const auto entries = parse_commit_name_status(files.stdout_text);
          if (entries.empty()) {
            out << files.stdout_text;
            if (!files.stdout_text.empty() && files.stdout_text.back() != '\n') {
              out << '\n';
            }
          } else {
            constexpr int kMax = 80;
            int n = 0;
            for (const auto& e : entries) {
              out << "  " << e.status << "  " << e.path << '\n';
              if (++n >= kMax) {
                out << "  … (" << (entries.size() - static_cast<std::size_t>(n))
                    << " más)\n";
                break;
              }
            }
          }
        }
        if (want_patch) {
          out << "\nDiff del commit:\n";
          if (!patch.success() || patch.stdout_text.empty()) {
            out << "(sin patch)\n";
          } else {
            std::istringstream iss(patch.stdout_text);
            std::string line;
            constexpr int kMaxPatchLines = 200;
            int n = 0;
            while (std::getline(iss, line)) {
              out << line << '\n';
              if (++n >= kMaxPatchLines) {
                out << "… (patch truncado)\n";
                break;
              }
            }
          }
        }
        return AiToolResult{true, out.str()};
      });

  registry->register_tool(
      "git_diff", "Muestra diffs de archivos modificados (arg opcional: prefijo ruta)",
      [ctx](const std::string& arg) {
        if (ctx.git == nullptr) {
          return AiToolResult{false, "GitService no disponible"};
        }
        if (!ctx.git->is_repo()) {
          return AiToolResult{false, "no es un repositorio git"};
        }
        const auto info = ctx.git->context_repo_info();
        if (!info.valid || info.root.empty()) {
          return AiToolResult{false, "no hay repo git activo"};
        }
        ctx.git->refresh_status();
        const auto snap = ctx.git->status();
        const auto filters = split_path_filters(arg);
        std::vector<std::string> matched;
        for (const auto& e : snap.entries) {
          if (!path_matches_filters(e.path, filters)) {
            continue;
          }
          // Evitar volcar árboles enteros de untracked dirs.
          if (!e.path.empty() && e.path.back() == '/') {
            continue;
          }
          matched.push_back(e.path);
        }
        std::ostringstream out;
        if (matched.empty()) {
          if (!filters.empty()) {
            std::string label = filters.front();
            for (std::size_t i = 1; i < filters.size(); ++i) {
              label += " | " + filters[i];
            }
            out << "No hay diffs bajo `" << label << "`.\n";
          } else {
            out << "No hay archivos con cambios para mostrar diff.\n";
          }
          return AiToolResult{true, out.str()};
        }
        out << "Diffs (" << matched.size() << " archivos";
        if (!filters.empty()) {
          std::string label = filters.front();
          for (std::size_t i = 1; i < filters.size(); ++i) {
            label += " | " + filters[i];
          }
          out << " bajo `" << label << "`";
        }
        out << "):\n";
        constexpr int kMaxFiles = 12;
        constexpr int kMaxLinesPerFile = 60;
        int shown = 0;
        for (const auto& path : matched) {
          if (shown >= kMaxFiles) {
            out << "… (" << (matched.size() - static_cast<std::size_t>(shown))
                << " archivos más omitidos)\n";
            break;
          }
          const GitCommandResult diff =
              run_git(info.root, {"diff", "HEAD", "--", path});
          out << "\n=== " << path << " ===\n";
          std::string body = diff.stdout_text;
          if (body.empty()) {
            const GitCommandResult untracked =
                run_git(info.root, {"diff", "--no-index", "/dev/null", path});
            body = untracked.stdout_text;
            if (body.empty()) {
              out << "(sin hunks de texto; binario o solo metadata)\n";
              ++shown;
              continue;
            }
          }
          std::istringstream iss(body);
          std::string line;
          int n = 0;
          while (std::getline(iss, line)) {
            out << line << '\n';
            if (++n >= kMaxLinesPerFile) {
              out << "… (diff truncado)\n";
              break;
            }
          }
          ++shown;
        }
        return AiToolResult{true, out.str()};
      });

  registry->register_tool("context_pack", "Arma ContextPack estructural (rg/TS/LSP)",
                          [ctx](const std::string& arg) {
                            ContextPackAssembler assembler(ctx.workspace, ctx.indexer,
                                                           ctx.symbols);
                            ContextPackOptions opts;
                            std::istringstream iss(arg);
                            std::string seed;
                            while (iss >> seed) {
                              opts.seeds.push_back(seed);
                            }
                            if (opts.seeds.empty() && ctx.workspace != nullptr) {
                              const auto& buf = ctx.workspace->buffer;
                              if (!buf.path.empty() && buf.primary_line() >= 0 &&
                                  buf.primary_line() < buf.lines.size()) {
                                const std::string& line = buf.lines[buf.primary_line()];
                                const int col =
                                    std::clamp(buf.primary_col(), 0, static_cast<int>(line.size()));
                                int a = col;
                                int b = col;
                                auto is_id = [](char c) {
                                  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
                                };
                                while (a > 0 && is_id(line[static_cast<std::size_t>(a - 1)])) {
                                  --a;
                                }
                                while (b < static_cast<int>(line.size()) &&
                                       is_id(line[static_cast<std::size_t>(b)])) {
                                  ++b;
                                }
                                if (b > a) {
                                  opts.seeds.push_back(line.substr(static_cast<std::size_t>(a),
                                                                   static_cast<std::size_t>(b - a)));
                                }
                              }
                            }
                            const ContextPack pack = assembler.assemble(opts);
                            return AiToolResult{true, pack.dump_text()};
                          });

  registry->register_tool(
      "repo_map",
      "Dump REPO_MAP (firmas + PageRank). Uso: /repomap [query] | /repomap status",
      [ctx](const std::string& arg) {
        if (ctx.symbol_indexer == nullptr) {
          return AiToolResult{false,
                              "sin SymbolWorkspaceIndexer (bug de cableado AI → indexer)"};
        }

        const std::string trimmed = trim_copy(arg);
        const bool want_status =
            trimmed.empty() || trimmed == "status" || trimmed == "?" || trimmed == "help";

        std::size_t done = 0;
        std::size_t total = 0;
        std::string current_file;
        ctx.symbol_indexer->progress(&done, &total, &current_file);
        const bool scanning = ctx.symbol_indexer->scanning();
        const auto snap = ctx.symbol_indexer->snapshot();
        const std::size_t n_syms = snap ? snap->symbols.size() : 0;
        const std::size_t n_refs = snap ? snap->refs.size() : 0;
        const bool partial = snap && snap->partial;
        const std::string root = snap ? snap->workspace_root : std::string{};
        std::size_t n_files = 0;
        if (snap) {
          std::unordered_set<std::string> uniq;
          for (const auto& s : snap->symbols) {
            if (!s.file.empty()) {
              uniq.insert(s.file);
            }
          }
          n_files = uniq.size();
        }

        auto status_block = [&]() {
          std::ostringstream st;
          st << "=== REPO_MAP status ===\n";
          st << "scanning: " << (scanning ? "YES" : "no")
             << (partial ? " (índice parcial publicado)" : "") << '\n';
          if (scanning || total > 0) {
            st << "progress: " << done << "/" << total << " source files\n";
          }
          if (!current_file.empty()) {
            st << "current: " << current_file << '\n';
          }
          st << "symbols: " << n_syms << "  refs: " << n_refs << "  files: " << n_files << '\n';
          st << "workspace: " << (root.empty() ? "(none)" : root) << '\n';
          if (scanning) {
            st << "\nEl índice tree-sitter (defs+refs) está calculándose en background "
                  "(hilo idx-syms).\n"
               << "Se publica cada ~20 archivos: /repomap ya puede usarse con un mapa parcial.\n"
               << "Vuelve a probar: /repomap status   o   /repomap <tema>\n";
          } else if (n_syms == 0) {
            st << "\nSnapshot vacío. Reabre el workspace o reinicia tuide.\n";
          } else {
            st << "\nListo. Prueba: /repomap panel performance\n";
            st << "Nota: src/ai/* suele estar untracked → no desaparece del mapa "
                  "(git es boost, no filtro duro).\n";
          }
          return st.str();
        };

        if (want_status && (trimmed == "status" || trimmed == "?" || trimmed == "help")) {
          return AiToolResult{true, status_block()};
        }

        if (scanning && n_syms == 0) {
          std::ostringstream out;
          out << "REPO_MAP: índice en curso (" << done << "/" << total
              << (current_file.empty() ? "" : ("; " + current_file))
              << "). Aún no hay mapa usable.\n"
              << "Consulta progreso: /repomap status\n";
          return AiToolResult{true, out.str()};
        }

        if (!snap || snap->symbols.empty()) {
          return AiToolResult{true, status_block()};
        }

        RepoMapOptions opts;
        opts.query = trimmed;
        opts.max_map_tokens = 2048;
        opts.max_symbols = 96;
        opts.max_chars = 6000;
        opts.prefer_git_tracked = true;
        opts.use_pagerank = true;

        if (ctx.workspace != nullptr) {
          if (!ctx.workspace->buffer.path.empty() && !ctx.workspace->root.empty()) {
            std::error_code ec;
            const auto rel =
                fs::relative(fs::path(ctx.workspace->buffer.path), fs::path(ctx.workspace->root),
                             ec);
            if (!ec && !rel.empty() && rel.native().rfind("..", 0) != 0) {
              opts.active_file = rel.generic_string();
            }
          }
          for (const auto& tab : ctx.workspace->tabs) {
            if (tab.path.empty() || tab.git_diff_view) {
              continue;
            }
            std::error_code ec;
            std::string rel = tab.path;
            if (!ctx.workspace->root.empty()) {
              const auto r =
                  fs::relative(fs::path(tab.path), fs::path(ctx.workspace->root), ec);
              if (!ec && !r.empty() && r.native().rfind("..", 0) != 0) {
                rel = r.generic_string();
              }
            }
            if (!rel.empty() && rel != opts.active_file) {
              opts.chat_files.push_back(rel);
            }
          }
        }
        if (opts.query.empty() && !opts.active_file.empty()) {
          // Prefer outline around the active file without stuffing the path as NL query
          // (that produced "índice sin símbolos útiles" when combined with a bad git filter).
          opts.query.clear();
        }

        const RepoMap map = build_repo_map(snap.get(), opts);
        std::ostringstream out;
        out << map.render_text();
        out << "\n--- meta ---\n";
        out << "entries=" << map.entries.size() << " best_score=" << map.best_score
            << " pagerank=" << (map.used_pagerank ? "yes" : "no") << '\n';
        out << "index: symbols=" << n_syms << " refs=" << n_refs << " files=" << n_files
            << (partial || scanning ? " partial=yes" : " partial=no") << '\n';
        if (!map.note.empty()) {
          out << "note: " << map.note << '\n';
        }
        out << "query: " << (opts.query.empty() ? "(none)" : opts.query) << '\n';
        if (!opts.active_file.empty()) {
          out << "active_file: " << opts.active_file << '\n';
        }
        const auto needles = map.suggested_needles(8);
        if (!needles.empty()) {
          out << "suggested_needles:";
          for (const auto& n : needles) {
            out << ' ' << n;
          }
          out << '\n';
        }
        return AiToolResult{true, out.str()};
      });

  registry->register_tool("apply_demo", "Apply simulado: inserta marca AI y pinta gutter azul",
                          [ctx](const std::string&) {
                            std::string detail;
                            const bool ok =
                                EditJournalStore::instance().apply_demo(ctx.workspace, &detail);
                            return AiToolResult{ok, detail};
                          });
}

}  // namespace tuide
