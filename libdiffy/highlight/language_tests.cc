// language_from_name resolves the --language/-L token, and language_list
// enumerates the grammars diffy detects out of the box (used by --language help).

#include "highlight/language.hpp"

#include <doctest.h>

#include <algorithm>

using namespace diffy;

TEST_CASE("language_from_name accepts grammar names, extensions, and dotted forms") {
    // Bare extension tokens resolve like a filename would.
    CHECK(language_from_name("cpp") == "cpp");
    CHECK(language_from_name("py") == "python");
    CHECK(language_from_name("rb") == "ruby");
    CHECK(language_from_name("cs") == "c_sharp");
    CHECK(language_from_name("c") == "c");

    // A leading dot is tolerated ("cpp" and ".cpp" mean the same thing).
    CHECK(language_from_name(".cpp") == "cpp");
    CHECK(language_from_name(".py") == "python");

    // Case-insensitive.
    CHECK(language_from_name("CPP") == "cpp");
    CHECK(language_from_name("Python") == "python");

    // Raw grammar names that are not extensions pass through unchanged.
    CHECK(language_from_name("c_sharp") == "c_sharp");
    CHECK(language_from_name("typescript") == "typescript");
    CHECK(language_from_name("bash") == "bash");

    // Empty stays empty (means "detect from the file name").
    CHECK(language_from_name("").empty());
}

TEST_CASE("language_list is sorted, de-duped, and covers the built-in grammars") {
    const auto langs = language_list();
    REQUIRE(!langs.empty());

    // Sorted and unique.
    CHECK(std::is_sorted(langs.begin(), langs.end()));
    CHECK(std::adjacent_find(langs.begin(), langs.end()) == langs.end());

    // A grammar that maps from several extensions appears exactly once.
    CHECK(std::count(langs.begin(), langs.end(), "cpp") == 1);

    // Representative languages are present.
    for (const char* expected : {"c", "cpp", "c_sharp", "python", "ruby", "rust",
                                 "bash", "typescript", "markdown"}) {
        CAPTURE(expected);
        CHECK(std::find(langs.begin(), langs.end(), expected) != langs.end());
    }

    // Every listed grammar round-trips through language_from_name.
    for (const auto& l : langs) {
        CAPTURE(l);
        CHECK(language_from_name(l) == l);
    }
}
