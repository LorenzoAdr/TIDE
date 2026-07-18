// Standalone microbenchmark for EditorBuffer text storage.
//
// This is the "before/after" reference harness for the vector<string> -> rope
// migration (see the migration plan). It exercises EditorBuffer/text_ops.cpp
// directly, with no UI, no LSP and no tree-sitter involved, so the numbers
// only reflect the cost of the text storage + text_ops.cpp mutation paths.
//
// Usage: text_buffer_bench [sizes...]   (defaults to 1000 10000 100000)
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "editor/editor_state.hpp"
#include "editor/text_ops.hpp"
#include "editor/text_rope.hpp"
#include "editor/undo_stack.hpp"

namespace tuide {

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string synthetic_line(int index) {
  return "int value_" + std::to_string(index) + " = " + std::to_string(index * 7) +
         ";  // synthetic benchmark line";
}

EditorBuffer make_buffer(int line_count) {
  EditorBuffer buffer;
  std::vector<std::string> lines;
  lines.reserve(static_cast<std::size_t>(line_count));
  for (int i = 0; i < line_count; ++i) {
    lines.push_back(synthetic_line(i));
  }
  buffer.lines.assign(std::move(lines));
  buffer.ensure_cursors();
  return buffer;
}

struct BenchResult {
  std::string name;
  double total_ms = 0.0;
  int iterations = 0;
  double per_op_us() const {
    return iterations > 0 ? (total_ms * 1000.0) / static_cast<double>(iterations) : 0.0;
  }
};

void report(const BenchResult& r) {
  std::printf("  %-32s total=%8.2f ms  iters=%6d  per-op=%9.3f us\n", r.name.c_str(), r.total_ms,
              r.iterations, r.per_op_us());
}

// Type kIterations characters sequentially at the given line/col, one at a time
// (as a real keystroke stream would), and report total elapsed time.
BenchResult bench_type_sequential(int line_count, int at_line, int iterations) {
  EditorBuffer buffer = make_buffer(line_count);
  buffer.reset_to_single_cursor(at_line, 0);
  clear_undo(&buffer);
  buffer.undo_coalesce_open = true;  // rule out push_undo's one-shot full snapshot

  const auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    insert_char(&buffer, 'x');
  }
  BenchResult result;
  result.name = "type_sequential(@line " + std::to_string(at_line) + ")";
  result.total_ms = ms_since(start);
  result.iterations = iterations;
  return result;
}

// Repeatedly press Enter at a fixed line (as if holding the key), which is the
// scenario that originally caused full-file TS/LSP recomputation (see the
// "borrar y todas las siguientes se quedan en blanco" / CPU spike investigations).
BenchResult bench_repeated_enter(int line_count, int at_line, int iterations) {
  EditorBuffer buffer = make_buffer(line_count);
  buffer.reset_to_single_cursor(at_line, 0);
  clear_undo(&buffer);

  const auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    newline(&buffer);
  }
  BenchResult result;
  result.name = "repeated_enter(@line " + std::to_string(at_line) + ")";
  result.total_ms = ms_since(start);
  result.iterations = iterations;
  return result;
}

// Backspace at the start of a line repeatedly, joining lines together, near
// the start of a large file (worst case for a vector<string> shift).
BenchResult bench_backspace_join(int line_count, int at_line, int iterations) {
  EditorBuffer buffer = make_buffer(line_count);
  iterations = std::min(iterations, line_count - 2);
  buffer.reset_to_single_cursor(at_line, 0);
  clear_undo(&buffer);

  const auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    backspace(&buffer);
  }
  BenchResult result;
  result.name = "backspace_join(@line " + std::to_string(at_line) + ")";
  result.total_ms = ms_since(start);
  result.iterations = iterations;
  return result;
}

// Delete a multi-line range (e.g. selecting 20 lines and pressing delete),
// repeated, always removing lines right after a fixed start point.
BenchResult bench_multiline_delete(int line_count, int at_line, int span, int iterations) {
  EditorBuffer buffer = make_buffer(line_count);
  iterations = std::min(iterations, (line_count - at_line - 1) / std::max(1, span));
  clear_undo(&buffer);

  const auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    buffer.reset_to_single_cursor(at_line, 0);
    buffer.primary().anchor = {at_line, 0};
    buffer.primary().head = {at_line + span, 0};
    delete_all_selections(&buffer);
  }
  BenchResult result;
  result.name = "multiline_delete(span=" + std::to_string(span) + ")";
  result.total_ms = ms_since(start);
  result.iterations = iterations;
  return result;
}

// push_undo cost in isolation (this is a full buffer snapshot today).
BenchResult bench_push_undo(int line_count, int iterations) {
  EditorBuffer buffer = make_buffer(line_count);
  clear_undo(&buffer);

  const auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    buffer.undo_coalesce_open = false;
    push_undo(&buffer);
  }
  BenchResult result;
  result.name = "push_undo";
  result.total_ms = ms_since(start);
  result.iterations = iterations;
  return result;
}

// Diagnostic-only: isolates the cost of the incremental joined-source cache
// maintenance (editor_buffer_note_char_inserted / recompute_line_starts_from)
// from everything else insert_char() does, by calling the low-level line
// mutation + cache note directly, without touching cursors/undo/auto-pairs.
BenchResult bench_raw_char_insert_with_cache(int line_count, int at_line, int iterations) {
  EditorBuffer buffer = make_buffer(line_count);
  editor_buffer_rebuild_joined(&buffer);  // force the cache to be valid up front
  int col = 0;

  const auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    std::string line = buffer.lines[static_cast<std::size_t>(at_line)];
    line.insert(static_cast<std::size_t>(col), 1, 'x');
    buffer.lines.set_line(at_line, std::move(line));
    editor_buffer_note_char_inserted(&buffer, at_line, col, std::string_view("x", 1));
    ++col;
  }
  BenchResult result;
  result.name = "raw_char_insert+cache(@line " + std::to_string(at_line) + ")";
  result.total_ms = ms_since(start);
  result.iterations = iterations;
  return result;
}

// Same as above but WITHOUT touching the joined-source cache at all (pure
// vector<string> line mutation cost).
BenchResult bench_raw_char_insert_no_cache(int line_count, int at_line, int iterations) {
  EditorBuffer buffer = make_buffer(line_count);
  int col = 0;

  const auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    std::string line = buffer.lines[static_cast<std::size_t>(at_line)];
    line.insert(static_cast<std::size_t>(col), 1, 'x');
    buffer.lines.set_line(at_line, std::move(line));
    ++col;
  }
  BenchResult result;
  result.name = "raw_char_insert_no_cache(@line " + std::to_string(at_line) + ")";
  result.total_ms = ms_since(start);
  result.iterations = iterations;
  return result;
}

// --- Pure storage backend comparison (vector<string> vs TextRope) ---------
// These bypass text_ops.cpp/cursor_in_code/the joined-source cache entirely,
// to isolate exactly the cost this migration targets: inserting/erasing a
// line at a given position in a large document.

BenchResult bench_vector_insert_at(int line_count, int at_line, int iterations) {
  std::vector<std::string> lines;
  lines.reserve(static_cast<std::size_t>(line_count));
  for (int i = 0; i < line_count; ++i) {
    lines.push_back(synthetic_line(i));
  }
  const auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    lines.insert(lines.begin() + at_line, "inserted_" + std::to_string(i));
  }
  BenchResult result;
  result.name = "vector::insert(@" + std::to_string(at_line) + ")";
  result.total_ms = ms_since(start);
  result.iterations = iterations;
  return result;
}

BenchResult bench_rope_insert_at(int line_count, int at_line, int iterations) {
  tuide::TextRope rope;
  {
    std::vector<std::string> lines;
    lines.reserve(static_cast<std::size_t>(line_count));
    for (int i = 0; i < line_count; ++i) {
      lines.push_back(synthetic_line(i));
    }
    rope.assign(std::move(lines));
  }
  const auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    rope.insert_line(at_line, "inserted_" + std::to_string(i));
  }
  BenchResult result;
  result.name = "rope::insert_line(@" + std::to_string(at_line) + ")";
  result.total_ms = ms_since(start);
  result.iterations = iterations;
  return result;
}

BenchResult bench_vector_erase_at(int line_count, int at_line, int iterations) {
  std::vector<std::string> lines;
  lines.reserve(static_cast<std::size_t>(line_count + iterations));
  for (int i = 0; i < line_count + iterations; ++i) {
    lines.push_back(synthetic_line(i));
  }
  const auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    lines.erase(lines.begin() + at_line);
  }
  BenchResult result;
  result.name = "vector::erase(@" + std::to_string(at_line) + ")";
  result.total_ms = ms_since(start);
  result.iterations = iterations;
  return result;
}

BenchResult bench_rope_erase_at(int line_count, int at_line, int iterations) {
  tuide::TextRope rope;
  {
    std::vector<std::string> lines;
    lines.reserve(static_cast<std::size_t>(line_count + iterations));
    for (int i = 0; i < line_count + iterations; ++i) {
      lines.push_back(synthetic_line(i));
    }
    rope.assign(std::move(lines));
  }
  const auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    rope.erase_line(at_line);
  }
  BenchResult result;
  result.name = "rope::erase_line(@" + std::to_string(at_line) + ")";
  result.total_ms = ms_since(start);
  result.iterations = iterations;
  return result;
}

BenchResult bench_vector_erase_range(int line_count, int at_line, int span, int iterations) {
  std::vector<std::string> lines;
  lines.reserve(static_cast<std::size_t>(line_count));
  for (int i = 0; i < line_count; ++i) {
    lines.push_back(synthetic_line(i));
  }
  iterations = std::min(iterations, (line_count - at_line - 1) / std::max(1, span));
  const auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    lines.erase(lines.begin() + at_line, lines.begin() + at_line + span);
    for (int j = 0; j < span; ++j) {
      lines.insert(lines.begin() + at_line + j, synthetic_line(1000000 + i * span + j));
    }
  }
  BenchResult result;
  result.name = "vector::erase+insert range(@" + std::to_string(at_line) + ", span=" +
               std::to_string(span) + ")";
  result.total_ms = ms_since(start);
  result.iterations = iterations;
  return result;
}

BenchResult bench_rope_erase_range(int line_count, int at_line, int span, int iterations) {
  std::vector<std::string> lines;
  lines.reserve(static_cast<std::size_t>(line_count));
  for (int i = 0; i < line_count; ++i) {
    lines.push_back(synthetic_line(i));
  }
  tuide::TextRope rope(std::move(lines));
  iterations = std::min(iterations, (line_count - at_line - 1) / std::max(1, span));
  const auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    rope.erase_lines(at_line, at_line + span);
    for (int j = 0; j < span; ++j) {
      rope.insert_line(at_line + j, synthetic_line(1000000 + i * span + j));
    }
  }
  BenchResult result;
  result.name = "rope::erase_lines+insert_line(@" + std::to_string(at_line) + ", span=" +
               std::to_string(span) + ")";
  result.total_ms = ms_since(start);
  result.iterations = iterations;
  return result;
}

BenchResult bench_vector_full_clone(int line_count, int iterations) {
  std::vector<std::string> lines;
  lines.reserve(static_cast<std::size_t>(line_count));
  for (int i = 0; i < line_count; ++i) {
    lines.push_back(synthetic_line(i));
  }
  const auto start = Clock::now();
  volatile std::size_t sink = 0;
  for (int i = 0; i < iterations; ++i) {
    std::vector<std::string> snapshot = lines;  // what push_undo does today
    sink += snapshot.size();
  }
  BenchResult result;
  result.name = "vector full copy (undo snapshot)";
  result.total_ms = ms_since(start);
  result.iterations = iterations;
  return result;
}

BenchResult bench_rope_full_clone(int line_count, int iterations) {
  std::vector<std::string> lines;
  lines.reserve(static_cast<std::size_t>(line_count));
  for (int i = 0; i < line_count; ++i) {
    lines.push_back(synthetic_line(i));
  }
  tuide::TextRope rope(std::move(lines));
  const auto start = Clock::now();
  volatile int sink = 0;
  for (int i = 0; i < iterations; ++i) {
    tuide::TextRope snapshot = rope.clone();  // O(1): shares the root
    sink += snapshot.line_count();
  }
  BenchResult result;
  result.name = "rope clone() (undo snapshot)";
  result.total_ms = ms_since(start);
  result.iterations = iterations;
  return result;
}

void run_storage_comparison(int line_count) {
  std::printf("\n--- storage backend comparison: %d lines ---\n", line_count);
  const int mid_line = line_count / 2;
  report(bench_vector_insert_at(line_count, 0, 200));
  report(bench_rope_insert_at(line_count, 0, 200));
  report(bench_vector_insert_at(line_count, mid_line, 200));
  report(bench_rope_insert_at(line_count, mid_line, 200));
  report(bench_vector_erase_at(line_count, 0, 200));
  report(bench_rope_erase_at(line_count, 0, 200));
  report(bench_vector_erase_range(line_count, 0, 20, 100));
  report(bench_rope_erase_range(line_count, 0, 20, 100));
  report(bench_vector_full_clone(line_count, 100));
  report(bench_rope_full_clone(line_count, 100));
}

void run_for_size(int line_count) {
  std::printf("\n=== document size: %d lines ===\n", line_count);
  const int mid_line = line_count / 2;
  const int last_line = line_count - 1;

  report(bench_raw_char_insert_no_cache(line_count, 0, 500));
  report(bench_raw_char_insert_no_cache(line_count, last_line, 500));
  report(bench_raw_char_insert_with_cache(line_count, 0, 500));
  report(bench_raw_char_insert_with_cache(line_count, last_line, 500));

  report(bench_type_sequential(line_count, 0, 500));
  report(bench_type_sequential(line_count, mid_line, 500));
  report(bench_type_sequential(line_count, last_line, 500));

  report(bench_repeated_enter(line_count, 0, 300));
  report(bench_repeated_enter(line_count, mid_line, 300));

  report(bench_backspace_join(line_count, std::min(2, last_line), 200));
  report(bench_backspace_join(line_count, mid_line, 200));

  report(bench_multiline_delete(line_count, 0, 20, 100));
  report(bench_multiline_delete(line_count, mid_line, 20, 100));

  report(bench_push_undo(line_count, 200));
}

}  // namespace

}  // namespace tuide

int main(int argc, char** argv) {
  std::vector<int> sizes;
  for (int i = 1; i < argc; ++i) {
    sizes.push_back(std::atoi(argv[i]));
  }
  if (sizes.empty()) {
    sizes = {1000, 10000, 100000};
  }

  std::printf("tuide text_buffer_bench -- EditorText backend=rope (vector<string> retired, Fase 5)\n");

  for (int size : sizes) {
    tuide::run_for_size(size);
  }
  for (int size : sizes) {
    tuide::run_storage_comparison(size);
  }
  return 0;
}
