#include "util/tabular_file.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

bool wait_ready(tuide::TabularFileStore& store, int max_ticks = 300) {
  for (int i = 0; i < max_ticks; ++i) {
    if (store.ready()) {
      return true;
    }
    if (store.state() == tuide::TabularFileState::kError) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

std::string make_wide_row_temp_file(int row_count, std::size_t payload_bytes) {
  const std::string path = "/tmp/tuide_tabular_wide_test.csv";
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << "id,data\n";
  const std::string payload(payload_bytes, 'x');
  for (int i = 0; i < row_count; ++i) {
    output << i << ',' << payload << '\n';
  }
  return path;
}

}  // namespace

int main() {
  {
    tuide::TabularFileStore store;
    store.open_async("/home/lorenzo/workspace/tgdb/tests/test_2.tsv");

    expect(wait_ready(store), "tabular open should complete quickly");

    const int initial_rows = store.row_count();
    expect(initial_rows == 1001, "initial chunk should load header + 1000 data rows");
    expect(store.has_more(), "test_2.tsv should have more rows after first chunk");
    expect(store.max_data_total() == 1000, "scroll range should match first chunk");

    const std::string header = store.row_at(0);
    expect(header.find("sensor1") != std::string::npos, "header contains sensor1");
    expect(header.find("SENSOR_003") != std::string::npos, "header contains SENSOR_003");

    const std::string first_data = store.row_at(1);
    expect(first_data.find('\t') != std::string::npos, "data row uses tabs");
    expect(first_data.find("40.7936") != std::string::npos, "first value preserved");

    expect(store.request_load_at_end(950, 20), "request load near end of first chunk");

    for (int i = 0; i < 300; ++i) {
      if (store.row_count() > initial_rows) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    expect(store.row_count() > initial_rows, "next chunk should load on demand");

    store.ensure_viewport(0, 20);
    const auto cells = tuide::parse_tabular_row(first_data, store.delimiter());
    expect(cells.size() >= 100, "row should have many columns");

    std::cout << "tabular_file_test: OK (" << store.row_count() << " rows loaded, " << cells.size()
              << " cols)\n";
  }

  {
    const std::string wide_path = make_wide_row_temp_file(60, 200 * 1024);
    tuide::TabularFileStore store;
    const auto started = std::chrono::steady_clock::now();
    store.open_async(wide_path);
    expect(wait_ready(store), "wide-row tabular open should complete quickly");
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count();
    expect(elapsed_ms < 2000, "wide-row open should not scan the whole file");
    expect(store.row_count() < 61, "wide-row open should stop after byte budget");
    expect(store.has_more(), "wide-row file should still have more rows");
    expect(!store.row_at(0).empty(), "wide-row header should be available");
    std::cout << "tabular_file_test wide rows: OK (" << store.row_count()
              << " rows in " << elapsed_ms << " ms)\n";
  }

  return 0;
}
