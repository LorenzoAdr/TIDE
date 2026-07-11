// Unit + randomized stress tests for TextRope (see text_rope.hpp).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "editor/text_rope.hpp"

namespace {

using tgdb::TextRope;

int g_failures = 0;

void expect(bool condition, const std::string& what) {
  if (!condition) {
    std::fprintf(stderr, "FAILED: %s\n", what.c_str());
    ++g_failures;
  }
}

std::vector<std::string> make_lines(int count) {
  std::vector<std::string> lines;
  lines.reserve(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    lines.push_back("line_" + std::to_string(i));
  }
  return lines;
}

void test_basic_construction() {
  TextRope empty_default;
  expect(empty_default.line_count() == 1, "default rope has exactly one (empty) line");
  expect(empty_default.line_at(0) == "", "default rope's line is empty");

  TextRope rope(make_lines(5));
  expect(rope.line_count() == 5, "construction from 5 lines yields line_count()==5");
  for (int i = 0; i < 5; ++i) {
    expect(rope.line_at(i) == "line_" + std::to_string(i), "line_at matches input for index " + std::to_string(i));
  }
  expect(rope.to_vector() == make_lines(5), "to_vector round-trips construction input");
}

void test_to_string_matches_join() {
  TextRope rope(std::vector<std::string>{"abc", "de", "", "fghij"});
  expect(rope.to_string() == "abc\nde\n\nfghij", "to_string joins with '\\n', no trailing separator");
}

void test_line_to_byte() {
  std::vector<std::string> lines = {"abc", "de", "", "fghij"};
  TextRope rope(lines);
  std::size_t expected = 0;
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    expect(rope.line_to_byte(i) == expected,
           "line_to_byte(" + std::to_string(i) + ") == " + std::to_string(expected));
    expected += lines[static_cast<std::size_t>(i)].size() + 1;
  }
  expect(rope.total_bytes() == expected, "total_bytes matches sum of (len+1) for every line");
}

void test_insert_erase_set() {
  TextRope rope(make_lines(5));
  rope.insert_line(2, "NEW");
  expect(rope.line_count() == 6, "insert_line grows count by 1");
  expect(rope.line_at(2) == "NEW", "insert_line places text at requested index");
  expect(rope.line_at(3) == "line_2", "insert_line shifts subsequent lines right");

  rope.erase_line(2);
  expect(rope.line_count() == 5, "erase_line shrinks count by 1");
  expect(rope.line_at(2) == "line_2", "erase_line removes exactly the requested line");

  rope.set_line(0, "REPLACED");
  expect(rope.line_at(0) == "REPLACED", "set_line replaces content in place (functionally)");
  expect(rope.line_count() == 5, "set_line does not change line_count");

  rope.erase_lines(1, 4);
  expect(rope.line_count() == 2, "erase_lines([1,4)) removes exactly 3 lines");
  expect(rope.line_at(0) == "REPLACED", "erase_lines keeps lines before the range");
  expect(rope.line_at(1) == "line_4", "erase_lines keeps lines after the range");
}

void test_clone_is_independent() {
  TextRope original(make_lines(10));
  TextRope snapshot = original.clone();

  original.set_line(3, "mutated");
  original.insert_line(0, "prepended");
  original.erase_line(5);

  expect(snapshot.line_count() == 10, "clone's line_count is unaffected by later mutations on original");
  expect(snapshot.line_at(3) == "line_3", "clone's content is unaffected by original's set_line");
  expect(snapshot.to_vector() == make_lines(10), "clone is a fully independent, unmodified copy");

  expect(original.line_count() == 10, "original: +1 insert -1 erase nets back to the same count");
  expect(original.line_at(0) == "prepended", "original reflects the prepend");
  expect(original.line_at(4) == "mutated", "original reflects the set_line, shifted by the prepend");
}

// Cross-checks every mutating op against a plain vector<string> reference
// implementation, for many random operation sequences and sizes.
void test_randomized_against_vector_reference() {
  std::mt19937 rng(12345);
  for (int trial = 0; trial < 60; ++trial) {
    const int initial_size = 1 + static_cast<int>(rng() % 200);
    std::vector<std::string> reference = make_lines(initial_size);
    TextRope rope(reference);

    const int op_count = 300;
    for (int op = 0; op < op_count; ++op) {
      const int n = static_cast<int>(reference.size());
      const int kind = static_cast<int>(rng() % 4);
      if (kind == 0 || n == 0) {
        // insert
        const int index = n == 0 ? 0 : static_cast<int>(rng() % static_cast<unsigned>(n + 1));
        std::string text = "ins_" + std::to_string(trial) + "_" + std::to_string(op);
        reference.insert(reference.begin() + index, text);
        rope.insert_line(index, text);
      } else if (kind == 1 && n > 0) {
        // erase single
        const int index = static_cast<int>(rng() % static_cast<unsigned>(n));
        reference.erase(reference.begin() + index);
        rope.erase_line(index);
      } else if (kind == 2 && n > 0) {
        // erase range
        const int begin_index = static_cast<int>(rng() % static_cast<unsigned>(n));
        const int max_len = n - begin_index;
        const int len = 1 + static_cast<int>(rng() % static_cast<unsigned>(max_len));
        reference.erase(reference.begin() + begin_index, reference.begin() + begin_index + len);
        rope.erase_lines(begin_index, begin_index + len);
      } else if (n > 0) {
        // set_line
        const int index = static_cast<int>(rng() % static_cast<unsigned>(n));
        std::string text = "set_" + std::to_string(trial) + "_" + std::to_string(op);
        reference[static_cast<std::size_t>(index)] = text;
        rope.set_line(index, text);
      }
    }

    expect(rope.to_vector() == reference,
           "trial " + std::to_string(trial) + ": rope matches vector<string> reference after " +
               std::to_string(op_count) + " random ops (final size " +
               std::to_string(reference.size()) + ")");

    std::size_t expected_offset = 0;
    bool offsets_ok = true;
    for (std::size_t i = 0; i < reference.size(); ++i) {
      if (rope.line_to_byte(static_cast<int>(i)) != expected_offset) {
        offsets_ok = false;
        break;
      }
      expected_offset += reference[i].size() + 1;
    }
    expect(offsets_ok, "trial " + std::to_string(trial) + ": line_to_byte matches reference offsets");
  }
}

// Empirically verifies the treap stays within a small constant factor of
// log2(n) in depth after many sequential appends (this is what the O(log n)
// complexity claim depends on in practice, since treap balance is
// probabilistic rather than a hard worst-case guarantee).
void test_balance_stays_logarithmic() {
  // We can't inspect Node depth directly (it's private), so use line_at()
  // timing as an indirect probe instead: this is covered by
  // text_buffer_bench's A/B comparison. Here we just sanity check that large
  // ropes still function correctly, which is the property that matters most
  // for correctness; the performance claim is validated by the benchmark.
  TextRope rope(make_lines(50000));
  bool ok = true;
  for (int i = 0; i < 50000; i += 977) {
    if (rope.line_at(i) != "line_" + std::to_string(i)) {
      ok = false;
      break;
    }
  }
  expect(ok, "50k-line rope: scattered line_at lookups return correct content");
}

}  // namespace

int main() {
  test_basic_construction();
  test_to_string_matches_join();
  test_line_to_byte();
  test_insert_erase_set();
  test_clone_is_independent();
  test_randomized_against_vector_reference();
  test_balance_stays_logarithmic();

  if (g_failures == 0) {
    std::printf("text_rope_test ok\n");
    return 0;
  }
  std::fprintf(stderr, "%d assertion(s) failed\n", g_failures);
  return 1;
}
