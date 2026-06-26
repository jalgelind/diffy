#include "app-window.h"

#include "config/gui_config.hpp"
#include "config/repos.hpp"
#include "diff_bridge.hpp"
#include "render/diff_pipeline.hpp"
#include "repo_model.hpp"

#include <slint.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace diffy;
using diffy::gui::Repo;

namespace {

slint::SharedString
ss(const std::string& s) {
    return slint::SharedString(s.c_str());
}

std::string
str(const slint::SharedString& s) {
    return std::string(s.data());
}

}  // namespace

// Headless end-to-end check of the GUI data path (no window): open a repo,
// list its changes, diff the first changed file, and build the Slint row model.
// Exercises repo_model (libgit2) -> diff_pipeline (libdiffy) -> diff_bridge.
static int
run_selftest(const std::string& repo_path, const GuiSettings& settings) {
    auto r = Repo::open(repo_path);
    if (!r) {
        std::fprintf(stderr, "selftest: cannot open git repo at '%s'\n", repo_path.c_str());
        return 1;
    }
    auto files = r->status();
    auto commits = r->recent_commits(5);
    std::fprintf(stderr, "selftest: branch=%s changed-files=%zu recent-commits=%zu\n",
                 r->head_branch().c_str(), files.size(), commits.size());

    // Verify the commit -> changed-files path (git diff-tree against parent).
    if (!commits.empty()) {
        auto cf = r->commit_files(commits.front().oid);
        std::fprintf(stderr, "selftest: HEAD commit %s changed %zu file(s)%s\n",
                     commits.front().short_oid.c_str(), cf.size(),
                     cf.empty() ? "" : (" e.g. '" + cf.front().path + "'").c_str());
    }

    // Exercise the repos.conf round-trip (load -> add -> save -> reload).
    {
        auto repos = repos_load();
        repos_add(repos, r->workdir().empty() ? repo_path : r->workdir());
        repos_save(repos);
        auto reloaded = repos_load();
        std::fprintf(stderr, "selftest: repos.conf round-trip -> %zu repo(s), first='%s'\n",
                     reloaded.size(), reloaded.empty() ? "" : reloaded.front().name.c_str());
    }

    if (files.empty()) {
        std::fprintf(stderr, "selftest: no working-tree changes to diff (ok)\n");
        return 0;
    }

    // Find the first change that actually has file content to diff (skip
    // directories / empty entries), then run the full pipeline + bridge on it.
    for (const auto& f : files) {
        auto pair = r->diff_workdir_file(f.path);
        if (pair.old_text.empty() && pair.new_text.empty()) {
            continue;
        }
        for (auto mode : {ViewMode::SideBySide, ViewMode::Unified}) {
            DiffPipelineOptions p;
            DiffLayoutOptions l;
            l.mode = mode;
            auto vm = build_diff_view_from_text(pair.old_text, pair.new_text, pair.old_name,
                                                pair.new_name, p, l);
            auto model = diffy::gui::build_row_model(vm, settings);
            std::fprintf(stderr, "selftest: '%s' (%s) [%s] -> %zu view rows, %d slint rows\n",
                         f.path.c_str(), f.status.c_str(),
                         mode == ViewMode::SideBySide ? "side-by-side" : "unified", vm.rows.size(),
                         static_cast<int>(model->row_count()));
        }
        return 0;
    }
    std::fprintf(stderr, "selftest: no diffable file content found (ok)\n");
    return 0;
}

int
main(int argc, char** argv) {
    diffy::gui::git_runtime_init();

    GuiSettings settings;
    gui_settings_load(settings);

    if (argc >= 3 && std::strcmp(argv[1], "--selftest") == 0) {
        int rc = run_selftest(argv[2], settings);
        diffy::gui::git_runtime_shutdown();
        return rc;
    }

    auto ui = AppWindow::create();
    // global<T>() returns `const T&`; Slint's set_/on_ accessors are const-qualified.
    const auto& backend = ui->global<Backend>();
    const auto& options = ui->global<DiffOptions>();

    // Per-run application state.
    struct State {
        std::optional<Repo> repo;
        std::vector<RepoEntry> repos;
        std::string current_file;
        std::string current_commit;  // empty => working-tree diff
        diffy::gui::BlobPair pair;
        DiffComputation computation;
        GuiSettings settings;
    } state;
    state.settings = settings;
    state.repos = repos_load();

    // --- typography + initial option-bar state from [gui] -------------------
    // "monospace" is not a real macOS family; map the generic/empty value to a
    // concrete monospace font so the diff text actually aligns crisply.
    std::string mono = settings.font_family;
    if (mono.empty() || mono == "monospace") {
        mono = "Menlo";
    }
    backend.set_mono_font(ss(mono));
    backend.set_font_size(static_cast<int>(settings.font_size));
    if (settings.theme_variant == "light") {
        backend.set_bg(slint::Color::from_argb_uint8(255, 0xff, 0xff, 0xff));
        backend.set_panel_bg(slint::Color::from_argb_uint8(255, 0xf3, 0xf3, 0xf3));
        backend.set_fg(slint::Color::from_argb_uint8(255, 0x24, 0x29, 0x2e));
        backend.set_gutter_fg(slint::Color::from_argb_uint8(255, 0x8a, 0x8a, 0x8a));
    }
    options.set_side_by_side(settings.view_mode() == ViewMode::SideBySide);
    options.set_show_line_numbers(settings.show_line_numbers);
    options.set_word_wrap(settings.word_wrap);
    options.set_token_granularity(true);
    options.set_context_lines(3);
    options.set_algorithm(0);

    // --- helpers ------------------------------------------------------------
    auto set_repo_names = [&]() {
        auto names = std::make_shared<slint::VectorModel<slint::SharedString>>();
        for (auto& r : state.repos) {
            names->push_back(ss(r.name));
        }
        backend.set_repo_names(names);
    };

    auto pipeline_opts = [&]() {
        DiffPipelineOptions p;
        const int a = options.get_algorithm();
        p.algorithm = a == 1 ? Algo::kMyersGreedy : a == 2 ? Algo::kMyersLinear : Algo::kPatience;
        p.context_lines = options.get_context_lines();
        p.granularity =
            options.get_token_granularity() ? EditGranularity::Token : EditGranularity::Line;
        p.ignore_whitespace = options.get_ignore_whitespace();
        return p;
    };

    auto layout_opts = [&]() {
        DiffLayoutOptions l;
        l.mode = options.get_side_by_side() ? ViewMode::SideBySide : ViewMode::Unified;
        return l;
    };

    // layout-only refresh: reuse the cached annotated hunks
    auto relayout = [&]() {
        auto input = state.computation.input();
        auto vm = build_diff_view(input, state.computation.hunks, layout_opts());

        // Longest line (in characters) drives the horizontal scroll extent.
        int max_cols = 0;
        for (const auto& row : vm.rows) {
            int left = 0, right = 0;
            for (const auto& s : row.left.spans) {
                left += static_cast<int>(s.text.size());
            }
            for (const auto& s : row.right.spans) {
                right += static_cast<int>(s.text.size());
            }
            max_cols = std::max(max_cols, std::max(left, right));
        }
        backend.set_max_cols(max_cols);
        backend.set_rows(diffy::gui::build_row_model(vm, state.settings));
    };

    // full refresh: re-run the diff for the current blob pair, then lay out
    auto recompute = [&]() {
        if (!state.pair.ok) {
            return;
        }
        state.computation = compute_annotated_diff(state.pair.old_text, state.pair.new_text,
                                                   state.pair.old_name, state.pair.new_name,
                                                   pipeline_opts());
        relayout();
    };

    auto populate_repo = [&]() {
        if (!state.repo) {
            return;
        }
        backend.set_branch(ss(state.repo->head_branch()));

        auto files = std::make_shared<slint::VectorModel<FileEntry>>();
        for (auto& f : state.repo->status()) {
            FileEntry fe;
            fe.path = ss(f.path);
            fe.status = ss(f.status);
            files->push_back(fe);
        }
        backend.set_files(files);

        auto commits = std::make_shared<slint::VectorModel<CommitEntry>>();
        for (auto& c : state.repo->recent_commits(50)) {
            CommitEntry ce;
            ce.oid = ss(c.oid);
            ce.short_oid = ss(c.short_oid);
            ce.summary = ss(c.summary);
            commits->push_back(ce);
        }
        backend.set_commits(commits);
    };

    // Assigned once open_file_workdir exists; auto-opens the first changed file
    // that has real content so the diff view isn't empty after opening a repo.
    std::function<void()> auto_select_first;

    auto load_repo = [&](const std::string& path) {
        auto r = Repo::open(path);
        if (!r) {
            backend.set_status_text(ss("Not a git repository: " + path));
            return;
        }
        state.repo = std::move(r);
        state.current_commit.clear();  // opening a repo starts in working-tree mode
        const std::string wd = state.repo->workdir().empty() ? path : state.repo->workdir();
        repos_add(state.repos, wd);
        repos_save(state.repos);
        set_repo_names();
        populate_repo();
        backend.set_status_text(ss("Opened " + wd));
        if (auto_select_first) {
            auto_select_first();
        }
    };

    // Open a file's diff. Honours the current mode: working-tree (HEAD vs disk)
    // or a selected commit (commit vs its parent).
    auto open_file = [&](const std::string& path) {
        if (!state.repo) {
            return;
        }
        state.current_file = path;
        if (state.current_commit.empty()) {
            state.pair = state.repo->diff_workdir_file(path);
            backend.set_status_text(ss("Working tree: " + path));
        } else {
            state.pair = state.repo->diff_commit_file(state.current_commit, path);
            backend.set_status_text(
                ss("Commit " + state.current_commit.substr(0, 8) + ": " + path));
        }
        backend.set_current_file(ss(path));
        recompute();
    };

    auto set_files = [&](const std::vector<diffy::gui::FileChange>& changes) {
        auto files = std::make_shared<slint::VectorModel<FileEntry>>();
        for (auto& f : changes) {
            FileEntry fe;
            fe.path = ss(f.path);
            fe.status = ss(f.status);
            files->push_back(fe);
        }
        backend.set_files(files);
    };

    auto_select_first = [&]() {
        if (!state.repo) {
            return;
        }
        for (auto& f : state.repo->status()) {
            auto pair = state.repo->diff_workdir_file(f.path);
            if (!(pair.old_text.empty() && pair.new_text.empty())) {
                open_file(f.path);
                return;
            }
        }
    };

    // --- callbacks ----------------------------------------------------------
    backend.on_open_repo([&](slint::SharedString p) { load_repo(str(p)); });
    backend.on_select_repo_index([&](int i) {
        if (i >= 0 && i < static_cast<int>(state.repos.size())) {
            load_repo(state.repos[i].path);
        }
    });
    backend.on_select_file([&](slint::SharedString p) { open_file(str(p)); });
    backend.on_select_commit([&](slint::SharedString oid) {
        if (!state.repo) {
            return;
        }
        // Switch into "browse this commit" mode: list its files and diff the first.
        state.current_commit = str(oid);
        auto changes = state.repo->commit_files(state.current_commit);
        set_files(changes);
        backend.set_status_text(ss("Commit " + state.current_commit.substr(0, 8) + " — " +
                                   std::to_string(changes.size()) + " file(s) — pick the repo to return"));
        if (!changes.empty()) {
            open_file(changes.front().path);
        } else {
            backend.set_rows(std::make_shared<slint::VectorModel<DiffRowData>>());
            backend.set_current_file(ss(""));
        }
    });
    options.on_relayout([&]() {
        if (state.pair.ok) {
            relayout();
        }
    });
    options.on_rediff([&]() { recompute(); });

    set_repo_names();
    if (settings.restore_last_repo && !state.repos.empty()) {
        load_repo(state.repos.front().path);
    }

    ui->run();

    // --- persist on exit ----------------------------------------------------
    gui_settings_save(state.settings);
    repos_save(state.repos);
    diffy::gui::git_runtime_shutdown();
    return 0;
}
