// Slint-free tests for the reusable git layer (repo_model). They prove the
// model can be driven entirely without the UI — the same way a future win32
// frontend would — and exercise the write operations against throwaway temp
// repositories (never a real one).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "repo_model.hpp"

#include <git2.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

using namespace diffy::gui;
namespace fs = std::filesystem;

namespace {

void
write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << content;
}

// A fresh git repo in a unique temp directory, with user identity configured so
// commits can be created. Removes itself on destruction.
struct TempRepo {
    std::string dir;

    TempRepo() {
        static std::atomic<int> counter{0};
        dir = (fs::temp_directory_path() /
               ("diffy_repo_test_" + std::to_string(counter.fetch_add(1))))
                  .string();
        fs::remove_all(dir);
        fs::create_directories(dir);

        git_libgit2_init();
        git_repository* repo = nullptr;
        REQUIRE(git_repository_init(&repo, dir.c_str(), 0) == 0);
        git_config* cfg = nullptr;
        REQUIRE(git_repository_config(&cfg, repo) == 0);
        git_config_set_string(cfg, "user.name", "Diffy Test");
        git_config_set_string(cfg, "user.email", "test@diffy.invalid");
        git_config_free(cfg);
        git_repository_free(repo);
    }
    ~TempRepo() {
        std::error_code ec;
        fs::remove_all(dir, ec);
        git_libgit2_shutdown();
    }

    std::string file(const std::string& rel) const { return (fs::path(dir) / rel).string(); }
};

}  // namespace

TEST_CASE("repo_model: stage then commit clears the working tree") {
    TempRepo tr;
    write_file(tr.file("a.txt"), "hello\n");

    auto repo = Repo::open(tr.dir);
    REQUIRE(repo.has_value());

    // The new file shows up as a change, unstaged.
    auto changes = repo->status();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].path == "a.txt");

    CHECK(repo->stage_file("a.txt"));
    CHECK(repo->commit("initial commit"));

    // Nothing left to commit, and the commit is in the log.
    CHECK(repo->status().empty());
    auto commits = repo->recent_commits(10);
    REQUIRE(commits.size() == 1);
    CHECK(commits[0].summary == "initial commit");
}

TEST_CASE("repo_model: unstage moves a change back out of the index") {
    TempRepo tr;
    write_file(tr.file("a.txt"), "hello\n");
    auto repo = Repo::open(tr.dir);
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage_file("a.txt"));
    REQUIRE(repo->commit("init"));

    write_file(tr.file("a.txt"), "hello world\n");
    CHECK(repo->stage_file("a.txt"));
    {
        auto st = repo->status();
        REQUIRE(st.size() == 1);
        CHECK(st[0].staged == true);
    }
    CHECK(repo->unstage_file("a.txt"));
    {
        auto st = repo->status();
        REQUIRE(st.size() == 1);
        CHECK(st[0].staged == false);
    }
}

TEST_CASE("repo_model: discard restores a tracked file and deletes an untracked one") {
    TempRepo tr;
    write_file(tr.file("a.txt"), "original\n");
    auto repo = Repo::open(tr.dir);
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage_file("a.txt"));
    REQUIRE(repo->commit("init"));

    // Tracked modification → restored to HEAD content.
    write_file(tr.file("a.txt"), "changed\n");
    REQUIRE(repo->status().size() == 1);
    CHECK(repo->discard_changes("a.txt"));
    CHECK(repo->status().empty());
    {
        std::ifstream f(tr.file("a.txt"));
        std::string s((std::istreambuf_iterator<char>(f)), {});
        CHECK(s == "original\n");
    }

    // Untracked file → removed from disk.
    write_file(tr.file("new.txt"), "junk\n");
    REQUIRE(repo->status().size() == 1);
    CHECK(repo->discard_changes("new.txt"));
    CHECK_FALSE(fs::exists(tr.file("new.txt")));
}

TEST_CASE("repo_model: a wholly-untracked directory is one named '?' entry, not empty") {
    TempRepo tr;
    write_file(tr.file("a.txt"), "x\n");
    auto repo = Repo::open(tr.dir);
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage_file("a.txt"));
    REQUIRE(repo->commit("init"));

    // libgit2 collapses an untracked directory into a single entry whose path
    // carries a trailing slash; status() must strip it so the UI shows a named row
    // ("build-out") instead of an empty-named "?" leaf under a phantom folder.
    fs::create_directories(fs::path(tr.dir) / "build-out" / "nested");
    write_file(tr.file("build-out/x.o"), "obj\n");
    write_file(tr.file("build-out/nested/y.o"), "obj\n");

    auto changes = repo->status();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].status == "?");
    CHECK(changes[0].path == "build-out");    // no trailing slash
    CHECK_FALSE(changes[0].path.empty());
    CHECK(changes[0].path.back() != '/');
}

TEST_CASE("repo_model: branches lists the current branch and checkout switches") {
    TempRepo tr;
    write_file(tr.file("a.txt"), "x\n");
    auto repo = Repo::open(tr.dir);
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage_file("a.txt"));
    REQUIRE(repo->commit("init"));

    const std::string start = repo->head_branch();
    auto branches = repo->branches();
    REQUIRE(branches.size() >= 1);
    bool found_current = false;
    for (const auto& b : branches) {
        if (b.current) {
            found_current = true;
            CHECK(b.name == start);
        }
    }
    CHECK(found_current);

    // Create a second branch via libgit2 directly, then check it out.
    {
        git_repository* raw = nullptr;
        REQUIRE(git_repository_open(&raw, tr.dir.c_str()) == 0);
        git_object* head = nullptr;
        REQUIRE(git_revparse_single(&head, raw, "HEAD") == 0);
        git_reference* newref = nullptr;
        git_branch_create(&newref, raw, "feature",
                          reinterpret_cast<git_commit*>(head), 0);
        if (newref) git_reference_free(newref);
        git_object_free(head);
        git_repository_free(raw);
    }

    CHECK(repo->checkout_branch("feature"));
    CHECK(repo->head_branch() == "feature");
}

TEST_CASE("repo_model: amend rewrites HEAD without adding a commit") {
    TempRepo tr;
    write_file(tr.file("a.txt"), "x\n");
    auto repo = Repo::open(tr.dir);
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage_file("a.txt"));
    REQUIRE(repo->commit("typo"));

    CHECK(repo->commit("fixed message", /*amend=*/true));
    auto commits = repo->recent_commits(10);
    REQUIRE(commits.size() == 1);
    CHECK(commits[0].summary == "fixed message");
}
