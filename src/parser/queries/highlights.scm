; Tree-sitter highlight query for C++ (tree-sitter-cpp v0.23.4)
; String literals below must be valid anonymous tokens for this grammar.

[
  "alignas"
  "alignof"
  "asm"
  "break"
  "case"
  "catch"
  "class"
  "co_await"
  "co_return"
  "co_yield"
  "const"
  "constexpr"
  "consteval"
  "constinit"
  "continue"
  "decltype"
  "default"
  "delete"
  "do"
  "else"
  "enum"
  "explicit"
  "extern"
  "final"
  "for"
  "friend"
  "goto"
  "if"
  "inline"
  "mutable"
  "namespace"
  "new"
  "noexcept"
  "operator"
  "override"
  "private"
  "protected"
  "public"
  "register"
  "return"
  "static"
  "static_assert"
  "struct"
  "switch"
  "template"
  "thread_local"
  "throw"
  "try"
  "typedef"
  "typename"
  "union"
  "using"
  "virtual"
  "volatile"
  "while"
  "concept"
  "requires"
] @keyword

"nullptr" @constant
(null) @constant

(this) @variable.builtin

(comment) @comment
(raw_string_literal) @string
(string_literal) @string
(char_literal) @string
(number_literal) @number

(preproc_def) @macro
(preproc_function_def) @macro
(preproc_include) @macro
(preproc_call) @macro
(preproc_arg) @macro
(preproc_directive) @macro
(preproc_if) @macro
(preproc_elif) @macro
(preproc_else) @macro

(auto) @type
(primitive_type) @type
(type_identifier) @type
(namespace_identifier) @namespace
(class_specifier name: (_) @type)
(struct_specifier name: (_) @type)
(enum_specifier name: (_) @type)

(function_declarator declarator: (identifier) @function)
(function_declarator declarator: (field_identifier) @function)
(function_declarator declarator: (qualified_identifier name: (_) @function))
(call_expression function: (identifier) @function)
(call_expression function: (qualified_identifier name: (_) @function))

(field_identifier) @property
(identifier) @variable
(parameter_declaration declarator: (_) @parameter)
