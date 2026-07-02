# config_parser

A small TOML-ish config parser for diffy. Its reason to exist over an off-the-shelf
library: it **preserves comments** and **key insertion order** across a
parse → serialize round-trip, so re-writing a config file doesn't reorder or drop
the user's edits.

## Format

```conf
# a comment (# or // )
[section]
    name    = "escaped\nstring"   # "double" quotes: \\ \" \n \r \t
    path    = 'C:\raw\literal'    # 'single' quotes: raw, no escapes
    blurb   = """multi-line
                 escaped"""       # '''raw''' / """escaped""" span newlines
    count   = 42                  # int
    ratio   = 0.5                 # float
    enabled = true                # bool: true/false/on/off
    list    = [1, 2, 3]           # array (of any value, incl. tables)
    nested  = { a = 1, b = ['x'] } # inline table
```

**Strings.** `'single'` = raw literal (verbatim, no escapes — safe for Windows
paths). `"double"` = escaped: `\\ \" \n \r \t`. The serializer emits a raw literal
when a value needs no escaping and switches to a `"double"` string when it contains
a quote, backslash or newline — so existing files stay byte-identical and arbitrary
text still round-trips. Multi-line strings use `'''raw'''` / `"""escaped"""`
(spanning real newlines); the serializer emits `"""…"""` for any value containing
a line break.

## API

```cpp
#include <config_parser/config_parser.hpp>
#include <config_parser/config_parser_utils.hpp>   // cfg_load_file
#include <config_parser/config_serializer.hpp>     // cfg_serialize

diffy::Value root;
diffy::ParseResult r;
cfg_parse_value_tree(text, r, root);   // or cfg_load_file(path, r, root)

if (auto v = root.lookup_value_by_path("section.name"); v && v->get().is_string())
    use(v->get().as_string());

std::string out = cfg_serialize(root);  // sections; cfg_serialize_obj for an object
```

`Value` is a variant of `Table` (insertion-ordered), `Array`, `Int`, `Float`,
`Bool`, `String` with `is_*()` / `as_*()` accessors and `operator[]`.

## Limitations

- Some comments with no logical anchor (trailing, or right of the last value in a
  block) are dropped on re-serialize.
- Line numbers in error messages after a multi-line string may be off (newlines
  inside the string aren't counted).

Tests: `*_tests.cc` (doctest), built into the top-level `diffy-test` target.
