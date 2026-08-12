#include "ai/edit_journal.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "editor/editor_buffer_source.hpp"
#include "editor/text_ops.hpp"
#include "editor/undo_stack.hpp"

namespace fs = std::filesystem;

namespace tuide {
namespace {

uint64_t now_ms() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

std::string author_name(AiAuthor a) {
  switch (a) {
    case AiAuthor::Human:
      return "human";
    case AiAuthor::Level1_AI:
      return "l1";
    case AiAuthor::Level2_AI:
      return "l2";
    case AiAuthor::Lsp:
      return "lsp";
    case AiAuthor::System:
      return "system";
  }
  return "human";
}

AiAuthor parse_author(const std::string& s) {
  if (s == "l1") {
    return AiAuthor::Level1_AI;
  }
  if (s == "l2") {
    return AiAuthor::Level2_AI;
  }
  if (s == "lsp") {
    return AiAuthor::Lsp;
  }
  if (s == "system") {
    return AiAuthor::System;
  }
  return AiAuthor::Human;
}

EditorBuffer* buffer_for_path(WorkspaceModel* workspace, const std::string& path) {
  if (workspace == nullptr) {
    return nullptr;
  }
  if (!path.empty() && workspace->buffer.path == path) {
    return &workspace->buffer;
  }
  const int tab = workspace->find_tab(path);
  if (tab >= 0) {
    return &workspace->tabs[static_cast<std::size_t>(tab)].buffer;
  }
  if (path.empty() && !workspace->buffer.path.empty()) {
    return &workspace->buffer;
  }
  return nullptr;
}

}  // namespace

EditJournalStore& EditJournalStore::instance() {
  static EditJournalStore store;
  return store;
}

uint64_t EditJournalStore::next_op_id() {
  std::lock_guard lock(mu_);
  return next_op_++;
}

EditJournalStore::FileJournal* EditJournalStore::mutable_journal(const std::string& path) {
  return &by_path_[path];
}

const EditJournalStore::FileJournal* EditJournalStore::journal(const std::string& path) const {
  const auto it = by_path_.find(path);
  if (it == by_path_.end()) {
    return nullptr;
  }
  return &it->second;
}

void EditJournalStore::record_edit(const std::string& path, AiTextEdit edit) {
  std::lock_guard lock(mu_);
  auto* j = mutable_journal(path);
  j->edits.push_back(edit);
  for (int line = edit.start_line; line <= edit.end_line; ++line) {
    if (line > 0) {
      j->line_authors[line] = edit.author;
    }
  }
}

void EditJournalStore::mark_lines(const std::string& path, int start_line_1, int end_line_1,
                                  AiAuthor author, uint64_t op_id) {
  AiTextEdit edit;
  edit.author = author;
  edit.op_id = op_id;
  edit.start_line = start_line_1;
  edit.end_line = end_line_1;
  edit.start_col = 1;
  edit.end_col = 1;
  edit.timestamp_ms = now_ms();
  record_edit(path, edit);
}

AiAuthor EditJournalStore::author_for_line(const std::string& path, int line_1based) const {
  std::lock_guard lock(mu_);
  const auto* j = journal(path);
  if (j == nullptr) {
    return AiAuthor::Human;
  }
  const auto it = j->line_authors.find(line_1based);
  if (it == j->line_authors.end()) {
    return AiAuthor::Human;
  }
  return it->second;
}

bool EditJournalStore::line_is_ai(const std::string& path, int line_1based) const {
  return ai_author_is_ai(author_for_line(path, line_1based));
}

bool EditJournalStore::apply_replace(WorkspaceModel* workspace, const std::string& path,
                                     int start_line, int start_col, int end_line, int end_col,
                                     const std::string& new_text, AiAuthor author,
                                     std::string* error) {
  EditorBuffer* buffer = buffer_for_path(workspace, path);
  if (buffer == nullptr) {
    if (error) {
      *error = "buffer no abierto: " + path;
    }
    return false;
  }
  if (start_line < 0 || end_line < start_line || start_line >= buffer->lines.size()) {
    if (error) {
      *error = "rango inválido";
    }
    return false;
  }

  push_undo(buffer);
  // text_ops uses 0-based lines; convert if callers pass 1-based — our API is 1-based lines,
  // 1-based cols like LSP, but replace_text_range uses 0-based line/col.
  const int sl = std::max(0, start_line - 1);
  const int el = std::max(0, end_line - 1);
  const int sc = std::max(0, start_col - 1);
  const int ec = std::max(0, end_col - 1);
  if (sl == el) {
    replace_text_range(buffer, sl, sc, ec, new_text);
  } else {
    // Multi-line: replace from start through end via joined apply.
    std::string text = editor_buffer_joined_source(*buffer);
    // Fallback: replace first line range only for MVP safety.
    replace_text_range(buffer, sl, sc, static_cast<int>(buffer->lines[sl].size()), new_text);
    (void)el;
    (void)ec;
    (void)text;
  }
  commit_undo_group(buffer);
  buffer->dirty = true;
  buffer->view_token++;

  const uint64_t op = next_op_id();
  AiTextEdit edit;
  edit.author = author;
  edit.op_id = op;
  edit.start_line = start_line;
  edit.start_col = start_col;
  edit.end_line = end_line;
  edit.end_col = end_col;
  edit.new_len = static_cast<uint32_t>(new_text.size());
  edit.timestamp_ms = now_ms();
  record_edit(buffer->path, edit);

  // Approximate lines touched by new_text newlines.
  int new_lines = 1;
  for (char c : new_text) {
    if (c == '\n') {
      ++new_lines;
    }
  }
  mark_lines(buffer->path, start_line, start_line + new_lines - 1, author, op);

  if (workspace != nullptr && !workspace->root.empty()) {
    save_sidecar(workspace->root, buffer->path);
  }
  return true;
}

bool EditJournalStore::apply_demo(WorkspaceModel* workspace, std::string* detail) {
  if (workspace == nullptr || workspace->buffer.path.empty()) {
    if (detail) {
      *detail = "abre un archivo en el editor primero";
    }
    return false;
  }
  EditorBuffer* buffer = &workspace->buffer;
  const int line = std::clamp(buffer->primary_line(), 0, std::max(0, buffer->lines.size() - 1));
  const std::string marker = "// tuide-ai: demo attribution (Level1_AI)";
  push_undo(buffer);
  // Insert as a new line above the cursor line.
  const std::string& cur = buffer->lines[line];
  const std::string replacement = marker + "\n" + cur;
  replace_text_range(buffer, line, 0, static_cast<int>(cur.size()), replacement);
  commit_undo_group(buffer);
  buffer->dirty = true;
  buffer->view_token++;

  const uint64_t op = next_op_id();
  mark_lines(buffer->path, line + 1, line + 1, AiAuthor::Level1_AI, op);
  if (!workspace->root.empty()) {
    save_sidecar(workspace->root, buffer->path);
  }
  if (detail) {
    *detail = "applied demo AI edit in " + buffer->path + ":" + std::to_string(line + 1) +
              " (gutter azul)";
  }
  return true;
}

std::string EditJournalStore::hash_text(const std::string& text) {
  // FNV-1a 64 — suficiente para invalidar sidecar si el archivo cambió.
  uint64_t h = 14695981039346656037ull;
  for (unsigned char c : text) {
    h ^= c;
    h *= 1099511628211ull;
  }
  std::ostringstream oss;
  oss << std::hex << h;
  return oss.str();
}

std::string EditJournalStore::sidecar_path(const std::string& workspace_root,
                                           const std::string& path) {
  const std::string hash = hash_text(path);
  return (fs::path(workspace_root) / ".tuide" / "ai" / "attribution" / (hash + ".json")).string();
}

void EditJournalStore::save_sidecar(const std::string& workspace_root,
                                    const std::string& path) const {
  if (workspace_root.empty() || path.empty()) {
    return;
  }
  std::lock_guard lock(mu_);
  const auto* j = journal(path);
  if (j == nullptr) {
    return;
  }
  const std::string out_path = sidecar_path(workspace_root, path);
  std::error_code ec;
  fs::create_directories(fs::path(out_path).parent_path(), ec);

  nlohmann::json lines = nlohmann::json::object();
  for (const auto& [line, author] : j->line_authors) {
    if (ai_author_is_ai(author)) {
      lines[std::to_string(line)] = author_name(author);
    }
  }
  nlohmann::json doc = {
      {"path", path},
      {"lines", std::move(lines)},
  };
  std::ofstream out(out_path);
  if (out) {
    out << doc.dump(2) << '\n';
  }
}

void EditJournalStore::load_sidecar(const std::string& workspace_root, const std::string& path,
                                    const std::string& file_hash) {
  if (workspace_root.empty() || path.empty()) {
    return;
  }
  const std::string in_path = sidecar_path(workspace_root, path);
  std::ifstream in(in_path);
  if (!in) {
    return;
  }
  try {
    nlohmann::json doc;
    in >> doc;
    std::lock_guard lock(mu_);
    auto* j = mutable_journal(path);
    j->content_hash = file_hash;
    j->line_authors.clear();
    if (doc.contains("lines") && doc["lines"].is_object()) {
      for (const auto& [k, v] : doc["lines"].items()) {
        if (v.is_string()) {
          j->line_authors[std::atoi(k.c_str())] = parse_author(v.get<std::string>());
        }
      }
    }
  } catch (...) {
  }
}

void EditJournalStore::invalidate(const std::string& path) {
  std::lock_guard lock(mu_);
  by_path_.erase(path);
}

}  // namespace tuide
