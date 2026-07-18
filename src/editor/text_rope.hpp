#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace tuide {

// A persistent (immutable-node, copy-on-write) balanced binary tree whose
// leaves are whole lines of text -- a "rope of lines" rather than a general
// byte-rope. Internal nodes are augmented with the number of lines and total
// bytes (each line counted as size()+1, as if followed by '\n', matching the
// convention used by join_editor_lines/EditorJoinedSourceCache::line_starts)
// of their subtree, so both line-index and byte-offset lookups are O(log n).
//
// Rebalancing uses the classic weight-balanced tree (Adams' BB[alpha])
// join/split primitives: insert/erase-at-index are expressed as split + join,
// which keeps single-line and multi-line edits alike at O(log n), regardless
// of where in the document they happen -- this is the core property a
// std::vector<std::string> cannot offer (there, inserting/erasing near the
// start of a large document is O(remaining lines)).
//
// Nodes are shared via std::shared_ptr and never mutated in place, so
// clone() is O(1) (a persistent snapshot, used by undo/redo) and any
// mutation only reallocates the O(log n) nodes on the path it touches.
class TextRope {
 public:
  TextRope();
  explicit TextRope(std::vector<std::string> lines);

  int line_count() const;
  bool empty() const { return line_count() == 0; }

  const std::string& line_at(int index) const;

  void set_line(int index, std::string text);
  void insert_line(int index, std::string text);
  void erase_line(int index);
  void erase_lines(int begin_index, int end_index);  // [begin, end)
  void push_back(std::string text);
  void clear();
  void assign(std::vector<std::string> lines);

  // Byte offset (within the '\n'-joined document, no trailing separator)
  // where `line` begins. O(log n).
  std::size_t line_to_byte(int line) const;
  // Total document length in bytes if joined with '\n' (no trailing
  // separator). O(1).
  std::size_t total_bytes() const;

  std::string to_string() const;
  std::vector<std::string> to_vector() const;

  // O(1): shares the root; mutating either copy only unshares/reallocates
  // the nodes it actually touches (structural sharing / copy-on-write).
  TextRope clone() const { return *this; }

  struct Node;

  // In-order iterator over an explicit path stack (the classic technique
  // for iterating a tree without per-step re-descent from the root). Each
  // node is pushed and popped exactly once across a full traversal, so a
  // full scan (begin() to end()) is O(n) total -- i.e. O(1) *amortized* per
  // ++, same complexity class as a std::vector<std::string> iterator.
  //
  // This exists specifically because line_at(index) (random access by
  // index) is O(log n): a caller doing a *sequential* full-document scan
  // via repeated line_at(0), line_at(1), ... would otherwise pay O(n log n)
  // total instead of O(n) -- exactly the trap EditorText::begin()/end() (see
  // editor_text.hpp) must avoid for callers that iterate the whole buffer
  // (e.g. rebuilding the joined-source byte offsets after an edit).
  class const_iterator {
   public:
    const_iterator() = default;
    const std::string& operator*() const;
    const_iterator& operator++();
    bool operator==(const const_iterator& other) const;
    bool operator!=(const const_iterator& other) const { return !(*this == other); }

   private:
    friend class TextRope;
    std::vector<const Node*> stack_;
  };

  const_iterator begin() const;
  const_iterator end() const;
  // Like begin(), but starting the sequential scan at `index` instead of 0.
  // The descent to `index` is O(log n) (same as line_at), but from there
  // onward each ++ is O(1) amortized, same as begin()/end() -- this lets a
  // caller resume a full-document rebuild from a known point (e.g. "recompute
  // byte offsets for every line from here to the end") without paying
  // O((n - index) * log n) via repeated line_at() calls.
  const_iterator seek(int index) const;

 private:
  std::shared_ptr<const Node> root_;
};

}  // namespace tuide
