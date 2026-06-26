#include "highlight/highlight_group.hpp"
#include "highlight/language.hpp"
#include "highlight/syntax_highlighter.hpp"

#include <doctest.h>

#include <string>

using namespace diffy;

TEST_CASE("language detection by extension") {
    CHECK(language_for_path("src/foo/bar.c") == Language::C);
    CHECK(language_for_path("a.h") == Language::C);
    CHECK(language_for_path("pkg/data.json") == Language::Json);
    CHECK(language_for_path("notes.unknownext") == Language::None);
    CHECK(language_for_path("Makefile") == Language::None);
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

    auto lines = highlight_source(src, Language::C);
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
        const char* name;
        Language lang;
        const char* src;
    };
    const Sample samples[] = {
        {"cpp", Language::Cpp, "// hi\nclass A { int x = 1; };\n"},
        {"go", Language::Go, "// hi\npackage main\nfunc main() {}\n"},
        {"rust", Language::Rust, "// hi\nfn main() { let x = 1; }\n"},
        {"java", Language::Java, "// hi\nclass A { int x = 1; }\n"},
        {"python", Language::Python, "# hi\ndef f():\n    return 1\n"},
        {"javascript", Language::JavaScript, "// hi\nconst x = 'hi';\n"},
        {"typescript", Language::TypeScript, "// hi\nconst x: number = 1;\n"},
        {"tsx", Language::Tsx, "// hi\nconst x = 1;\n"},
        {"ruby", Language::Ruby, "# hi\ndef f\n  return 1\nend\n"},
        {"bash", Language::Bash, "# hi\nif true; then\n  echo hi\nfi\n"},
        {"c_sharp", Language::CSharp, "// hi\nclass A { int X = 1; }\n"},
        {"html", Language::Html, "<!-- hi -->\n<div class=\"x\">y</div>\n"},
        {"css", Language::Css, "/* hi */\na { color: red; }\n"},
    };
    for (const auto& s : samples) {
        CAPTURE(s.name);
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

TEST_CASE("unknown language yields no highlights") {
    auto lines = highlight_source("plain text\nmore text\n", Language::None);
    bool any = false;
    for (const auto& line : lines) {
        any = any || !line.empty();
    }
    CHECK_FALSE(any);
}
