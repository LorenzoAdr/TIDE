; Tree-sitter locals query for C++ (tree-sitter-cpp v0.23.4)
; Based on helix-editor c + cpp locals queries.

; Scopes
(function_definition) @local.scope
(declaration) @local.scope
(lambda_expression) @local.scope
(namespace_definition) @local.scope
(class_specifier) @local.scope
(struct_specifier) @local.scope
(for_range_loop) @local.scope
(while_statement) @local.scope
(for_statement) @local.scope
(do_statement) @local.scope
(if_statement) @local.scope
(switch_statement) @local.scope
(try_statement) @local.scope
(catch_clause) @local.scope
(compound_statement) @local.scope

; Parameters (up to 4 declarator layers)
(parameter_declaration (identifier) @local.definition)
(parameter_declaration (_ (identifier) @local.definition))
(parameter_declaration (_ (_ (identifier) @local.definition)))
(parameter_declaration (_ (_ (_ (identifier) @local.definition))))

(optional_parameter_declaration declarator: (identifier) @local.definition)
(variadic_parameter_declaration
  declarator: (variadic_declarator (identifier) @local.definition))

(type_parameter_declaration (type_identifier) @local.definition)

; References
(identifier) @local.reference

(call_expression function: (identifier) @_)
