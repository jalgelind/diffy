#include "repos.hpp"

#include "config/config.hpp"  // config_get_directory

#include <fmt/format.h>
#include <config_parser/config_parser.hpp>
#include <config_parser/config_parser_utils.hpp>
#include <config_parser/config_serializer.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

std::string
basename_of(const std::string& path) {
    fs::path p{path};
    std::string name = p.filename().string();
    if (name.empty()) {
        name = p.parent_path().filename().string();
    }
    return name.empty() ? path : name;
}

std::string
canonicalise(const std::string& path) {
    std::error_code ec;
    auto c = fs::weakly_canonical(fs::path{path}, ec);
    return ec ? path : c.string();
}

}  // namespace

std::string
diffy::repos_config_path() {
    return fmt::format("{}/repos.conf", diffy::config_get_directory());
}

std::vector<diffy::RepoEntry>
diffy::repos_load() {
    std::vector<RepoEntry> out;

    ParseResult parse_result;
    Value table;
    if (!cfg_load_file(repos_config_path(), parse_result, table) || !table.is_table()) {
        return out;
    }

    std::vector<std::string> pinned;
    if (auto v = table.lookup_value_by_path("repos.pinned"); v && v->get().is_array()) {
        for (auto& e : v->get().as_array()) {
            if (e.is_string()) {
                pinned.push_back(e.as_string());
            }
        }
    }

    if (auto v = table.lookup_value_by_path("repos.recent"); v && v->get().is_array()) {
        for (auto& e : v->get().as_array()) {
            if (!e.is_string()) {
                continue;
            }
            RepoEntry re;
            re.path = e.as_string();
            re.name = basename_of(re.path);
            re.pinned = std::find(pinned.begin(), pinned.end(), re.path) != pinned.end();
            out.push_back(std::move(re));
        }
    }

    return out;
}

void
diffy::repos_save(const std::vector<RepoEntry>& repos) {
    // The config format is a table of sections; top-level keys must be tables.
    // So the repo lists live under a [repos] section.
    Value recent{Value::Array{}};
    Value pinned{Value::Array{}};

    for (const auto& r : repos) {
        recent.as_array().push_back(Value{r.path});
        if (r.pinned) {
            pinned.as_array().push_back(Value{r.path});
        }
    }

    Value section;  // default-constructs to an empty table
    section["recent"] = recent;
    section["pinned"] = pinned;

    Value table;  // root table of sections
    table["repos"] = section;
    table["repos"].key_comments.push_back(
        "# Repositories opened in diffy-gui, most-recent first.");

    std::error_code ec;
    fs::create_directories(diffy::config_get_directory(), ec);

    std::ofstream f(repos_config_path(), std::ios::binary);
    if (f) {
        f << cfg_serialize(table);
    }
}

void
diffy::repos_add(std::vector<RepoEntry>& repos, const std::string& path) {
    const std::string canonical = canonicalise(path);

    bool was_pinned = false;
    repos.erase(std::remove_if(repos.begin(), repos.end(),
                               [&](const RepoEntry& r) {
                                   if (r.path == canonical) {
                                       was_pinned = was_pinned || r.pinned;
                                       return true;
                                   }
                                   return false;
                               }),
                repos.end());

    RepoEntry re;
    re.path = canonical;
    re.name = basename_of(canonical);
    re.pinned = was_pinned;
    repos.insert(repos.begin(), std::move(re));
}

void
diffy::repos_remove(std::vector<RepoEntry>& repos, const std::string& path) {
    const std::string canonical = canonicalise(path);
    repos.erase(std::remove_if(repos.begin(), repos.end(),
                               [&](const RepoEntry& r) { return r.path == canonical; }),
                repos.end());
}

void
diffy::repos_set_pinned(std::vector<RepoEntry>& repos, const std::string& path, bool pinned) {
    const std::string canonical = canonicalise(path);
    for (auto& r : repos) {
        if (r.path == canonical) {
            r.pinned = pinned;
        }
    }
}
