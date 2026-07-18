#include "editor/text_rope.hpp"

#include <cassert>
#include <utility>

namespace tuide {

// Implementation note: this is an "implicit treap" (a randomized balanced
// binary search tree ordered purely by position, not by key) rather than a
// deterministic weight-balanced tree. Treaps give the same O(log n) expected
// complexity for insert/erase/split/merge at an arbitrary index, but the
// split/merge primitives are a few lines of straightforward, well-known
// recursion with no rotation case analysis -- which makes them much easier
// to get right (and to verify with randomized tests) than hand-rolled
// AVL/weight-balanced rotations. Balance only depends on each node's random
// priority, not on insertion order, so appending N lines one at a time (as
// `assign()` does) produces the same expected depth as any other insertion
// order.
struct TextRope::Node {
  std::string text;
  std::shared_ptr<const Node> left;
  std::shared_ptr<const Node> right;
  uint64_t priority = 0;
  int size = 1;
  std::size_t bytes = 0;
};

namespace {

using NodePtr = std::shared_ptr<const TextRope::Node>;

int size_of(const NodePtr& n) { return n ? n->size : 0; }
std::size_t bytes_of(const NodePtr& n) { return n ? n->bytes : 0; }
std::size_t own_bytes(const std::string& text) { return text.size() + 1; }

uint64_t next_priority() {
  // splitmix64: fast, good-enough distribution for treap balance (this is
  // not a cryptographic RNG and doesn't need to be one).
  static thread_local uint64_t state = 0x9E3779B97F4A7C15ULL;
  state += 0x9E3779B97F4A7C15ULL;
  uint64_t z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

NodePtr make_node(std::string text, uint64_t priority, NodePtr left, NodePtr right) {
  auto node = std::make_shared<TextRope::Node>();
  node->priority = priority;
  node->size = 1 + size_of(left) + size_of(right);
  node->bytes = own_bytes(text) + bytes_of(left) + bytes_of(right);
  node->text = std::move(text);
  node->left = std::move(left);
  node->right = std::move(right);
  return node;
}

NodePtr make_leaf(std::string text) { return make_node(std::move(text), next_priority(), nullptr, nullptr); }

// Rebuilds `t` with a new left child (keeps t's own text/priority/right).
NodePtr with_left(const NodePtr& t, NodePtr new_left) {
  return make_node(t->text, t->priority, std::move(new_left), t->right);
}

// Rebuilds `t` with a new right child (keeps t's own text/priority/left).
NodePtr with_right(const NodePtr& t, NodePtr new_right) {
  return make_node(t->text, t->priority, t->left, std::move(new_right));
}

// All of `l`'s elements come before all of `r`'s elements in the result.
NodePtr merge(NodePtr l, NodePtr r) {
  if (!l) return r;
  if (!r) return l;
  if (l->priority > r->priority) {
    return with_right(l, merge(l->right, r));
  }
  return with_left(r, merge(l, r->left));
}

// Splits `t` into [0, k) and [k, size(t)).
std::pair<NodePtr, NodePtr> split(const NodePtr& t, int k) {
  if (!t) {
    return {nullptr, nullptr};
  }
  const int ls = size_of(t->left);
  if (k <= ls) {
    auto [a, b] = split(t->left, k);
    return {a, with_left(t, std::move(b))};
  }
  auto [a, b] = split(t->right, k - ls - 1);
  return {with_right(t, std::move(a)), b};
}

const std::string& node_line_at(const NodePtr& t, int index) {
  const int ls = size_of(t->left);
  if (index < ls) {
    return node_line_at(t->left, index);
  }
  if (index == ls) {
    return t->text;
  }
  return node_line_at(t->right, index - ls - 1);
}

NodePtr node_set_line(const NodePtr& t, int index, std::string text) {
  const int ls = size_of(t->left);
  if (index < ls) {
    return with_left(t, node_set_line(t->left, index, std::move(text)));
  }
  if (index == ls) {
    return make_node(std::move(text), t->priority, t->left, t->right);
  }
  return with_right(t, node_set_line(t->right, index - ls - 1, std::move(text)));
}

// Bytes represented by the first k elements of subtree t (own_bytes summed).
std::size_t node_prefix_bytes(const NodePtr& t, int k) {
  if (!t || k <= 0) {
    return 0;
  }
  const int ls = size_of(t->left);
  if (k <= ls) {
    return node_prefix_bytes(t->left, k);
  }
  const std::size_t through_self = bytes_of(t->left) + own_bytes(t->text);
  if (k == ls + 1) {
    return through_self;
  }
  return through_self + node_prefix_bytes(t->right, k - ls - 1);
}

void push_left_spine(std::vector<const TextRope::Node*>* stack, const TextRope::Node* node) {
  while (node != nullptr) {
    stack->push_back(node);
    node = node->left.get();
  }
}

// Builds the iterator stack for a sequential scan starting at `index`,
// without visiting (or paying for) anything before it. Nodes are pushed
// only when the search descends into their *left* child (mirroring
// push_left_spine's invariant that the stack always holds exactly the
// ancestors still owed a "finish visiting your right subtree" step);
// descending right never pushes, since that subtree's predecessors are
// already behind `index` and must not be revisited.
void seek_stack(std::vector<const TextRope::Node*>* stack, const TextRope::Node* node, int index) {
  while (node != nullptr) {
    const int ls = size_of(node->left);
    if (index < ls) {
      stack->push_back(node);
      node = node->left.get();
    } else if (index == ls) {
      stack->push_back(node);
      return;
    } else {
      node = node->right.get();
      index -= ls + 1;
    }
  }
}

void node_collect(const NodePtr& t, std::vector<std::string>* out) {
  if (!t) {
    return;
  }
  node_collect(t->left, out);
  out->push_back(t->text);
  node_collect(t->right, out);
}

}  // namespace

TextRope::TextRope() : root_(make_leaf("")) {}

TextRope::TextRope(std::vector<std::string> lines) { assign(std::move(lines)); }

int TextRope::line_count() const { return size_of(root_); }

const std::string& TextRope::line_at(int index) const { return node_line_at(root_, index); }

void TextRope::set_line(int index, std::string text) {
  root_ = node_set_line(root_, index, std::move(text));
}

void TextRope::insert_line(int index, std::string text) {
  auto [left, right] = split(root_, index);
  root_ = merge(merge(std::move(left), make_leaf(std::move(text))), std::move(right));
}

void TextRope::erase_line(int index) { erase_lines(index, index + 1); }

void TextRope::erase_lines(int begin_index, int end_index) {
  if (end_index <= begin_index) {
    return;
  }
  auto [left, rest] = split(root_, begin_index);
  auto [mid, right] = split(rest, end_index - begin_index);
  (void)mid;
  root_ = merge(std::move(left), std::move(right));
}

void TextRope::push_back(std::string text) { insert_line(line_count(), std::move(text)); }

void TextRope::clear() { root_ = nullptr; }

void TextRope::assign(std::vector<std::string> lines) {
  root_ = nullptr;
  for (auto& line : lines) {
    push_back(std::move(line));
  }
  if (root_ == nullptr) {
    root_ = make_leaf("");
  }
}

std::size_t TextRope::line_to_byte(int line) const { return node_prefix_bytes(root_, line); }

std::size_t TextRope::total_bytes() const { return bytes_of(root_); }

const std::string& TextRope::const_iterator::operator*() const { return stack_.back()->text; }

TextRope::const_iterator& TextRope::const_iterator::operator++() {
  const Node* node = stack_.back();
  stack_.pop_back();
  push_left_spine(&stack_, node->right.get());
  return *this;
}

bool TextRope::const_iterator::operator==(const const_iterator& other) const {
  return stack_ == other.stack_;
}

TextRope::const_iterator TextRope::begin() const {
  const_iterator it;
  push_left_spine(&it.stack_, root_.get());
  return it;
}

TextRope::const_iterator TextRope::end() const { return const_iterator(); }

TextRope::const_iterator TextRope::seek(int index) const {
  const_iterator it;
  if (index < line_count()) {
    seek_stack(&it.stack_, root_.get(), index);
  }
  return it;
}

std::string TextRope::to_string() const {
  // Single in-order pass via the O(1)-amortized iterator, straight into the
  // output buffer -- as opposed to going through to_vector() first, which
  // would copy every line into a temporary vector<string> before this loop
  // even starts (an extra full-document copy for no benefit here).
  std::string out;
  out.reserve(total_bytes());
  bool first = true;
  for (auto it = begin(); it != end(); ++it) {
    if (!first) {
      out.push_back('\n');
    }
    first = false;
    out += *it;
  }
  return out;
}

std::vector<std::string> TextRope::to_vector() const {
  std::vector<std::string> out;
  out.reserve(static_cast<std::size_t>(line_count()));
  node_collect(root_, &out);
  return out;
}

}  // namespace tuide
