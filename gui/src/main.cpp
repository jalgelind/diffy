#include "app-window.h"

#include "config/gui_config.hpp"
#include "config/repos.hpp"
#include "diff_bridge.hpp"
#include "highlight/highlight_group.hpp"
#include "highlight/highlight_palette.hpp"
#include "render/diff_pipeline.hpp"
#include "repo_model.hpp"

#include <slint.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // keep std::min/std::max usable (see relayout())
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shobjidl.h>
#endif

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

// Native "choose a folder" dialog. Returns the picked path (UTF-8), or nullopt
// if the user cancelled or the platform has no picker.
std::optional<std::string>
pick_folder() {
#ifdef _WIN32
    std::optional<std::string> chosen;
    const bool com_ok =
        SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dialog)))) {
        DWORD opts = 0;
        dialog->GetOptions(&opts);
        dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(L"Open a git repository");
        if (SUCCEEDED(dialog->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR wpath = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wpath))) {
                    const int n =
                        WideCharToMultiByte(CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
                    if (n > 1) {
                        std::string s(static_cast<size_t>(n - 1), '\0');
                        WideCharToMultiByte(CP_UTF8, 0, wpath, -1, s.data(), n, nullptr, nullptr);
                        chosen = std::move(s);
                    }
                    CoTaskMemFree(wpath);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (com_ok) {
        CoUninitialize();
    }
    return chosen;
#else
    // macOS / Linux: shell out to the platform's native folder chooser. Keeps the
    // GUI free of AppKit/GTK link dependencies. Returns nullopt on cancel or when
    // no chooser is available.
    auto run = [](const char* cmd) -> std::optional<std::string> {
        FILE* p = popen(cmd, "r");
        if (!p) {
            return std::nullopt;
        }
        std::string out;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), p)) > 0) {
            out.append(buf, n);
        }
        const int rc = pclose(p);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) {
            out.pop_back();
        }
        if (rc != 0 || out.empty()) {
            return std::nullopt;
        }
        return out;
    };
#if defined(__APPLE__)
    // AppleScript folder chooser; `try` swallows the error raised on Cancel.
    return run(
        "osascript -e 'try' "
        "-e 'POSIX path of (choose folder with prompt \"Open a git repository\")' "
        "-e 'end try' 2>/dev/null");
#else
    if (auto p = run("zenity --file-selection --directory 2>/dev/null")) {
        return p;
    }
    return run("kdialog --getexistingdirectory ~ 2>/dev/null");
#endif
#endif
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
                         static_cast<int>(model.rows->row_count()));
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

    // Apply any [gui.syntax] colour overrides (group-name -> "#rrggbb").
    {
        auto parse_hex = [](const std::string& s) -> std::optional<diffy::HlRgb> {
            std::string h = (!s.empty() && s[0] == '#') ? s.substr(1) : s;
            if (h.size() != 6) {
                return std::nullopt;
            }
            auto nyb = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                c = static_cast<char>(c | 0x20);
                return (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
            };
            int v[6];
            for (int i = 0; i < 6; ++i) {
                if ((v[i] = nyb(h[i])) < 0) return std::nullopt;
            }
            return diffy::HlRgb{static_cast<uint8_t>(v[0] * 16 + v[1]),
                                static_cast<uint8_t>(v[2] * 16 + v[3]),
                                static_cast<uint8_t>(v[4] * 16 + v[5])};
        };
        for (const auto& [name, hex] : settings.syntax_overrides) {
            const diffy::HighlightGroup g = diffy::group_for_capture(name);
            if (g != diffy::HighlightGroup::None) {
                if (auto rgb = parse_hex(hex)) {
                    diffy::set_syntax_color_override(g, *rgb);
                }
            }
        }
    }

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
        uint64_t load_generation = 0;  // bumped per open; old async loads are dropped
        std::vector<diffy::gui::FileChange> all_files;  // unfiltered changes list
        std::string file_filter;                        // substring filter (lowercased)
    } state;
    state.settings = settings;
    state.repos = repos_load();

    // Outstanding background repo-load threads, joined before libgit2 shuts down.
    std::vector<std::thread> load_threads;

    // --- typography + initial option-bar state from [gui] -------------------
    // Map a generic/empty (or legacy macOS-only "Menlo") family to a concrete
    // monospace font that actually exists on this OS, so the diff text aligns
    // crisply instead of falling back to a font that lacks the glyphs.
    const std::string default_mono =
#if defined(_WIN32)
        "Consolas";  // shipped with Windows since Vista
#elif defined(__APPLE__)
        "Menlo";
#else
        "DejaVu Sans Mono";
#endif
    std::string mono = settings.font_family;
    if (mono.empty() || mono == "monospace") {
        mono = default_mono;
    }
#if !defined(__APPLE__)
    if (mono == "Menlo") {  // the old default; Menlo only exists on macOS
        mono = default_mono;
    }
#endif
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
    options.set_syntax_highlight(settings.syntax_highlight);
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
        p.syntax_highlight = options.get_syntax_highlight();
        state.settings.syntax_highlight = p.syntax_highlight;  // persisted on exit
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
        auto vm = build_diff_view(input, state.computation.hunks, layout_opts(),
                                  &state.computation.a_highlights,
                                  &state.computation.b_highlights);

        // build_row_model expands tabs and reports the widest line (in display
        // columns), which drives the horizontal scroll extent.
        auto model = diffy::gui::build_row_model(vm, state.settings);
        backend.set_max_cols(model.max_cols);
        backend.set_rows(model.rows);
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

    // Rebuild the Slint file model from state.all_files, honouring the filter.
    auto render_files = [&]() {
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        };
        const std::string& needle = state.file_filter;
        auto files = std::make_shared<slint::VectorModel<FileEntry>>();
        for (const auto& f : state.all_files) {
            if (!needle.empty() && lower(f.path).find(needle) == std::string::npos) {
                continue;
            }
            FileEntry fe;
            fe.path = ss(f.path);
            fe.status = ss(f.status);
            fe.staged = f.staged;
            const auto slash = f.path.find_last_of('/');
            if (slash == std::string::npos) {
                fe.name = ss(f.path);
                fe.dir = ss("");
            } else {
                fe.name = ss(f.path.substr(slash + 1));
                fe.dir = ss(f.path.substr(0, slash + 1));
            }
            files->push_back(fe);
        }
        backend.set_files(files);
    };

    auto set_files = [&](const std::vector<diffy::gui::FileChange>& changes) {
        state.all_files = changes;
        render_files();
    };

    auto set_commits = [&](const std::vector<diffy::gui::CommitInfo>& commits) {
        auto model = std::make_shared<slint::VectorModel<CommitEntry>>();
        for (auto& c : commits) {
            CommitEntry ce;
            ce.oid = ss(c.oid);
            ce.short_oid = ss(c.short_oid);
            ce.summary = ss(c.summary);
            model->push_back(ce);
        }
        backend.set_commits(model);
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

    // A repository opens in two stages so a large repo (lots of submodules /
    // untracked files) shows something useful immediately: the cheap branch +
    // recent-commit info lands first, then the working-tree status (the slow
    // part) and the first diff arrive when ready. Each stage is gen-checked so a
    // newer open started meanwhile discards stale results.

    // Stage 1 payload — fast.
    struct LoadMeta {
        std::string workdir;
        std::string branch;
        std::vector<diffy::gui::CommitInfo> commits;
    };
    // Stage 2 payload — after status().
    struct LoadFiles {
        std::vector<diffy::gui::FileChange> files;
        std::string first_file;  // first changed file with real content ("" if none)
        std::optional<Repo> repo;
    };

    auto apply_meta = [&](std::shared_ptr<LoadMeta> m, uint64_t gen) {
        if (gen != state.load_generation) {
            return;
        }
        repos_add(state.repos, m->workdir);
        repos_save(state.repos);
        set_repo_names();
        backend.set_branch(ss(m->branch));
        set_commits(m->commits);
        backend.set_status_text(ss("Reading changes in " + m->workdir + " …"));
    };

    auto apply_fail = [&](std::string path, uint64_t gen) {
        if (gen != state.load_generation) {
            return;
        }
        backend.set_loading(false);
        backend.set_status_text(ss("Not a git repository: " + path));
    };

    auto apply_files = [&](std::shared_ptr<LoadFiles> f, uint64_t gen) {
        if (gen != state.load_generation) {
            return;
        }
        state.repo = std::move(f->repo);
        state.current_commit.clear();  // opening a repo starts in working-tree mode
        backend.set_loading(false);
        set_files(f->files);
        backend.set_status_text(
            ss(std::to_string(f->files.size()) + " changed file(s)"));
        if (!f->first_file.empty()) {
            open_file(f->first_file);  // cheap: one blob + one file read
        } else {
            backend.set_rows(std::make_shared<slint::VectorModel<DiffRowData>>());
            backend.set_current_file(ss(""));
        }
    };

    auto load_repo = [&](const std::string& path) {
        const uint64_t gen = ++state.load_generation;
        backend.set_loading(true);
        backend.set_status_text(ss("Opening " + path + " …"));
        // Clear the current view so switching repos feels immediate.
        set_files({});
        set_commits({});
        backend.set_rows(std::make_shared<slint::VectorModel<DiffRowData>>());
        backend.set_branch(ss(""));
        backend.set_current_file(ss(""));

        load_threads.emplace_back([path, gen, &apply_meta, &apply_files, &apply_fail]() {
            auto r = Repo::open(path);
            if (!r) {
                slint::invoke_from_event_loop(
                    [path, gen, &apply_fail]() { apply_fail(path, gen); });
                return;
            }

            // Stage 1: branch + recent commits (fast) — show the repo at once.
            auto meta = std::make_shared<LoadMeta>();
            meta->workdir = r->workdir().empty() ? path : r->workdir();
            meta->branch = r->head_branch();
            meta->commits = r->recent_commits(50);
            slint::invoke_from_event_loop(
                [meta, gen, &apply_meta]() { apply_meta(meta, gen); });

            // Stage 2: working-tree status (slow) + first diffable file.
            auto fl = std::make_shared<LoadFiles>();
            fl->files = r->status();
            int budget = 64;  // bound the per-file scan so a huge change set can't stall
            for (const auto& f : fl->files) {
                if (budget-- <= 0) {
                    break;
                }
                auto pair = r->diff_workdir_file(f.path);
                if (!(pair.old_text.empty() && pair.new_text.empty())) {
                    fl->first_file = f.path;
                    break;
                }
            }
            fl->repo = std::move(*r);
            slint::invoke_from_event_loop(
                [fl, gen, &apply_files]() { apply_files(fl, gen); });
        });
    };

    // --- callbacks ----------------------------------------------------------
    backend.on_open_repo([&](slint::SharedString p) { load_repo(str(p)); });
    backend.on_browse_repo([&]() {
        if (auto path = pick_folder(); path && !path->empty()) {
            load_repo(*path);
        }
    });
    backend.on_select_repo_index([&](int i) {
        if (i >= 0 && i < static_cast<int>(state.repos.size())) {
            load_repo(state.repos[i].path);
        }
    });
    backend.on_select_file([&](slint::SharedString p) { open_file(str(p)); });
    backend.on_filter_files([&](slint::SharedString p) {
        std::string f = str(p);
        std::transform(f.begin(), f.end(), f.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        state.file_filter = f;
        render_files();
    });
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

    // Let any in-flight background loads finish before libgit2 is torn down.
    for (auto& t : load_threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    // --- persist on exit ----------------------------------------------------
    gui_settings_save(state.settings);
    repos_save(state.repos);
    diffy::gui::git_runtime_shutdown();
    return 0;
}
