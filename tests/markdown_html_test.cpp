#include "util/markdown_html.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expect_contains(const std::string& text, const std::string& expected,
                     const char* message) {
  if (text.find(expected) == std::string::npos) {
    std::cerr << "FAIL: " << message << "\nMissing: " << expected << '\n';
    std::exit(1);
  }
}

void expect_not_contains(const std::string& text, const std::string& unexpected,
                         const char* message) {
  if (text.find(unexpected) != std::string::npos) {
    std::cerr << "FAIL: " << message << "\nUnexpected: " << unexpected << '\n';
    std::exit(1);
  }
}

void test_gfm_table() {
  const std::string html = tuide::markdown_to_html(
      "| Nombre | Estado | Total |\n"
      "| :--- | :---: | ---: |\n"
      "| **uno** | listo | 42 |\n"
      "| dos | `a|b` | 7 |\n",
      "Tabla");

  expect_contains(html, "<div class=\"table-scroll\"><table>",
                  "table wrapper emitted");
  expect_contains(html, "<thead><tr>", "table header emitted");
  expect_contains(html, "<th scope=\"col\" class=\"align-left\">Nombre</th>",
                  "left-aligned header emitted");
  expect_contains(html, "<th scope=\"col\" class=\"align-center\">Estado</th>",
                  "centered header emitted");
  expect_contains(html, "<th scope=\"col\" class=\"align-right\">Total</th>",
                  "right-aligned header emitted");
  expect_contains(html, "<td class=\"align-left\"><strong>uno</strong></td>",
                  "inline markdown rendered inside cells");
  expect_contains(html, "<td class=\"align-center\"><code>a|b</code></td>",
                  "pipe inside inline code preserved");
  expect_contains(html, "table{width:100%;border-spacing:0;border-collapse:collapse",
                  "table styling emitted");
}

void test_non_table_stays_paragraph() {
  const std::string html =
      tuide::markdown_to_html("valor | otro\nsin separador\n", "Texto");

  expect_not_contains(html, "<table>", "plain pipe text is not a table");
  expect_contains(html, "<p>valor | otro sin separador</p>",
                  "plain pipe text remains a paragraph");
}

}  // namespace

int main() {
  test_gfm_table();
  test_non_table_stays_paragraph();
  std::cout << "markdown_html_test: OK\n";
  return 0;
}
