; Tree-sitter highlight query for Bash (no #match? / #eq? predicates).

[
  (string)
  (raw_string)
  (ansi_c_string)
  (heredoc_body)
] @string

[
  (heredoc_start)
  (heredoc_end)
] @label

(comment) @comment
(number) @number
(test_operator) @operator

(command_name) @function
(function_definition name: (word) @function)

(simple_expansion) @variable
(expansion) @variable
(special_variable_name) @constant

[
  "if"
  "then"
  "else"
  "elif"
  "fi"
  "case"
  "in"
  "esac"
  "for"
  "do"
  "done"
  "select"
  "until"
  "while"
  "declare"
  "typeset"
  "readonly"
  "local"
  "unset"
  "unsetenv"
  "export"
  "function"
  "time"
  "coproc"
] @keyword

[
  ">"
  ">>"
  "<"
  "<<"
  "&&"
  "|"
  "|&"
  "||"
  "="
  "+="
  "=~"
  "=="
  "!="
  "!"
] @operator
