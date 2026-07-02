#include "highlight/highlight_group.hpp"
#include "highlight/highlight_palette.hpp"
#include "highlight/language.hpp"
#include "highlight/syntax_highlighter.hpp"

#include <doctest.h>

#include <string>

using namespace diffy;

TEST_CASE("language detection by extension") {
    CHECK(language_for_path("src/foo/bar.c") == "c");
    CHECK(language_for_path("a.h") == "c");
    CHECK(language_for_path("pkg/data.json") == "json");
    CHECK(language_for_path("notes.unknownext") == "");
    CHECK(language_for_path("Makefile") == "");
    // Filename-based detection, including dotfiles (no extension per
    // std::filesystem) and lock files.
    CHECK(language_for_path("api/Gemfile") == "ruby");
    CHECK(language_for_path("home/.bashrc") == "bash");
    CHECK(language_for_path("Cargo.lock") == "toml");
    CHECK(language_for_path("tmpl.tpp") == "cpp");
    // "foo.profile" has the .profile *extension*; only the exact dotfile
    // ".profile" is a shell file.
    CHECK(language_for_path("foo.profile") == "");
}

TEST_CASE("config extension overrides beat the built-in map, then clear") {
    language_set_overrides({{".h", "cpp"}, {".zig", "zig"}, {"makefile", "bash"}});
    CHECK(language_for_path("a.h") == "cpp");        // override wins over built-in
    CHECK(language_for_path("main.zig") == "zig");   // new mapping
    CHECK(language_for_path("sub/Makefile") == "bash");  // filename match, case-insensitive
    CHECK(language_for_path("bar.c") == "c");        // untouched built-ins still work
    language_set_overrides({});
    CHECK(language_for_path("a.h") == "c");
    CHECK(language_for_path("main.zig") == "");
}

TEST_CASE("group_for_capture maps dotted capture names") {
    CHECK(group_for_capture("keyword") == HighlightGroup::Keyword);
    CHECK(group_for_capture("keyword.control.return") == HighlightGroup::Keyword);
    CHECK(group_for_capture("function.method") == HighlightGroup::Method);
    CHECK(group_for_capture("type.builtin") == HighlightGroup::TypeBuiltin);
    CHECK(group_for_capture("string") == HighlightGroup::String);
    CHECK(group_for_capture("comment.line") == HighlightGroup::Comment);
    CHECK(group_for_capture("totally.unknown") == HighlightGroup::None);
}

TEST_CASE("C source highlights into keyword/comment/string/number runs") {
    if (!highlighting_available()) {
        return;  // built without tree-sitter
    }
    const std::string src =
        "// a comment\n"
        "int main(void) {\n"
        "  const char* s = \"hi\";\n"
        "  return 0;\n"
        "}\n";

    auto lines = highlight_source(src, "c");
    REQUIRE(lines.size() >= 5);

    bool kw = false, comment = false, str = false, num = false;
    for (const auto& line : lines) {
        for (const auto& run : line) {
            CHECK(run.start < run.end);
            switch (run.group) {
                case HighlightGroup::Keyword: kw = true; break;
                case HighlightGroup::Comment: comment = true; break;
                case HighlightGroup::String:  str = true; break;
                case HighlightGroup::Number:  num = true; break;
                default: break;
            }
        }
    }
    CHECK(comment);
    CHECK(kw);
    CHECK(str);
    CHECK(num);

    // The comment occupies the whole first line, starting at column 0.
    REQUIRE_FALSE(lines[0].empty());
    CHECK(lines[0][0].group == HighlightGroup::Comment);
    CHECK(lines[0][0].start == 0);
}

TEST_CASE("each bundled grammar parses and highlights") {
    if (!highlighting_available()) {
        return;
    }
    struct Sample {
        Language lang;
        const char* src;
    };
    const Sample samples[] = {
        {"cpp", "// hi\nclass A { int x = 1; };\n"},
        {"go", "// hi\npackage main\nfunc main() {}\n"},
        {"rust", "// hi\nfn main() { let x = 1; }\n"},
        {"java", "// hi\nclass A { int x = 1; }\n"},
        {"python", "# hi\ndef f():\n    return 1\n"},
        {"javascript", "// hi\nconst x = 'hi';\n"},
        {"typescript", "// hi\nconst x: number = 1;\n"},
        {"tsx", "// hi\nconst x = 1;\n"},
        {"ruby", "# hi\ndef f\n  return 1\nend\n"},
        {"bash", "# hi\nif true; then\n  echo hi\nfi\n"},
        {"c_sharp", "// hi\nclass A { int X = 1; }\n"},
        {"html", "<!-- hi -->\n<div class=\"x\">y</div>\n"},
        {"css", "/* hi */\na { color: red; }\n"},
        {"lua", "-- hi\nlocal x = 1\n"},
        {"toml", "# hi\nkey = 1\n"},
        {"cmake", "# hi\nset(MY_VAR 1)\n"},
    };
    for (const auto& s : samples) {
        CAPTURE(s.lang);
        auto lines = highlight_source(s.src, s.lang);
        bool comment = false;
        int total_runs = 0;
        for (const auto& line : lines) {
            total_runs += static_cast<int>(line.size());
            for (const auto& run : line) {
                if (run.group == HighlightGroup::Comment) comment = true;
            }
        }
        // The grammar loaded (ABI-compatible), the query matched, and the
        // leading comment is recognised. (Markup grammars have no keywords, so
        // a generic "has highlights + has the comment" check fits every case.)
        CHECK(total_runs > 0);
        CHECK(comment);
    }
}

TEST_CASE("markdown highlights (no line-comment syntax)") {
    if (!highlighting_available()) {
        return;
    }
    auto lines = highlight_source("# Heading\n\nsome **bold** text\n", "markdown");
    int total = 0;
    for (const auto& line : lines) {
        total += static_cast<int>(line.size());
    }
    CHECK(total > 0);  // grammar loaded and the query matched something
}

TEST_CASE("syntax colour overrides apply, isolate, and clear") {
    clear_syntax_overrides();
    const HlRgb base = syntax_color(HighlightGroup::Keyword, /*light=*/false);

    set_syntax_color_override(HighlightGroup::Keyword, HlRgb{1, 2, 3});
    HlRgb got = syntax_color(HighlightGroup::Keyword, false);
    CHECK(got.r == 1);
    CHECK(got.g == 2);
    CHECK(got.b == 3);
    // The override also wins for the light variant.
    CHECK(syntax_color(HighlightGroup::Keyword, true).r == 1);
    // Other groups are unaffected.
    HlRgb str = syntax_color(HighlightGroup::String, false);
    const bool str_overridden = (str.r == 1 && str.g == 2 && str.b == 3);
    CHECK_FALSE(str_overridden);
    // None is never overridden.
    set_syntax_color_override(HighlightGroup::None, HlRgb{9, 9, 9});

    clear_syntax_overrides();
    HlRgb restored = syntax_color(HighlightGroup::Keyword, false);
    CHECK(restored.r == base.r);
    CHECK(restored.g == base.g);
    CHECK(restored.b == base.b);
}

TEST_CASE("unknown language yields no highlights") {
    auto lines = highlight_source("plain text\nmore text\n", "");
    bool any = false;
    for (const auto& line : lines) {
        any = any || !line.empty();
    }
    CHECK_FALSE(any);
}
