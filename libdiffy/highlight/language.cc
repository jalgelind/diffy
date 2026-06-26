#include "highlight/language.hpp"
#include "highlight/language_ts.hpp"

#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace diffy {

namespace {

std::string
lower_ext(std::string_view path) {
    namespace fs = std::filesystem;
    std::string ext = fs::path(std::string(path)).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

}  // namespace

Language
language_for_path(std::string_view path) {
    static const std::unordered_map<std::string, Language> by_ext = {
        {".c", Language::C},
        {".h", Language::C},
        {".cc", Language::Cpp},   {".cpp", Language::Cpp}, {".cxx", Language::Cpp},
        {".hpp", Language::Cpp},  {".hh", Language::Cpp},  {".hxx", Language::Cpp},
        {".go", Language::Go},
        {".rs", Language::Rust},
        {".java", Language::Java},
        {".cs", Language::CSharp},
        {".py", Language::Python}, {".pyi", Language::Python},
        {".rb", Language::Ruby},
        {".sh", Language::Bash}, {".bash", Language::Bash},
        {".js", Language::JavaScript}, {".mjs", Language::JavaScript},
        {".cjs", Language::JavaScript}, {".jsx", Language::JavaScript},
        {".ts", Language::TypeScript}, {".mts", Language::TypeScript},
        {".cts", Language::TypeScript},
        {".tsx", Language::Tsx},
        {".json", Language::Json},
    };
    auto it = by_ext.find(lower_ext(path));
    return it == by_ext.end() ? Language::None : it->second;
}

}  // namespace diffy

#ifdef DIFFY_ENABLE_HIGHLIGHT

#include <map>

extern "C" {
const TSLanguage* tree_sitter_c(void);
const TSLanguage* tree_sitter_cpp(void);
const TSLanguage* tree_sitter_go(void);
const TSLanguage* tree_sitter_rust(void);
const TSLanguage* tree_sitter_java(void);
const TSLanguage* tree_sitter_c_sharp(void);
const TSLanguage* tree_sitter_python(void);
const TSLanguage* tree_sitter_ruby(void);
const TSLanguage* tree_sitter_bash(void);
const TSLanguage* tree_sitter_javascript(void);
const TSLanguage* tree_sitter_typescript(void);
const TSLanguage* tree_sitter_tsx(void);
const TSLanguage* tree_sitter_json(void);
}

namespace diffy {

// Defined in the generated ts_queries_generated.cc.
const std::map<std::string, std::string>&
ts_raw_queries();

bool
highlighting_available() {
    return true;
}

const TSLanguage*
ts_language_for(Language lang) {
    switch (lang) {
        case Language::C:          return tree_sitter_c();
        case Language::Cpp:        return tree_sitter_cpp();
        case Language::Go:         return tree_sitter_go();
        case Language::Rust:       return tree_sitter_rust();
        case Language::Java:       return tree_sitter_java();
        case Language::CSharp:     return tree_sitter_c_sharp();
        case Language::Python:     return tree_sitter_python();
        case Language::Ruby:       return tree_sitter_ruby();
        case Language::Bash:       return tree_sitter_bash();
        case Language::JavaScript: return tree_sitter_javascript();
        case Language::TypeScript: return tree_sitter_typescript();
        case Language::Tsx:        return tree_sitter_tsx();
        case Language::Json:       return tree_sitter_json();
        default:                   return nullptr;
    }
}

namespace {

// The query keys to concatenate for a language, base-first (inheritance).
std::vector<std::string>
query_chain(Language lang) {
    switch (lang) {
        case Language::C:          return {"c"};
        case Language::Cpp:        return {"c", "cpp"};  // C++ inherits C highlights
        case Language::Go:         return {"go"};
        case Language::Rust:       return {"rust"};
        case Language::Java:       return {"java"};
        case Language::CSharp:     return {"c_sharp"};
        case Language::Python:     return {"python"};
        case Language::Ruby:       return {"ruby"};
        case Language::Bash:       return {"bash"};
        case Language::JavaScript: return {"javascript"};
        case Language::TypeScript: return {"javascript", "typescript"};  // TS inherits JS
        case Language::Tsx:        return {"javascript", "tsx"};         // TSX inherits JS
        case Language::Json:       return {"json"};
        default:                   return {};
    }
}

}  // namespace

std::string
highlight_query_for(Language lang) {
    const auto& raw = ts_raw_queries();
    std::string out;
    for (const auto& key : query_chain(lang)) {
        auto it = raw.find(key);
        if (it != raw.end()) {
            out += it->second;
            out += '\n';
        }
    }
    return out;
}

}  // namespace diffy

#else  // !DIFFY_ENABLE_HIGHLIGHT

namespace diffy {

bool
highlighting_available() {
    return false;
}

const TSLanguage*
ts_language_for(Language) {
    return nullptr;
}

std::string
highlight_query_for(Language) {
    return {};
}

}  // namespace diffy

#endif
