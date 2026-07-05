[
  "if"
  "else"
  "for"
  "while"
  "do"
  "switch"
  "case"
  "break"
  "continue"
  "return"
  "goto"
  "sizeof"
  "struct"
  "union"
  "enum"
  "typedef"
  "const"
  "static"
  "extern"
] @keyword

(primitive_type) @type
(type_identifier) @type
(comment) @comment
(string_literal) @string
(char_literal) @string
(system_lib_string) @string
(number_literal) @number
(preproc_directive) @keyword
(identifier) @default

(call_expression
  function: (identifier) @function)
