#include "editor/code_snippets.hpp"

#include <algorithm>
#include <cctype>

#include "i18n/tr.hpp"
#include "lsp/lsp_uri.hpp"

namespace tuide {

namespace {

struct StructureSnippet {
  const char* id;
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

const std::vector<StructureSnippet>& cpp_snippets() {
  static const std::vector<StructureSnippet> kSnippets = {
      {"struct", "struct", "struct", "snippet.detail.struct", "struct ${1:Name} {\n\t$0\n};",
       SymbolKind::kStruct},
      {"class", "class", "class", "snippet.detail.class",
       "class ${1:Name} {\npublic:\n\t$0\n};", SymbolKind::kClass},
      {"union", "union", "union", "snippet.detail.union", "union ${1:Name} {\n\t$0\n};",
       SymbolKind::kStruct},
      {"enum", "enum", "enum", "snippet.detail.enum", "enum ${1:Name} {\n\t$0\n};",
       SymbolKind::kVariable},
      {"enum_class", "enum class", "enum class", "snippet.detail.enum_class",
       "enum class ${1:Name} {\n\t$0\n};", SymbolKind::kVariable},
      {"namespace", "namespace", "namespace", "snippet.detail.namespace",
       "namespace ${1:name} {\n\t$0\n}", SymbolKind::kNamespace},
      {"switch", "switch", "switch", "snippet.detail.switch",
       "switch (${1:expr}) {\n\tcase ${2:value}:\n\t\t$0\n\t\tbreak;\n}",
       SymbolKind::kFunction},
      {"if", "if", "if", "snippet.detail.if", "if (${1:condition}) {\n\t$0\n}",
       SymbolKind::kFunction},
      {"if_else", "if else", "if else", "snippet.detail.if_else",
       "if (${1:condition}) {\n\t$2\n} else {\n\t$0\n}", SymbolKind::kFunction},
      {"for", "for", "for", "snippet.detail.for_classic",
       "for (${1:int i = 0}; ${2:i < n}; ${3:++i}) {\n\t$0\n}", SymbolKind::kFunction},
      {"for_range", "for range", "for (auto", "snippet.detail.for_range",
       "for (auto& ${1:item} : ${2:container}) {\n\t$0\n}", SymbolKind::kFunction},
      {"while", "while", "while", "snippet.detail.while", "while (${1:condition}) {\n\t$0\n}",
       SymbolKind::kFunction},
      {"do_while", "do", "do while", "snippet.detail.do_while",
       "do {\n\t$0\n} while (${1:condition});", SymbolKind::kFunction},
      {"try_catch", "try", "try catch", "snippet.detail.try_catch",
       "try {\n\t$1\n} catch (const ${2:std::exception}& e) {\n\t$0\n}",
       SymbolKind::kFunction},
      {"void_fn", "void", "void fn()", "snippet.detail.void_fn",
       "void ${1:name}(${2:}) {\n\t$0\n}", SymbolKind::kFunction},
      {"main", "main", "main()", "snippet.detail.main",
       "int main(int argc, char* argv[]) {\n\t$0\n\treturn 0;\n}", SymbolKind::kFunction},
      {"lambda", "lambda", "lambda", "snippet.detail.lambda",
       "auto ${1:fn} = [${2:&}](${3:}) -> ${4:void} {\n\t$0\n};", SymbolKind::kFunction},
      {"ctor", "ctor", "constructor", "snippet.detail.constructor",
       "${1:Class}::${1:Class}(${2:}) : ${3:member_()}\n{\n\t$0\n}", SymbolKind::kMethod},
      {"pragma_once", "pragma", "#pragma once", "snippet.detail.pragma_once",
       "#pragma once\n\n$0", SymbolKind::kVariable},
  };
  return kSnippets;
}

const std::vector<StructureSnippet>& python_snippets() {
  static const std::vector<StructureSnippet> kSnippets = {
      {"class", "class", "class", "snippet.detail.class",
       "class ${1:Name}:\n\tdef __init__(self${2:}):\n\t\t$0\n\t\tpass\n",
       SymbolKind::kClass},
      {"dataclass", "dataclass", "@dataclass", "snippet.detail.dataclass",
       "@dataclass\nclass ${1:Name}:\n\t${2:field}: ${3:str}\n\t$0\n", SymbolKind::kClass},
      {"def", "def", "def", "snippet.detail.def",
       "def ${1:name}(${2:self}):\n\t$0\n\tpass\n", SymbolKind::kFunction},
      {"async_def", "async def", "async def", "snippet.detail.async_def",
       "async def ${1:name}(${2:}):\n\t$0\n\tpass\n", SymbolKind::kFunction},
      {"if", "if", "if", "snippet.detail.if", "if ${1:condition}:\n\t$0\n",
       SymbolKind::kFunction},
      {"if_else", "if else", "if else", "snippet.detail.if_else",
       "if ${1:condition}:\n\t$2\nelse:\n\t$0\n", SymbolKind::kFunction},
      {"for", "for", "for", "snippet.detail.for_range",
       "for ${1:item} in ${2:iterable}:\n\t$0\n", SymbolKind::kFunction},
      {"while", "while", "while", "snippet.detail.while", "while ${1:condition}:\n\t$0\n",
       SymbolKind::kFunction},
      {"try_except", "try", "try except", "snippet.detail.try_except",
       "try:\n\t$1\nexcept ${2:Exception} as e:\n\t$0\n", SymbolKind::kFunction},
      {"with", "with", "with", "snippet.detail.with",
       "with ${1:expr} as ${2:var}:\n\t$0\n", SymbolKind::kFunction},
      {"main", "main", "if __name__", "snippet.detail.main",
       "def main():\n\t$0\n\tpass\n\n\nif __name__ == \"__main__\":\n\tmain()\n",
       SymbolKind::kFunction},
      {"lambda", "lambda", "lambda", "snippet.detail.lambda",
       "${1:name} = lambda ${2:x}: ${0:x}", SymbolKind::kFunction},
      {"enum", "enum", "Enum", "snippet.detail.enum",
       "class ${1:Name}(Enum):\n\t${2:VALUE} = ${3:1}\n\t$0\n", SymbolKind::kVariable},
  };
  return kSnippets;
}

const std::vector<StructureSnippet>& rust_snippets() {
  static const std::vector<StructureSnippet> kSnippets = {
      {"struct", "struct", "struct", "snippet.detail.struct",
       "struct ${1:Name} {\n\t$0\n}", SymbolKind::kStruct},
      {"enum", "enum", "enum", "snippet.detail.enum", "enum ${1:Name} {\n\t$0\n}",
       SymbolKind::kVariable},
      {"impl", "impl", "impl", "snippet.detail.impl",
       "impl ${1:Name} {\n\t$0\n}", SymbolKind::kClass},
      {"trait", "trait", "trait", "snippet.detail.trait",
       "trait ${1:Name} {\n\t$0\n}", SymbolKind::kClass},
      {"fn", "fn", "fn", "snippet.detail.fn",
       "fn ${1:name}(${2:}) ${3:-> ()} {\n\t$0\n}", SymbolKind::kFunction},
      {"match", "match", "match", "snippet.detail.match",
       "match ${1:expr} {\n\t${2:pattern} => ${3:},\n\t$0\n}", SymbolKind::kFunction},
      {"if", "if", "if", "snippet.detail.if", "if ${1:condition} {\n\t$0\n}",
       SymbolKind::kFunction},
      {"if_else", "if else", "if else", "snippet.detail.if_else",
       "if ${1:condition} {\n\t$2\n} else {\n\t$0\n}", SymbolKind::kFunction},
      {"for", "for", "for", "snippet.detail.for_range",
       "for ${1:item} in ${2:iter} {\n\t$0\n}", SymbolKind::kFunction},
      {"while", "while", "while", "snippet.detail.while", "while ${1:condition} {\n\t$0\n}",
       SymbolKind::kFunction},
      {"loop", "loop", "loop", "snippet.detail.loop", "loop {\n\t$0\n}",
       SymbolKind::kFunction},
      {"main", "main", "fn main", "snippet.detail.main", "fn main() {\n\t$0\n}",
       SymbolKind::kFunction},
      {"mod", "mod", "mod", "snippet.detail.mod", "mod ${1:name} {\n\t$0\n}",
       SymbolKind::kNamespace},
  };
  return kSnippets;
}

const std::vector<StructureSnippet>& go_snippets() {
  static const std::vector<StructureSnippet> kSnippets = {
      {"struct", "struct", "struct", "snippet.detail.struct",
       "type ${1:Name} struct {\n\t$0\n}", SymbolKind::kStruct},
      {"interface", "interface", "interface", "snippet.detail.interface",
       "type ${1:Name} interface {\n\t$0\n}", SymbolKind::kClass},
      {"func", "func", "func", "snippet.detail.fn",
       "func ${1:name}(${2:}) ${3:} {\n\t$0\n}", SymbolKind::kFunction},
      {"method", "method", "method", "snippet.detail.method",
       "func (${1:r *Receiver}) ${2:Name}(${3:}) ${4:} {\n\t$0\n}", SymbolKind::kMethod},
      {"if", "if", "if", "snippet.detail.if", "if ${1:condition} {\n\t$0\n}",
       SymbolKind::kFunction},
      {"if_else", "if else", "if else", "snippet.detail.if_else",
       "if ${1:condition} {\n\t$2\n} else {\n\t$0\n}", SymbolKind::kFunction},
      {"for", "for", "for", "snippet.detail.for_classic",
       "for ${1:i := 0}; ${2:i < n}; ${3:i++} {\n\t$0\n}", SymbolKind::kFunction},
      {"for_range", "for range", "for range", "snippet.detail.for_range",
       "for ${1:i}, ${2:v} := range ${3:items} {\n\t$0\n}", SymbolKind::kFunction},
      {"switch", "switch", "switch", "snippet.detail.switch",
       "switch ${1:expr} {\ncase ${2:value}:\n\t$0\n}", SymbolKind::kFunction},
      {"select", "select", "select", "snippet.detail.select",
       "select {\ncase ${1:v} := <-${2:ch}:\n\t$0\n}", SymbolKind::kFunction},
      {"main", "main", "func main", "snippet.detail.main",
       "func main() {\n\t$0\n}", SymbolKind::kFunction},
  };
  return kSnippets;
}

const std::vector<StructureSnippet>& zig_snippets() {
  static const std::vector<StructureSnippet> kSnippets = {
      {"struct", "struct", "struct", "snippet.detail.struct",
       "const ${1:Name} = struct {\n\t$0\n};", SymbolKind::kStruct},
      {"enum", "enum", "enum", "snippet.detail.enum",
       "const ${1:Name} = enum {\n\t$0\n};", SymbolKind::kVariable},
      {"fn", "fn", "fn", "snippet.detail.fn",
       "fn ${1:name}(${2:}) ${3:void} {\n\t$0\n}", SymbolKind::kFunction},
      {"if", "if", "if", "snippet.detail.if", "if (${1:condition}) {\n\t$0\n}",
       SymbolKind::kFunction},
      {"if_else", "if else", "if else", "snippet.detail.if_else",
       "if (${1:condition}) {\n\t$2\n} else {\n\t$0\n}", SymbolKind::kFunction},
      {"for", "for", "for", "snippet.detail.for_range",
       "for (${1:items}) |${2:item}| {\n\t$0\n}", SymbolKind::kFunction},
      {"while", "while", "while", "snippet.detail.while",
       "while (${1:condition}) {\n\t$0\n}", SymbolKind::kFunction},
      {"switch", "switch", "switch", "snippet.detail.switch",
       "switch (${1:expr}) {\n\t${2:value} => $0,\n}", SymbolKind::kFunction},
      {"main", "main", "main", "snippet.detail.main",
       "pub fn main() !void {\n\t$0\n}", SymbolKind::kFunction},
  };
  return kSnippets;
}

const std::vector<StructureSnippet>& fortran_snippets() {
  static const std::vector<StructureSnippet> kSnippets = {
      {"program", "program", "program", "snippet.detail.program",
       "program ${1:name}\n  implicit none\n  $0\nend program ${1:name}\n",
       SymbolKind::kFunction},
      {"module", "module", "module", "snippet.detail.module",
       "module ${1:name}\n  implicit none\n  $0\nend module ${1:name}\n",
       SymbolKind::kNamespace},
      {"subroutine", "subroutine", "subroutine", "snippet.detail.subroutine",
       "subroutine ${1:name}(${2:})\n  implicit none\n  $0\nend subroutine ${1:name}\n",
       SymbolKind::kFunction},
      {"function", "function", "function", "snippet.detail.fn",
       "function ${1:name}(${2:}) result(${3:res})\n  implicit none\n  $0\nend function "
       "${1:name}\n",
       SymbolKind::kFunction},
      {"type", "type", "type", "snippet.detail.struct",
       "type :: ${1:name}\n  $0\nend type ${1:name}\n", SymbolKind::kStruct},
      {"if", "if", "if", "snippet.detail.if", "if (${1:condition}) then\n  $0\nend if\n",
       SymbolKind::kFunction},
      {"if_else", "if else", "if else", "snippet.detail.if_else",
       "if (${1:condition}) then\n  $2\nelse\n  $0\nend if\n", SymbolKind::kFunction},
      {"do", "do", "do", "snippet.detail.for_classic",
       "do ${1:i} = ${2:1}, ${3:n}\n  $0\nend do\n", SymbolKind::kFunction},
      {"select_case", "select", "select case", "snippet.detail.switch",
       "select case (${1:expr})\ncase (${2:value})\n  $0\nend select\n",
       SymbolKind::kFunction},
  };
  return kSnippets;
}

const std::vector<StructureSnippet>& lua_snippets() {
  static const std::vector<StructureSnippet> kSnippets = {
      {"function", "function", "function", "snippet.detail.fn",
       "function ${1:name}(${2:})\n\t$0\nend\n", SymbolKind::kFunction},
      {"local_fn", "local function", "local function", "snippet.detail.local_fn",
       "local function ${1:name}(${2:})\n\t$0\nend\n", SymbolKind::kFunction},
      {"if", "if", "if", "snippet.detail.if", "if ${1:condition} then\n\t$0\nend\n",
       SymbolKind::kFunction},
      {"if_else", "if else", "if else", "snippet.detail.if_else",
       "if ${1:condition} then\n\t$2\nelse\n\t$0\nend\n", SymbolKind::kFunction},
      {"for", "for", "for", "snippet.detail.for_classic",
       "for ${1:i} = ${2:1}, ${3:n} do\n\t$0\nend\n", SymbolKind::kFunction},
      {"for_in", "for in", "for in", "snippet.detail.for_range",
       "for ${1:k}, ${2:v} in pairs(${3:t}) do\n\t$0\nend\n", SymbolKind::kFunction},
      {"while", "while", "while", "snippet.detail.while",
       "while ${1:condition} do\n\t$0\nend\n", SymbolKind::kFunction},
      {"table", "table", "table", "snippet.detail.struct",
       "local ${1:t} = {\n\t$0\n}\n", SymbolKind::kStruct},
  };
  return kSnippets;
}

const std::vector<StructureSnippet>& js_snippets() {
  static const std::vector<StructureSnippet> kSnippets = {
      {"class", "class", "class", "snippet.detail.class",
       "class ${1:Name} {\n\tconstructor(${2:}) {\n\t\t$0\n\t}\n}\n", SymbolKind::kClass},
      {"function", "function", "function", "snippet.detail.fn",
       "function ${1:name}(${2:}) {\n\t$0\n}\n", SymbolKind::kFunction},
      {"arrow", "arrow", "arrow fn", "snippet.detail.arrow",
       "const ${1:name} = (${2:}) => {\n\t$0\n};\n", SymbolKind::kFunction},
      {"if", "if", "if", "snippet.detail.if", "if (${1:condition}) {\n\t$0\n}",
       SymbolKind::kFunction},
      {"if_else", "if else", "if else", "snippet.detail.if_else",
       "if (${1:condition}) {\n\t$2\n} else {\n\t$0\n}", SymbolKind::kFunction},
      {"for", "for", "for", "snippet.detail.for_classic",
       "for (let ${1:i} = 0; ${1:i} < ${2:n}; ${1:i}++) {\n\t$0\n}",
       SymbolKind::kFunction},
      {"for_of", "for of", "for of", "snippet.detail.for_range",
       "for (const ${1:item} of ${2:items}) {\n\t$0\n}", SymbolKind::kFunction},
      {"while", "while", "while", "snippet.detail.while", "while (${1:condition}) {\n\t$0\n}",
       SymbolKind::kFunction},
      {"switch", "switch", "switch", "snippet.detail.switch",
       "switch (${1:expr}) {\n\tcase ${2:value}:\n\t\t$0\n\t\tbreak;\n}",
       SymbolKind::kFunction},
      {"try_catch", "try", "try catch", "snippet.detail.try_catch",
       "try {\n\t$1\n} catch (${2:e}) {\n\t$0\n}", SymbolKind::kFunction},
      {"interface", "interface", "interface", "snippet.detail.interface",
       "interface ${1:Name} {\n\t$0\n}\n", SymbolKind::kClass},
      {"enum", "enum", "enum", "snippet.detail.enum",
       "enum ${1:Name} {\n\t$0\n}\n", SymbolKind::kVariable},
  };
  return kSnippets;
}

const std::vector<StructureSnippet>& shell_snippets() {
  static const std::vector<StructureSnippet> kSnippets = {
      {"function", "function", "function", "snippet.detail.fn",
       "${1:name}() {\n\t$0\n}\n", SymbolKind::kFunction},
      {"if", "if", "if", "snippet.detail.if",
       "if [[ ${1:condition} ]]; then\n\t$0\nfi\n", SymbolKind::kFunction},
      {"if_else", "if else", "if else", "snippet.detail.if_else",
       "if [[ ${1:condition} ]]; then\n\t$2\nelse\n\t$0\nfi\n", SymbolKind::kFunction},
      {"for", "for", "for", "snippet.detail.for_range",
       "for ${1:item} in ${2:items}; do\n\t$0\ndone\n", SymbolKind::kFunction},
      {"while", "while", "while", "snippet.detail.while",
       "while ${1:condition}; do\n\t$0\ndone\n", SymbolKind::kFunction},
      {"case", "case", "case", "snippet.detail.switch",
       "case ${1:expr} in\n\t${2:pattern})\n\t\t$0\n\t\t;;\nesac\n",
       SymbolKind::kFunction},
  };
  return kSnippets;
}

const std::vector<StructureSnippet>& snippets_for_language(const std::string& language_id) {
  if (language_id == "python") {
    return python_snippets();
  }
  if (language_id == "rust") {
    return rust_snippets();
  }
  if (language_id == "go") {
    return go_snippets();
  }
  if (language_id == "zig") {
    return zig_snippets();
  }
  if (language_id == "fortran") {
    return fortran_snippets();
  }
  if (language_id == "lua") {
    return lua_snippets();
  }
  if (language_id == "javascript" || language_id == "typescript") {
    return js_snippets();
  }
  if (language_id == "shellscript") {
    return shell_snippets();
  }
  return cpp_snippets();
}

CodeTemplate to_template(const StructureSnippet& snippet) {
  CodeTemplate out;
  out.id = snippet.id;
  out.label = snippet.label;
  out.detail = i18n::tr(snippet.detail_key);
  out.body = snippet.body;
  out.kind = snippet.kind;
  return out;
}

CompletionItem to_completion(const StructureSnippet& snippet) {
  CompletionItem item;
  item.label = snippet.label;
  item.insert_text = snippet.body;
  item.detail = i18n::tr(snippet.detail_key);
  item.kind = snippet.kind;
  item.insert_format = InsertTextFormat::kSnippet;
  return item;
}

}  // namespace

std::vector<CodeTemplate> code_templates_for_path(const std::string& path) {
  const std::string language_id = language_id_for_path(path);
  std::vector<CodeTemplate> out;
  for (const StructureSnippet& snippet : snippets_for_language(language_id)) {
    out.push_back(to_template(snippet));
  }
  return out;
}

std::vector<CompletionItem> structure_snippet_completions_for_path(const std::string& path,
                                                                   const std::string& query) {
  if (!kStructureSnippetCompletionsEnabled) {
    return {};
  }
  std::vector<CompletionItem> out;
  for (const StructureSnippet& snippet : snippets_for_language(language_id_for_path(path))) {
    if (!prefix_match(snippet.keyword, query)) {
      continue;
    }
    out.push_back(to_completion(snippet));
  }
  return out;
}

std::vector<CompletionItem> structure_snippet_completions(const std::string& query) {
  return structure_snippet_completions_for_path({}, query);
}

bool structure_snippet_prefix_active_for_path(const std::string& path, const std::string& prefix) {
  if (!kStructureSnippetCompletionsEnabled || prefix.empty()) {
    return false;
  }
  for (const StructureSnippet& snippet : snippets_for_language(language_id_for_path(path))) {
    if (prefix_match(snippet.keyword, prefix)) {
      return true;
    }
  }
  return false;
}

bool structure_snippet_prefix_active(const std::string& prefix) {
  return structure_snippet_prefix_active_for_path({}, prefix);
}

}  // namespace tuide
