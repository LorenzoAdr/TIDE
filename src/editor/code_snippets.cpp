#include "editor/code_snippets.hpp"

#include <algorithm>
#include <cctype>

#include "i18n/tr.hpp"

namespace tgdb {

namespace {

struct StructureSnippet {
  const char* keyword;
  const char* label;
  const char* detail_key;
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
      {"struct", "struct", "snippet.detail.struct", "struct ${1:Name} {\n\t$0\n};",
       SymbolKind::kStruct},
      {"class", "class", "snippet.detail.class", "class ${1:Name} {\npublic:\n\t$0\n};",
       SymbolKind::kClass},
      {"union", "union", "snippet.detail.union", "union ${1:Name} {\n\t$0\n};",
       SymbolKind::kStruct},
      {"enum", "enum", "snippet.detail.enum", "enum ${1:Name} {\n\t$0\n};",
       SymbolKind::kVariable},
      {"enum class", "enum class", "snippet.detail.enum_class",
       "enum class ${1:Name} {\n\t$0\n};", SymbolKind::kVariable},
      {"namespace", "namespace", "snippet.detail.namespace",
       "namespace ${1:name} {\n\t$0\n}", SymbolKind::kNamespace},
      {"switch", "switch", "snippet.detail.switch",
       "switch (${1:expr}) {\n\tcase ${2:value}:\n\t\t$0\n\t\tbreak;\n}",
       SymbolKind::kFunction},
      {"if", "if", "snippet.detail.if", "if (${1:condition}) {\n\t$0\n}",
       SymbolKind::kFunction},
      {"if else", "if else", "snippet.detail.if_else",
       "if (${1:condition}) {\n\t$2\n} else {\n\t$0\n}", SymbolKind::kFunction},
      {"for", "for", "snippet.detail.for_classic",
       "for (${1:int i = 0}; ${2:i < n}; ${3:++i}) {\n\t$0\n}", SymbolKind::kFunction},
      {"for range", "for (auto", "snippet.detail.for_range",
       "for (auto& ${1:item} : ${2:container}) {\n\t$0\n}", SymbolKind::kFunction},
      {"while", "while", "snippet.detail.while", "while (${1:condition}) {\n\t$0\n}",
       SymbolKind::kFunction},
      {"do", "do while", "snippet.detail.do_while",
       "do {\n\t$0\n} while (${1:condition});", SymbolKind::kFunction},
      {"try", "try catch", "snippet.detail.try_catch",
       "try {\n\t$1\n} catch (const ${2:std::exception}& e) {\n\t$0\n}",
       SymbolKind::kFunction},
      {"void", "void fn()", "snippet.detail.void_fn", "void ${1:name}(${2:}) {\n\t$0\n}",
       SymbolKind::kFunction},
      {"main", "main()", "snippet.detail.main",
       "int main(int argc, char* argv[]) {\n\t$0\n\treturn 0;\n}", SymbolKind::kFunction},
      {"lambda", "lambda", "snippet.detail.lambda",
       "auto ${1:fn} = [${2:&}](${3:}) -> ${4:void} {\n\t$0\n};", SymbolKind::kFunction},
      {"ctor", "constructor", "snippet.detail.constructor",
       "${1:Class}::${1:Class}(${2:}) : ${3:member_()}\n{\n\t$0\n}", SymbolKind::kMethod},
      {"pragma", "#pragma once", "snippet.detail.pragma_once", "#pragma once\n\n$0",
       SymbolKind::kVariable},
  };
  return kSnippets;
}

}  // namespace

std::vector<CompletionItem> structure_snippet_completions(const std::string& query) {
  if (!kStructureSnippetCompletionsEnabled) {
    return {};
  }
  std::vector<CompletionItem> out;
  for (const StructureSnippet& snippet : all_snippets()) {
    if (!prefix_match(snippet.keyword, query)) {
      continue;
    }
    CompletionItem item;
    item.label = snippet.label;
    item.insert_text = snippet.body;
    item.detail = i18n::tr(snippet.detail_key);
    item.kind = snippet.kind;
    item.insert_format = InsertTextFormat::kSnippet;
    out.push_back(item);
  }
  return out;
}

bool structure_snippet_prefix_active(const std::string& prefix) {
  if (!kStructureSnippetCompletionsEnabled) {
    return false;
  }
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
