#include "editor/code_snippets.hpp"

#include <algorithm>
#include <cctype>

namespace tgdb {

namespace {

struct StructureSnippet {
  const char* keyword;
  const char* label;
  const char* detail;
  const char* body;
  SymbolKind kind;
};

bool prefix_match(const std::string& keyword, const std::string& query) {
  if (query.empty()) {
    return true;
  }
  if (keyword.size() < query.size()) {
    return false;
  }
  for (std::size_t i = 0; i < query.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(keyword[i])) !=
        std::tolower(static_cast<unsigned char>(query[i]))) {
      return false;
    }
  }
  return true;
}

const std::vector<StructureSnippet>& all_snippets() {
  static const std::vector<StructureSnippet> kSnippets = {
      {"struct", "struct", "structura", "struct ${1:Name} {\n\t$0\n};", SymbolKind::kStruct},
      {"class", "class", "clase", "class ${1:Name} {\npublic:\n\t$0\n};", SymbolKind::kClass},
      {"union", "union", "unión", "union ${1:Name} {\n\t$0\n};", SymbolKind::kStruct},
      {"enum", "enum", "enumeración", "enum ${1:Name} {\n\t$0\n};", SymbolKind::kVariable},
      {"enum class", "enum class", "enum class", "enum class ${1:Name} {\n\t$0\n};",
       SymbolKind::kVariable},
      {"namespace", "namespace", "espacio de nombres", "namespace ${1:name} {\n\t$0\n}",
       SymbolKind::kNamespace},
      {"switch", "switch", "switch", "switch (${1:expr}) {\n\tcase ${2:value}:\n\t\t$0\n\t\tbreak;\n}",
       SymbolKind::kFunction},
      {"if", "if", "if", "if (${1:condition}) {\n\t$0\n}", SymbolKind::kFunction},
      {"if else", "if else", "if / else", "if (${1:condition}) {\n\t$2\n} else {\n\t$0\n}",
       SymbolKind::kFunction},
      {"for", "for", "for clásico", "for (${1:int i = 0}; ${2:i < n}; ${3:++i}) {\n\t$0\n}",
       SymbolKind::kFunction},
      {"for range", "for (auto", "for rango (C++11)",
       "for (auto& ${1:item} : ${2:container}) {\n\t$0\n}", SymbolKind::kFunction},
      {"while", "while", "while", "while (${1:condition}) {\n\t$0\n}", SymbolKind::kFunction},
      {"do", "do while", "do / while", "do {\n\t$0\n} while (${1:condition});",
       SymbolKind::kFunction},
      {"try", "try catch", "try / catch",
       "try {\n\t$1\n} catch (const ${2:std::exception}& e) {\n\t$0\n}", SymbolKind::kFunction},
      {"void", "void fn()", "función void", "void ${1:name}(${2:}) {\n\t$0\n}",
       SymbolKind::kFunction},
      {"main", "main()", "punto de entrada",
       "int main(int argc, char* argv[]) {\n\t$0\n\treturn 0;\n}", SymbolKind::kFunction},
      {"lambda", "lambda", "expresión lambda", "auto ${1:fn} = [${2:&}](${3:}) -> ${4:void} {\n\t$0\n};",
       SymbolKind::kFunction},
      {"ctor", "constructor", "constructor con init list",
       "${1:Class}::${1:Class}(${2:}) : ${3:member_()}\n{\n\t$0\n}", SymbolKind::kMethod},
      {"pragma", "#pragma once", "pragma once", "#pragma once\n\n$0", SymbolKind::kVariable},
  };
  return kSnippets;
}

}  // namespace

std::vector<CompletionItem> structure_snippet_completions(const std::string& query) {
  std::vector<CompletionItem> out;
  for (const StructureSnippet& snippet : all_snippets()) {
    if (!prefix_match(snippet.keyword, query)) {
      continue;
    }
    CompletionItem item;
    item.label = snippet.label;
    item.insert_text = snippet.body;
    item.detail = snippet.detail;
    item.kind = snippet.kind;
    item.insert_format = InsertTextFormat::kSnippet;
    out.push_back(item);
  }
  return out;
}

bool structure_snippet_prefix_active(const std::string& prefix) {
  if (prefix.empty()) {
    return false;
  }
  for (const StructureSnippet& snippet : all_snippets()) {
    if (prefix_match(snippet.keyword, prefix)) {
      return true;
    }
  }
  return false;
}

}  // namespace tgdb
