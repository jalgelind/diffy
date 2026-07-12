#include <doctest.h>

#include "highlight/language.hpp"
#include "highlight/scope.hpp"

#include <string>
#include <vector>

using namespace diffy;

TEST_CASE("scope_outline finds enclosing functions") {
    const std::string src =
        "#include <stdio.h>\n"   // line 0
        "\n"                     // 1
        "int add(int a, int b) {\n"  // 2
        "    int s = a + b;\n"   // 3
        "    return s;\n"        // 4
        "}\n"                    // 5
        "\n"                     // 6
        "int main(void) {\n"     // 7
        "    return 0;\n"        // 8
        "}\n";                   // 9

    auto outline = scope_outline(src, language_for_path("x.c"));

#ifdef DIFFY_ENABLE_HIGHLIGHT
    REQUIRE_FALSE(outline.empty());
    CHECK(enclosing_scope(outline, 3) == "int add(int a, int b)");
    CHECK(enclosing_scope(outline, 8) == "int main(void)");
    CHECK(enclosing_scope(outline, 0).empty());  // #include is top-level
#else
    CHECK(outline.empty());  // highlighting disabled at build time
#endif
}

TEST_CASE("scope_outline captures classes and structs by name") {
    const std::string src =
        "class Widget {\n"        // 0
        "  public:\n"             // 1
        "    void draw();\n"      // 2
        "};\n"                    // 3
        "\n"                      // 4
        "struct Point {\n"        // 5
        "    int x;\n"            // 6
        "    int y;\n"            // 7
        "};\n"                    // 8
        "\n"                      // 9
        "Widget make() {\n"       // 10
        "    Point p;\n"          // 11  uses of Widget + Point resolve to their defs
        "    return Widget{};\n"  // 12
        "}\n";                    // 13
    auto outline = scope_outline(src, language_for_path("x.cpp"));
#ifdef DIFFY_ENABLE_HIGHLIGHT
    auto w = resolve_definition(outline, "Widget", 11);
    REQUIRE(w.has_value());
    CHECK(w->start_line == 0);
    auto p = resolve_definition(outline, "Point", 11);
    REQUIRE(p.has_value());
    CHECK(p->start_line == 5);
#endif
}

TEST_CASE("local_defs captures params and locals with visibility scopes") {
    const std::string src =
        "int add(int a, int b) {\n"   // 0  params a, b
        "    int s = a + b;\n"        // 1  outer local s
        "    {\n"                     // 2
        "        int s = 99;\n"       // 3  inner local s (shadows)
        "        return s;\n"         // 4  use -> inner s
        "    }\n"                     // 5
        "    return s + a;\n"         // 6  use -> outer s, param a
        "}\n";                        // 7

    auto defs = local_defs(src, language_for_path("x.cpp"));

#ifdef DIFFY_ENABLE_HIGHLIGHT
    REQUIRE_FALSE(defs.empty());
    // Shadowing: the use on line 4 sees the inner s (declared line 3), not the outer.
    auto inner = resolve_local(defs, "s", 4);
    REQUIRE(inner.has_value());
    CHECK(inner->line == 3);
    // The use on line 6 is outside the inner block, so it sees the outer s (line 1).
    auto outer = resolve_local(defs, "s", 6);
    REQUIRE(outer.has_value());
    CHECK(outer->line == 1);
    // A parameter resolves from anywhere in the function body.
    auto param = resolve_local(defs, "a", 6);
    REQUIRE(param.has_value());
    CHECK(param->line == 0);
    // Initializer identifiers are uses, not bindings: `b` is a param (line 0), never
    // captured from `a + b`.
    CHECK_FALSE(resolve_local(defs, "nope", 4).has_value());
#else
    CHECK(defs.empty());  // highlighting disabled at build time
#endif
}

TEST_CASE("resolve_local: innermost scope wins, tiebreak nearest declaration") {
    // Pure (no parse): {line, scope_start, scope_end, label, name}.
    const std::vector<LocalDef> defs = {
        {1, 0, 9, "int x", "x"},   // outer x, whole function
        {4, 3, 6, "int x", "x"},   // inner x, block 3..6 (shadows within)
        {2, 0, 9, "int y", "y"},   // y at line 2
        {7, 0, 9, "int y", "y"},   // y redeclared at line 7 (same scope)
    };
    CHECK(resolve_local(defs, "x", 5)->line == 4);  // inside inner block -> inner
    CHECK(resolve_local(defs, "x", 8)->line == 1);  // outside inner block -> outer
    CHECK(resolve_local(defs, "y", 3)->line == 2);  // nearest at/above the use
    CHECK(resolve_local(defs, "y", 8)->line == 7);  // nearest at/above the use
    CHECK_FALSE(resolve_local(defs, "z", 5).has_value());
    CHECK_FALSE(resolve_local(defs, "", 5).has_value());
}

TEST_CASE("hunk_context prefers the new side, then old, then nothing") {
    const std::vector<CodeScope> a_outline = {{2, 5, "old_fn"}};
    const std::vector<CodeScope> b_outline = {{2, 5, "new_fn"}};

    // Change on the new side -> new side's label.
    CHECK(hunk_context(a_outline, b_outline, -1, 3, 0, 0) == "new_fn");
    // Pure deletion (only old side changed) -> old side's label.
    CHECK(hunk_context(a_outline, b_outline, 3, -1, 0, 0) == "old_fn");
    // Change outside any scope -> empty.
    CHECK(hunk_context(a_outline, b_outline, -1, 9, 9, 9).empty());
}

TEST_CASE("scope label keeps the name when the return type is on its own line") {
    const std::string src =
        "namespace n {\n"        // 0
        "\n"                     // 1
        "std::string\n"          // 2  return type on its own line
        "make_label(int x,\n"    // 3  name + params, wrapped
        "           int y) {\n"  // 4
        "    return \"\";\n"     // 5
        "}\n"                    // 6
        "\n"                     // 7
        "}\n";                   // 8

    auto outline = scope_outline(src, language_for_path("x.cpp"));
#ifdef DIFFY_ENABLE_HIGHLIGHT
    REQUIRE_FALSE(outline.empty());
    // The label spans the whole signature, not just "std::string".
    CHECK(enclosing_scope(outline, 4) == "std::string make_label(int x, int y)");
#else
    CHECK(outline.empty());
#endif
}

TEST_CASE("scope label for Python ends at the def/class colon") {
    const std::string src =
        "class Foo:\n"          // 0
        "    def bar(self,\n"   // 1
        "            x):\n"     // 2
        "        return x\n";   // 3

    auto outline = scope_outline(src, language_for_path("x.py"));
#ifdef DIFFY_ENABLE_HIGHLIGHT
    REQUIRE_FALSE(outline.empty());
    CHECK(enclosing_scope(outline, 3) == "def bar(self, x)");
    CHECK(enclosing_scope(outline, 0) == "class Foo");
#else
    CHECK(outline.empty());
#endif
}

TEST_CASE("scope_outline captures C++ definition names (jump-to-definition)") {
    const std::string src =
        "int add(int a, int b) {\n"        // 0  free function
        "    return a + b;\n"              // 1
        "}\n"                              // 2
        "\n"                               // 3
        "class Widget {\n"                 // 4  class
        "    void draw() {\n"              // 5  in-class method (field_identifier)
        "        return;\n"                // 6
        "    }\n"                          // 7
        "};\n"                             // 8
        "\n"                               // 9
        "void Widget::resize(int w) {\n"   // 10 out-of-line method (qualified)
        "    (void)w;\n"                   // 11
        "}\n";                             // 12

    auto outline = scope_outline(src, language_for_path("x.cpp"));
#ifdef DIFFY_ENABLE_HIGHLIGHT
    REQUIRE_FALSE(outline.empty());
    REQUIRE(resolve_definition(outline, "add").has_value());
    CHECK(resolve_definition(outline, "add")->start_line == 0);
    CHECK(resolve_definition(outline, "Widget").has_value());
    CHECK(resolve_definition(outline, "draw").has_value());
    // Out-of-line method resolves by its unqualified name (Widget::resize -> resize).
    REQUIRE(resolve_definition(outline, "resize").has_value());
    CHECK(resolve_definition(outline, "resize")->start_line == 10);
    // Unknown identifier / empty -> no match.
    CHECK_FALSE(resolve_definition(outline, "nope").has_value());
    CHECK_FALSE(resolve_definition(outline, "").has_value());
#else
    CHECK(outline.empty());
#endif
}

TEST_CASE("scope_outline captures Python definition names") {
    const std::string src =
        "class Foo:\n"          // 0
        "    def bar(self):\n"  // 1
        "        return 1\n";   // 2
    auto outline = scope_outline(src, language_for_path("x.py"));
#ifdef DIFFY_ENABLE_HIGHLIGHT
    REQUIRE_FALSE(outline.empty());
    CHECK(resolve_definition(outline, "Foo").has_value());
    REQUIRE(resolve_definition(outline, "bar").has_value());
    CHECK(resolve_definition(outline, "bar")->start_line == 1);
#else
    CHECK(outline.empty());
#endif
}

TEST_CASE("resolve_definition picks the nearest same-named def (no scoping)") {
    const std::vector<CodeScope> outline = {
        {2, 5, "first", "foo"},
        {20, 25, "second", "foo"},
    };
    CHECK(resolve_definition(outline, "foo", 22)->start_line == 20);
    CHECK(resolve_definition(outline, "foo", 3)->start_line == 2);
    CHECK(resolve_definition(outline, "foo")->start_line == 2);  // near=-1 -> first match
}
