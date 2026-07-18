#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

#include "editor/text_rope.hpp"
#include "util/line_source.hpp"

namespace tuide {

// Line-oriented text storage for EditorBuffer, backed by a TextRope (see
// text_rope.hpp) behind a small, stable API so that:
//  - the ~250 read-only call sites across the codebase (rendering, search,
//    diagnostics...) keep using the same `buffer.lines[i]` / `.size()` /
//    range-for syntax unchanged, via operator[]/size()/begin()/end(), and
//  - the handful of mutation call sites in text_ops.cpp/helix_dispatch.cpp
//    go through explicit set_line/insert_line/erase_line(s) instead of
//    mutating a std::string& in place -- TextRope's nodes are
//    immutable/shared (for O(1) clone()), so in-place mutation isn't
//    supported.
//
// This used to also support a std::vector<std::string> backend, selectable
// at runtime via the TUIDE_TEXT_BACKEND env var, to A/B the rope against the
// original implementation while the migration was being verified (see the
// "Fase 2-4" migration plan). That backend has been retired (Fase 5) now
// that the rope has been verified to be a strict improvement for editing
// (O(log n) insert/erase/undo vs O(n)); EditorText is now just a thin,
// non-polymorphic wrapper so the class still exists as the one stable
// surface the rest of the codebase depends on.
class EditorText : public LineSource {
 public:
  EditorText() = default;
  explicit EditorText(std::vector<std::string> lines) : rope_(std::move(lines)) {}

  int size() const override { return rope_.line_count(); }
  bool empty() const { return size() == 0; }

  const std::string& at(int index) const override { return rope_.line_at(index); }
  const std::string& operator[](std::size_t index) const { return at(static_cast<int>(index)); }
  const std::string& front() const { return at(0); }
  const std::string& back() const { return at(size() - 1); }

  void set_line(int index, std::string text) { rope_.set_line(index, std::move(text)); }
  void insert_line(int index, std::string text) { rope_.insert_line(index, std::move(text)); }
  void erase_line(int index) { rope_.erase_line(index); }
  void erase_lines(int begin_index, int end_index) { rope_.erase_lines(begin_index, end_index); }
  void push_back(std::string text) { rope_.push_back(std::move(text)); }
  void emplace_back(std::string text) { push_back(std::move(text)); }
  void clear() { rope_.clear(); }
  void assign(std::vector<std::string> lines) { rope_.assign(std::move(lines)); }

  // Convenience for tests/call sites that build a buffer's contents from a
  // literal list of lines (e.g. `buffer.lines = {"foo", "bar"};`).
  EditorText& operator=(std::initializer_list<std::string> lines) {
    assign(std::vector<std::string>(lines));
    return *this;
  }

  std::vector<std::string> to_vector() const { return rope_.to_vector(); }
  std::string to_string() const { return rope_.to_string(); }

  // O(1): structural sharing means clone() just copies a pointer to the
  // root. Used by undo/redo snapshots.
  EditorText clone() const {
    EditorText copy;
    copy.rope_ = rope_.clone();
    return copy;
  }

  // A sequential full-buffer scan (range-for, std::find_if, etc.) must stay
  // O(n). Walking via repeated at(index) would be O(n log n) instead -- see
  // TextRope::const_iterator's comment -- so this wraps the rope's own
  // proper linear iterator rather than re-deriving positions from an index.
  using const_iterator = TextRope::const_iterator;

  const_iterator begin() const { return rope_.begin(); }
  const_iterator end() const { return rope_.end(); }
  // See TextRope::seek(): resumes a sequential O(1)-amortized-per-step scan
  // from `index` instead of from the start, without paying an O(n) or
  // O(n log n) cost to skip the prefix.
  const_iterator seek(int index) const { return rope_.seek(index); }

 private:
  TextRope rope_;
};

}  // namespace tuide
