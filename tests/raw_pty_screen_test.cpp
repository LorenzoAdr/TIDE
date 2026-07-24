#include "terminal/raw_pty_screen.hpp"

#include <iostream>
#include <string>

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void test_basic_ansi_colors() {
  tuide::RawPtyScreen screen(4, 40);
  const std::string input = "\033[32mgreen\033[0m plain";
  screen.feed(input.data(), input.size());
  const auto rows = screen.styled_rows();
  expect(!rows.empty(), "basic rows");
  const auto& row = rows.back();
  expect(row.size() >= 2, "basic spans");
  expect(row[0].text == "green", "basic green text");
  expect(row[0].fg == ftxui::Color::RGB(80, 200, 80), "basic green fg");
  expect(row[1].text.find("plain") != std::string::npos, "basic plain text");
}

void test_256_color_prompt() {
  // Mirrors a typical bash PS1 fragment: \033[38;5;166muser\033[0m \033[38;5;2m/path
  tuide::RawPtyScreen screen(4, 80);
  const std::string input =
      "[\033[38;5;166muser\033[0m \033[38;5;2m/home/proj\033[0m] ";
  screen.feed(input.data(), input.size());
  const auto rows = screen.styled_rows();
  expect(!rows.empty(), "256 rows");
  const auto& row = rows.back();

  bool found_user = false;
  bool found_path = false;
  for (const auto& span : row) {
    if (span.text == "user") {
      found_user = true;
      expect(span.fg == ftxui::Color::RGB(215, 95, 0), "256 user orange");
    }
    if (span.text == "/home/proj") {
      found_path = true;
      expect(span.fg == ftxui::Color::RGB(80, 200, 80), "256 path green");
    }
  }
  expect(found_user, "256 found user span");
  expect(found_path, "256 found path span");
}

void test_truecolor() {
  tuide::RawPtyScreen screen(4, 40);
  const std::string input = "\033[38;2;10;20;30mrgb\033[0m";
  screen.feed(input.data(), input.size());
  const auto rows = screen.styled_rows();
  expect(!rows.empty(), "truecolor rows");
  const auto& row = rows.back();
  expect(!row.empty(), "truecolor spans");
  expect(row[0].text == "rgb", "truecolor text");
  expect(row[0].fg == ftxui::Color::RGB(10, 20, 30), "truecolor fg");
}

void test_256_background() {
  tuide::RawPtyScreen screen(4, 40);
  const std::string input = "\033[48;5;196mx\033[0m";
  screen.feed(input.data(), input.size());
  const auto rows = screen.styled_rows();
  expect(!rows.empty() && !rows.back().empty(), "bg rows");
  expect(rows.back()[0].bg == ftxui::Color::RGB(255, 0, 0), "256 bg red");
  expect(!rows.back()[0].bg_default, "256 bg not default");
}

void test_default_background_flag() {
  tuide::RawPtyScreen screen(4, 40);
  const std::string input = "\033[48;5;196mx\033[49my\033[0mz";
  screen.feed(input.data(), input.size());
  const auto rows = screen.styled_rows();
  expect(!rows.empty() && rows.back().size() >= 2, "default bg spans");
  expect(rows.back()[0].text == "x" && !rows.back()[0].bg_default, "explicit red bg");
  // SGR 49 and SGR 0 both restore default bg with the same fg, so "y"/"z" merge.
  expect(rows.back()[1].text.find('y') != std::string::npos && rows.back()[1].bg_default,
         "sgr 49 default bg");
  expect(rows.back()[1].text.find('z') != std::string::npos && rows.back()[1].bg_default,
         "sgr 0 default bg");
}

}  // namespace

int main() {
  test_basic_ansi_colors();
  test_256_color_prompt();
  test_truecolor();
  test_256_background();
  test_default_background_flag();
  std::cout << "raw_pty_screen_test: ok\n";
  return 0;
}
