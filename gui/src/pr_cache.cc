#include "pr_cache.hpp"

#include "config/config.hpp"  // config_get_directory

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;
using diffy::review::ApprovalState;
using diffy::review::PullRequest;
using diffy::review::Reviewer;

namespace {

std::string
cache_path() {
    return diffy::config_get_directory() + "/pr_cache.json";
}

json
to_json(const PullRequest& p) {
    json reviewers = json::array();
    for (const auto& r : p.reviewers) {
        reviewers.push_back({{"id", r.id}, {"name", r.name}, {"avatar", r.avatar},
                             {"approved", r.approved}});
    }
    return json{
        {"id", p.id},
        {"title", p.title},
        {"description", p.description},
        {"author", p.author},
        {"author_id", p.author_id},
        {"author_avatar", p.author_avatar},
        {"src", p.src_branch},
        {"dst", p.dst_branch},
        {"state", static_cast<int>(p.state)},
        {"draft", p.draft},
        {"comments", p.comment_count},
        {"updated", p.updated},
        {"reviewers", reviewers},
    };
}

PullRequest
from_json(const json& j) {
    PullRequest p;
    p.id = j.value("id", std::string{});
    p.title = j.value("title", std::string{});
    p.description = j.value("description", std::string{});
    p.author = j.value("author", std::string{});
    p.author_id = j.value("author_id", std::string{});
    p.author_avatar = j.value("author_avatar", std::string{});
    p.src_branch = j.value("src", std::string{});
    p.dst_branch = j.value("dst", std::string{});
    p.state = static_cast<ApprovalState>(j.value("state", 0));
    p.draft = j.value("draft", false);
    p.comment_count = j.value("comments", 0);
    p.updated = j.value("updated", std::string{});
    if (auto it = j.find("reviewers"); it != j.end() && it->is_array()) {
        for (const auto& r : *it) {
            Reviewer rv;
            rv.id = r.value("id", std::string{});
            rv.name = r.value("name", std::string{});
            rv.avatar = r.value("avatar", std::string{});
            rv.approved = r.value("approved", false);
            p.reviewers.push_back(std::move(rv));
        }
    }
    return p;
}

}  // namespace

diffy::gui::PrListCache
diffy::gui::pr_cache_load() {
    PrListCache out;
    std::ifstream f(cache_path(), std::ios::binary);
    if (!f) {
        return out;
    }
    const json root = json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object() || !root.contains("repos")) {
        return out;
    }
    const json& repos = root["repos"];
    if (!repos.is_object()) {
        return out;
    }
    for (const auto& kv : repos.items()) {
        if (!kv.value().is_array()) {
            continue;
        }
        std::vector<PullRequest> prs;
        prs.reserve(kv.value().size());
        for (const auto& j : kv.value()) {
            prs.push_back(from_json(j));
        }
        out[kv.key()] = std::move(prs);
    }
    return out;
}

void
diffy::gui::pr_cache_save(const PrListCache& cache) {
    json repos = json::object();
    for (const auto& kv : cache) {
        json arr = json::array();
        for (const auto& p : kv.second) {
            arr.push_back(to_json(p));
        }
        repos[kv.first] = std::move(arr);
    }
    json root;
    root["version"] = 1;
    root["repos"] = std::move(repos);

    std::error_code ec;
    fs::create_directories(diffy::config_get_directory(), ec);
    std::ofstream f(cache_path(), std::ios::binary);
    if (f) {
        f << root.dump();
    }
}
