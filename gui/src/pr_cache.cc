#include "pr_cache.hpp"

#include "config/config.hpp"  // config_get_directory

#include <config_parser/config_parser.hpp>
#include <config_parser/config_parser_utils.hpp>
#include <config_parser/config_serializer.hpp>

#include <filesystem>
#include <fstream>
#include <utility>

namespace fs = std::filesystem;
using diffy::Value;
using diffy::review::ApprovalState;
using diffy::review::PullRequest;
using diffy::review::Reviewer;

namespace {

std::string
cache_path() {
    return diffy::config_get_directory() + "/pr_cache.conf";
}

std::string
gstr(Value& t, const char* k) {
    return (t.contains(k) && t[k].is_string()) ? t[k].as_string() : std::string{};
}
int
gint(Value& t, const char* k) {
    return (t.contains(k) && t[k].is_int()) ? static_cast<int>(t[k].as_int()) : 0;
}
bool
gbool(Value& t, const char* k) {
    return (t.contains(k) && t[k].is_bool()) ? t[k].as_bool() : false;
}

Value
pr_to_value(const PullRequest& p) {
    Value v;  // default-constructs to an empty table
    v["id"] = Value{p.id};
    v["title"] = Value{p.title};
    v["description"] = Value{p.description};
    v["author"] = Value{p.author};
    v["author_id"] = Value{p.author_id};
    v["author_avatar"] = Value{p.author_avatar};
    v["src"] = Value{p.src_branch};
    v["dst"] = Value{p.dst_branch};
    v["state"] = Value{static_cast<int>(p.state)};
    v["draft"] = Value{p.draft};
    v["comments"] = Value{p.comment_count};
    v["updated"] = Value{p.updated};
    Value reviewers{Value::Array{}};
    for (const auto& r : p.reviewers) {
        Value rv;
        rv["id"] = Value{r.id};
        rv["name"] = Value{r.name};
        rv["avatar"] = Value{r.avatar};
        rv["approved"] = Value{r.approved};
        reviewers.as_array().push_back(std::move(rv));
    }
    v["reviewers"] = std::move(reviewers);
    return v;
}

PullRequest
pr_from_value(Value& v) {
    PullRequest p;
    p.id = gstr(v, "id");
    p.title = gstr(v, "title");
    p.description = gstr(v, "description");
    p.author = gstr(v, "author");
    p.author_id = gstr(v, "author_id");
    p.author_avatar = gstr(v, "author_avatar");
    p.src_branch = gstr(v, "src");
    p.dst_branch = gstr(v, "dst");
    p.state = static_cast<ApprovalState>(gint(v, "state"));
    p.draft = gbool(v, "draft");
    p.comment_count = gint(v, "comments");
    p.updated = gstr(v, "updated");
    if (v.contains("reviewers") && v["reviewers"].is_array()) {
        for (auto& rv : v["reviewers"].as_array()) {
            if (!rv.is_table()) {
                continue;
            }
            Reviewer r;
            r.id = gstr(rv, "id");
            r.name = gstr(rv, "name");
            r.avatar = gstr(rv, "avatar");
            r.approved = gbool(rv, "approved");
            p.reviewers.push_back(std::move(r));
        }
    }
    return p;
}

}  // namespace

diffy::gui::PrListCache
diffy::gui::pr_cache_load() {
    PrListCache out;
    ParseResult parse_result;
    Value table;
    if (!cfg_load_file(cache_path(), parse_result, table) || !table.is_table()) {
        return out;
    }
    auto repos = table.lookup_value_by_path("pr_cache.repos");
    if (!repos || !repos->get().is_array()) {
        return out;
    }
    for (auto& entry : repos->get().as_array()) {
        if (!entry.is_table() || !entry.contains("key") || !entry["key"].is_string()) {
            continue;
        }
        std::vector<PullRequest> prs;
        if (entry.contains("prs") && entry["prs"].is_array()) {
            for (auto& j : entry["prs"].as_array()) {
                if (j.is_table()) {
                    prs.push_back(pr_from_value(j));
                }
            }
        }
        out[entry["key"].as_string()] = std::move(prs);
    }
    return out;
}

void
diffy::gui::pr_cache_save(const PrListCache& cache) {
    // A [pr_cache] section holding an array of { key = "ws/repo", prs = [ … ] }
    // entries; the repo key is stored as a value (not a table key) to avoid '/'.
    Value repos{Value::Array{}};
    for (const auto& kv : cache) {
        Value entry;
        entry["key"] = Value{kv.first};
        Value prs{Value::Array{}};
        for (const auto& p : kv.second) {
            prs.as_array().push_back(pr_to_value(p));
        }
        entry["prs"] = std::move(prs);
        repos.as_array().push_back(std::move(entry));
    }
    Value section;
    section["repos"] = std::move(repos);
    Value table;
    table["pr_cache"] = std::move(section);
    table["pr_cache"].key_comments.push_back("# Cached open pull requests, per workspace/repo.");

    std::error_code ec;
    fs::create_directories(diffy::config_get_directory(), ec);
    std::ofstream f(cache_path(), std::ios::binary);
    if (f) {
        f << cfg_serialize(table);
    }
}
