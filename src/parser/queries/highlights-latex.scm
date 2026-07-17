; Tree-sitter highlight query for LaTeX (no #match? / #eq? predicates).

[
  (comment)
  (line_comment)
  (block_comment)
] @comment

(command_name) @function

(begin
  command: _ @function.builtin
  name: (curly_group_text (text) @function.macro))

(end
  command: _ @function.builtin
  name: (curly_group_text (text) @function.macro))

(section
  command: _ @function.macro
  text: (_) @type)

(subsection
  command: _ @function.macro
  text: (_) @type)

(subsubsection
  command: _ @function.macro
  text: (_) @type)

(chapter
  command: _ @function.macro
  text: (_) @type)

(part
  command: _ @function.macro
  text: (_) @type)

(label_definition
  command: _ @function.macro
  name: (curly_group_text (_) @label))

(label_reference
  command: _ @function.macro
  names: (curly_group_text_list (_) @label))

(math_environment) @string
(inline_formula) @string
(displayed_equation) @string

[(operator) "="] @operator
["[" "]" "{" "}"] @punctuation.bracket
