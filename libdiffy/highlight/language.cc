#include "highlight/language.hpp"
#include "highlight/language_ts.hpp"

#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace diffy {

namespace {

std::string
lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Overrides from [highlight.extensions] in diffy.conf. Written once at startup
// (language_set_overrides), read-only afterwards — no locking needed.
std::unordered_map<std::string, Language>&
override_by_ext() {
    static std::unordered_map<std::string, Language> m;
    return m;
}

std::unordered_map<std::string, Language>&
override_by_name() {
    static std::unordered_map<std::string, Language> m;
    return m;
}

}  // namespace

void
language_set_overrides(std::vector<std::pair<std::string, Language>> patterns) {
    override_by_ext().clear();
    override_by_name().clear();
    for (auto& [pattern, lang] : patterns) {
        if (pattern.empty() || lang.empty()) {
            continue;
        }
        if (pattern[0] == '.') {
            override_by_ext()[lowered(pattern)] = lang;
        } else {
            override_by_name()[lowered(pattern)] = lang;
        }
    }
}

Language
language_for_path(std::string_view path) {
    static const std::unordered_map<std::string, Language> by_ext = {
        {".c", "c"},
        {".h", "c"},
        {".cc", "cpp"},   {".cpp", "cpp"}, {".cxx", "cpp"},
        {".hpp", "cpp"},  {".hh", "cpp"},  {".hxx", "cpp"},
        {".go", "go"},
        {".rs", "rust"},
        {".java", "java"},
        {".cs", "c_sharp"},
        {".py", "python"}, {".pyi", "python"},
        {".rb", "ruby"},
        {".sh", "bash"}, {".bash", "bash"},
        {".js", "javascript"}, {".mjs", "javascript"},
        {".cjs", "javascript"}, {".jsx", "javascript"},
        {".ts", "typescript"}, {".mts", "typescript"},
        {".cts", "typescript"},
        {".tsx", "tsx"},
        {".html", "html"}, {".htm", "html"},
        {".css", "css"},
        {".lua", "lua"},
        {".toml", "toml"},
        {".cmake", "cmake"},
        {".md", "markdown"}, {".markdown", "markdown"},
        {".json", "json"},
    };
    // Languages identified by filename rather than extension.
    static const std::unordered_map<std::string, Language> by_name = {
        {"cmakelists.txt", "cmake"},
    };
    namespace fs = std::filesystem;
    const fs::path p{std::string(path)};
    const std::string fname = lowered(p.filename().string());
    const std::string ext = lowered(p.extension().string());

    // Config overrides win over the built-in map; exact filename beats extension.
    if (auto it = override_by_name().find(fname); it != override_by_name().end()) {
        return it->second;
    }
    if (auto it = override_by_ext().find(ext); it != override_by_ext().end()) {
        return it->second;
    }
    if (auto it = by_name.find(fname); it != by_name.end()) {
        return it->second;
    }
    auto it = by_ext.find(ext);
    return it == by_ext.end() ? Language{} : it->second;
}

}  // namespace diffy

#ifdef DIFFY_ENABLE_HIGHLIGHT

#include "config/config.hpp"  // config_get_directory

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <dlfcn.h>
#else
#include <dlfcn.h>
#endif

#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

namespace diffy {

namespace {

#ifdef _WIN32
constexpr const char* kLibSuffix = ".dll";
#elif defined(__APPLE__)
constexpr const char* kLibSuffix = ".dylib";
#else
constexpr const char* kLibSuffix = ".so";
#endif

namespace fs = std::filesystem;

fs::path
exe_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    return fs::path(buf).parent_path();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) {
        return {};
    }
    std::error_code ec;
    fs::path p = fs::canonical(buf, ec);
    return ec ? fs::path(buf).parent_path() : p.parent_path();
#else
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path{} : p.parent_path();
#endif
}

std::vector<fs::path>
grammar_dirs() {
    std::vector<fs::path> dirs;
    const fs::path exe = exe_dir();
    if (!exe.empty()) {
        dirs.push_back(exe / "grammars");                // installed layout
        dirs.push_back(exe.parent_path() / "grammars");  // build tree (cli/, gui/, tests/ siblings)
    }
    dirs.push_back(fs::path(config_get_directory()) / "grammars");  // user drop-ins
    return dirs;
}

// Loaded grammar + its highlights query; ts == nullptr caches a failed lookup.
struct Grammar {
    const TSLanguage* ts = nullptr;
    std::string query;
};

const TSLanguage*
load_grammar_lib(const fs::path& lib, const std::string& symbol) {
    using LangFn = const TSLanguage* (*)();
#ifdef _WIN32
    HMODULE handle = LoadLibraryW(lib.wstring().c_str());
    if (!handle) {
        return nullptr;
    }
    auto fn = reinterpret_cast<LangFn>(
        reinterpret_cast<void*>(GetProcAddress(handle, symbol.c_str())));
#else
    void* handle = dlopen(lib.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        return nullptr;
    }
    auto fn = reinterpret_cast<LangFn>(dlsym(handle, symbol.c_str()));
#endif
    // The library handle is intentionally kept for the process lifetime: the
    // TSLanguage tables live inside it.
    return fn ? fn() : nullptr;
}

const Grammar&
grammar_for(const Language& name) {
    static std::mutex mu;
    static std::map<Language, Grammar> cache;
    std::lock_guard<std::mutex> lock(mu);
    if (auto it = cache.find(name); it != cache.end()) {
        return it->second;
    }
    Grammar g;
    // The name becomes a filename; permit only [a-z0-9_] so a config entry
    // can't point the loader outside the grammar directories.
    const bool sane = !name.empty() &&
                      std::all_of(name.begin(), name.end(), [](unsigned char c) {
                          return std::islower(c) || std::isdigit(c) || c == '_';
                      });
    if (sane) {
        for (const auto& dir : grammar_dirs()) {
            std::error_code ec;
            const fs::path lib = dir / (name + kLibSuffix);
            if (!fs::exists(lib, ec)) {
                continue;
            }
            g.ts = load_grammar_lib(lib, "tree_sitter_" + name);
            if (!g.ts) {
                continue;  // wrong arch / missing symbol — try the next dir
            }
            // A grammar built against an incompatible tree-sitter ABI is caught
            // later: ts_parser_set_language() fails and highlighting degrades
            // to a no-op for that language.
            std::ifstream q(dir / (name + ".scm"), std::ios::binary);
            if (q) {
                std::stringstream ss;
                ss << q.rdbuf();
                g.query = ss.str();
            }
            break;
        }
    }
    return cache.emplace(name, std::move(g)).first->second;
}

}  // namespace

bool
highlighting_available() {
    return true;
}

const TSLanguage*
ts_language_for(const Language& lang) {
    return grammar_for(lang).ts;
}

std::string
highlight_query_for(const Language& lang) {
    return grammar_for(lang).query;
}

}  // namespace diffy

#else  // !DIFFY_ENABLE_HIGHLIGHT

namespace diffy {

bool
highlighting_available() {
    return false;
}

const TSLanguage*
ts_language_for(const Language&) {
    return nullptr;
}

std::string
highlight_query_for(const Language&) {
    return {};
}

}  // namespace diffy

#endif
