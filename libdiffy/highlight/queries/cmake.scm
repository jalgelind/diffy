; Minimal CMake highlights — the uyha/tree-sitter-cmake grammar ships no query.
; Node names verified against the grammar (line_comment, normal_command,
; identifier, quoted_argument, variable).
(line_comment) @comment
(bracket_comment) @comment
(quoted_argument) @string
(variable) @variable
(normal_command (identifier) @function)
