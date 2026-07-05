(identifier) @default

[
  "if"
  "else"
  "for"
  "while"
  "do"
  "switch"
  "case"
  "default"
  "break"
  "continue"
  "return"
  "function"
  "const"
  "let"
  "var"
  "class"
  "extends"
  "new"
  "delete"
  "typeof"
  "instanceof"
  "in"
  "of"
  "import"
  "export"
  "from"
  "as"
  "async"
  "await"
  "yield"
  "throw"
  "try"
  "catch"
  "finally"
  "void"
  "get"
  "set"
  "static"
  "interface"
  "type"
  "enum"
  "namespace"
  "declare"
  "abstract"
  "implements"
  "readonly"
  "public"
  "private"
  "protected"
  "keyof"
  "infer"
  "is"
  "satisfies"
  "override"
] @keyword

[(true) (false) (null) (undefined)] @constant
(comment) @comment
(string) @string
(template_string) @string
(number) @number
(predefined_type) @type
(type_identifier) @type
(property_identifier) @property

(call_expression
  function: (identifier) @function)
(function_declaration
  name: (identifier) @function)
(method_definition
  name: (property_identifier) @function)
