#include "app-window.h"

#include "config/config.hpp"  // config_get_directory
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
#include <filesystem>
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

// Curated monospace families offered in the settings dropdown (the panel also
// accepts any free-text family). Common cross-platform faces follow the
// OS-native ones; Slint silently falls back if a family isn't installed.
const std::vector<std::string>&
mono_font_choices() {
    static const std::vector<std::string> fonts = {
#if defined(_WIN32)
        "Consolas", "Cascadia Code", "Cascadia Mono", "Courier New",
#elif defined(__APPLE__)
        "Menlo", "SF Mono", "Monaco", "Courier New",
#else
        "DejaVu Sans Mono", "Liberation Mono", "Noto Sans Mono", "Ubuntu Mono",
#endif
        "JetBrains Mono", "Fira Code", "Source Code Pro", "Hack", "Inconsolata",
    };
    return fonts;
}

// Theme names available for the settings dropdown: every <name>.conf in the
// config directory except the app's own diffy.conf / repos.conf.
std::vector<std::string>
list_theme_names() {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(diffy::config_get_directory(), ec), end; !ec && it != end;
         it.increment(ec)) {
        const auto& p = it->path();
        if (p.extension() != ".conf") {
            continue;
        }
        const std::string stem = p.stem().string();
        if (stem == "diffy" || stem == "repos") {
            continue;
        }
        out.push_back(stem);
    }
    std::sort(out.begin(), out.end());
    return out;
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
    const auto theme = diffy::gui::load_gui_theme(settings.theme);
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
            auto model = diffy::gui::build_row_model(vm, theme,
                                                     static_cast<int>(settings.tab_width));
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
        // Lazy commit-list paging.
        std::string repo_path;  // workdir of the open repo (for background re-walks)
        int commits_shown = 0;  // commits currently in the model
        bool commits_more = false;     // history may have more than is loaded
        bool loading_commits = false;  // a load-more is in flight
        std::shared_ptr<slint::VectorModel<CommitEntry>> commits_model;
        // Keyboard navigation: ordered ids of what's currently shown, plus the
        // active pane (0 = changes list, 1 = commits list).
        std::vector<std::string> shown_files;
        std::vector<std::string> shown_commits;
        int nav_pane = 0;
        std::string refresh_select;  // file to re-open after a refresh, if present
        std::vector<int> hunk_rows;  // model-row index of each hunk header
        int cur_hunk = -1;           // index into hunk_rows for n/p navigation
        std::vector<std::string> row_texts;  // lowercased searchable text per row
        std::vector<int> find_rows;          // model rows matching the find query
        int find_idx = -1;                   // index into find_rows
    } state;
    constexpr int kCommitPage = 50;  // commits per lazy page
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

    // Colours come from the same theme .conf the CLI uses ([gui] theme), so the
    // GUI matches `diffy`/`git diffy`. Mutable so the settings panel can switch
    // it live (apply_theme_colors re-pushes the palette; on_set_theme re-renders).
    diffy::gui::GuiTheme gui_theme = diffy::gui::load_gui_theme(settings.theme);
    auto apply_theme_colors = [&]() {
        backend.set_bg(gui_theme.bg);
        backend.set_panel_bg(gui_theme.panel_bg);
        backend.set_fg(gui_theme.fg);
        backend.set_gutter_fg(gui_theme.gutter_fg);
        backend.set_accent(gui_theme.accent);
        // A foreground that contrasts with the accent (used on selected toggles),
        // so the active segment is never white-on-light or dark-on-dark.
        const auto a = gui_theme.accent;
        const float lum = (0.299f * a.red() + 0.587f * a.green() + 0.114f * a.blue());
        backend.set_on_accent(lum > 140.0f ? slint::Color::from_rgb_uint8(0, 0, 0)
                                           : slint::Color::from_rgb_uint8(255, 255, 255));
        backend.set_header_bg(gui_theme.header_bg);
        backend.set_header_fg(gui_theme.header_fg);
        backend.set_divider(gui_theme.divider);
        // Selection/hover are accent/fg tints so they read correctly on either a
        // light or dark theme background.
        backend.set_selection(gui_theme.accent.with_alpha(0.30f));
        backend.set_hover(gui_theme.fg.with_alpha(0.08f));
    };
    apply_theme_colors();

    // Settings-panel inputs: tab width, font choices, theme list + current name.
    backend.set_tab_width(static_cast<int>(settings.tab_width));
    backend.set_theme_name(ss(settings.theme));
    {
        auto fonts = std::make_shared<slint::VectorModel<slint::SharedString>>();
        for (const auto& f : mono_font_choices()) {
            fonts->push_back(ss(f));
        }
        backend.set_font_choices(fonts);
        auto themes = std::make_shared<slint::VectorModel<slint::SharedString>>();
        for (const auto& name : list_theme_names()) {
            themes->push_back(ss(name));
        }
        backend.set_theme_names(themes);
    }
    options.set_side_by_side(settings.view_mode() == ViewMode::SideBySide);
    options.set_show_line_numbers(settings.show_line_numbers);
    options.set_word_wrap(settings.word_wrap);
    options.set_token_granularity(settings.token_granularity);
    options.set_syntax_highlight(settings.syntax_highlight);
    options.set_ignore_whitespace(settings.ignore_whitespace);
    options.set_context_lines(static_cast<int>(settings.context_lines));
    options.set_algorithm(static_cast<int>(settings.algorithm));

    // Restore persisted layout (sidebar width + commits-pane height + window size).
    backend.set_sidebar_width(static_cast<float>(settings.sidebar_width));
    backend.set_commits_panel_height(static_cast<float>(settings.commits_panel_height));
    ui->window().set_size(slint::LogicalSize({static_cast<float>(settings.window_width),
                                              static_cast<float>(settings.window_height)}));

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
        auto model = diffy::gui::build_row_model(vm, gui_theme,
                                                 static_cast<int>(state.settings.tab_width));
        backend.set_max_cols(model.max_cols);
        backend.set_rows(model.rows);

        // Record hunk-header row positions for n/p jump-to-hunk navigation.
        state.hunk_rows.clear();
        state.row_texts.clear();
        state.row_texts.reserve(vm.rows.size());
        for (size_t i = 0; i < vm.rows.size(); ++i) {
            const auto& r = vm.rows[i];
            if (r.kind == diffy::RowKind::HunkHeader) {
                state.hunk_rows.push_back(static_cast<int>(i));
            }
            std::string t = r.header_text;
            for (const auto& sp : r.left.spans) {
                t += sp.text;
            }
            for (const auto& sp : r.right.spans) {
                t += sp.text;
            }
            std::transform(t.begin(), t.end(), t.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            state.row_texts.push_back(std::move(t));
        }
        state.cur_hunk = -1;
        state.find_rows.clear();
        state.find_idx = -1;
        backend.set_diff_scroll_row(-1);  // reset so the first n/p always re-fires
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
        state.shown_files.clear();
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
            state.shown_files.push_back(f.path);
        }
        backend.set_files(files);
        // Keep the keyboard selection pointed at the open file (or clear it).
        int idx = -1;
        for (size_t i = 0; i < state.shown_files.size(); ++i) {
            if (state.shown_files[i] == state.current_file) {
                idx = static_cast<int>(i);
                break;
            }
        }
        backend.set_file_sel_index(idx);
    };

    auto set_files = [&](const std::vector<diffy::gui::FileChange>& changes) {
        state.all_files = changes;
        render_files();
    };

    auto commit_entry = [&](const diffy::gui::CommitInfo& c) {
        CommitEntry ce;
        ce.oid = ss(c.oid);
        ce.short_oid = ss(c.short_oid);
        ce.summary = ss(c.summary);
        return ce;
    };

    // (Re)seed the commit list with the first page. `more` reflects whether the
    // history likely has commits beyond this page (i.e. a full page came back).
    auto set_commits = [&](const std::vector<diffy::gui::CommitInfo>& commits, bool more) {
        auto model = std::make_shared<slint::VectorModel<CommitEntry>>();
        state.shown_commits.clear();
        for (auto& c : commits) {
            model->push_back(commit_entry(c));
            state.shown_commits.push_back(c.oid);
        }
        state.commits_model = model;
        state.commits_shown = static_cast<int>(commits.size());
        state.commits_more = more;
        backend.set_commits(model);
        backend.set_more_commits(more);
        backend.set_loading_commits(false);
        backend.set_commit_sel_index(-1);
    };

    // Append a fetched page to the existing model (runs on the UI thread).
    auto append_commits =
        [&](std::shared_ptr<std::vector<diffy::gui::CommitInfo>> batch, int requested, uint64_t gen) {
            state.loading_commits = false;
            backend.set_loading_commits(false);
            if (gen != state.load_generation || !state.commits_model) {
                return;  // repo switched while loading; drop
            }
            for (const auto& c : *batch) {
                state.commits_model->push_back(commit_entry(c));
                state.shown_commits.push_back(c.oid);
            }
            state.commits_shown += static_cast<int>(batch->size());
            // A bounded page that came back short means we reached the root.
            state.commits_more = requested > 0 && static_cast<int>(batch->size()) == requested;
            backend.set_more_commits(state.commits_more);
        };

    // Walk the next page (or all remaining when count <= 0) off the UI thread.
    auto fetch_more_commits = [&](int count) {
        if (state.loading_commits || !state.commits_more || !state.commits_model ||
            state.repo_path.empty()) {
            return;
        }
        state.loading_commits = true;
        backend.set_loading_commits(true);
        const uint64_t gen = state.load_generation;
        const int skip = state.commits_shown;
        const std::string path = state.repo_path;
        load_threads.emplace_back([path, skip, count, gen, &append_commits]() {
            auto r = Repo::open(path);
            auto batch = std::make_shared<std::vector<diffy::gui::CommitInfo>>();
            if (r) {
                *batch = r->commits(skip, count);
            }
            slint::invoke_from_event_loop(
                [batch, count, gen, &append_commits]() { append_commits(batch, count, gen); });
        });
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
        } else {
            state.pair = state.repo->diff_commit_file(state.current_commit, path);
        }
        backend.set_current_file(ss(path));
        const std::string ctx = state.current_commit.empty()
                                    ? "Working tree: "
                                    : ("Commit " + state.current_commit.substr(0, 8) + ": ");
        // Binary / oversized files aren't diffed; show the reason instead.
        if (!state.pair.note.empty()) {
            state.pair.ok = false;  // nothing to render; option toggles skip it
            backend.set_diff_note(ss(state.pair.note));
            backend.set_rows(std::make_shared<slint::VectorModel<DiffRowData>>());
            backend.set_status_text(ss(ctx + path + " — " + state.pair.note));
            return;
        }
        backend.set_diff_note(ss(""));
        backend.set_status_text(ss(ctx + path));
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
        state.repo_path = m->workdir;  // for background commit paging
        backend.set_branch(ss(m->branch));
        set_commits(m->commits, static_cast<int>(m->commits.size()) >= kCommitPage);
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
        // Staged count (enables Commit) + local branches (for the checkout combo).
        int staged = 0;
        for (const auto& fc : f->files) {
            if (fc.staged) {
                ++staged;
            }
        }
        backend.set_staged_count(staged);
        {
            auto names = std::make_shared<slint::VectorModel<slint::SharedString>>();
            for (const auto& b : state.repo->branches()) {
                if (!b.remote) {
                    names->push_back(ss(b.name));
                }
            }
            backend.set_branch_names(names);
        }
        // Re-open the file the user was viewing: a pending refresh selection
        // wins; otherwise fall back to this repo's remembered last file (which
        // repos_add carried to the front entry).
        std::string want = std::move(state.refresh_select);
        state.refresh_select.clear();
        if (want.empty() && !state.repos.empty()) {
            want = state.repos.front().last_file;
        }
        set_files(f->files);
        backend.set_status_text(
            ss(std::to_string(f->files.size()) + " changed file(s)"));
        bool reopened = false;
        if (!want.empty()) {
            for (const auto& fc : f->files) {
                if (fc.path == want) {
                    open_file(want);
                    reopened = true;
                    break;
                }
            }
        }
        if (reopened) {
            // nothing else to do
        } else if (!f->first_file.empty()) {
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
        set_commits({}, false);
        backend.set_rows(std::make_shared<slint::VectorModel<DiffRowData>>());
        backend.set_branch(ss(""));
        backend.set_current_file(ss(""));
        backend.set_diff_note(ss(""));

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
            meta->commits = r->recent_commits(kCommitPage);
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

    // Browse a commit: list its files and diff the first. Shared by the click
    // handler and keyboard activation. Leaves the keyboard focus on the file
    // list so the arrows then browse the commit's files.
    auto select_commit = [&](const std::string& oid) {
        if (!state.repo) {
            return;
        }
        state.current_commit = oid;
        int idx = -1;
        for (size_t i = 0; i < state.shown_commits.size(); ++i) {
            if (state.shown_commits[i] == oid) {
                idx = static_cast<int>(i);
                break;
            }
        }
        backend.set_commit_sel_index(idx);
        auto changes = state.repo->commit_files(oid);
        set_files(changes);
        backend.set_status_text(ss("Commit " + oid.substr(0, 8) + " — " +
                                   std::to_string(changes.size()) + " file(s) — pick the repo to return"));
        if (!changes.empty()) {
            open_file(changes.front().path);
        } else {
            backend.set_rows(std::make_shared<slint::VectorModel<DiffRowData>>());
            backend.set_current_file(ss(""));
        }
        state.nav_pane = 0;
    };

    // Keyboard navigation across the two list panes. Files preview on move
    // (cheap); commits only highlight on move and load on Enter (a commit diff
    // is heavier). Tab switches panes.
    auto navigate = [&](const std::string& dir) {
        auto step = [](int idx, int n, const std::string& d) {
            if (d == "down") {
                return idx < 0 ? 0 : (idx + 1) % n;
            }
            return idx <= 0 ? n - 1 : idx - 1;  // "up", with wrap
        };
        if (dir == "tab") {
            state.nav_pane = state.nav_pane == 0 ? 1 : 0;
            return;
        }
        // Jump-to-hunk (n/p): scroll the diff so the next/previous hunk header
        // is at the top. Row height isn't perfectly uniform under word-wrap, so
        // this lands at the hunk rather than pixel-exact.
        if (dir == "next-hunk" || dir == "prev-hunk") {
            if (state.hunk_rows.empty()) {
                return;
            }
            const int n = static_cast<int>(state.hunk_rows.size());
            state.cur_hunk = dir == "next-hunk"
                                 ? (state.cur_hunk < 0 ? 0 : (state.cur_hunk + 1) % n)
                                 : (state.cur_hunk <= 0 ? n - 1 : state.cur_hunk - 1);
            backend.set_diff_scroll_row(state.hunk_rows[state.cur_hunk]);
            backend.set_status_text(ss("Hunk " + std::to_string(state.cur_hunk + 1) + "/" +
                                       std::to_string(n) + " — " + state.current_file));
            return;
        }
        if (state.nav_pane == 0) {
            const auto& list = state.shown_files;
            if (list.empty()) {
                return;
            }
            int idx = backend.get_file_sel_index();
            if (dir == "activate") {
                if (idx >= 0 && idx < static_cast<int>(list.size())) {
                    open_file(list[idx]);
                }
                return;
            }
            idx = step(idx, static_cast<int>(list.size()), dir);
            backend.set_file_sel_index(idx);
            open_file(list[idx]);  // selection opens the diff (mirrors a click)
        } else {
            const auto& list = state.shown_commits;
            if (list.empty()) {
                return;
            }
            int idx = backend.get_commit_sel_index();
            if (dir == "activate") {
                if (idx >= 0 && idx < static_cast<int>(list.size())) {
                    select_commit(list[idx]);
                }
                return;
            }
            idx = step(idx, static_cast<int>(list.size()), dir);
            backend.set_commit_sel_index(idx);  // highlight only; Enter loads it
        }
    };

    // Re-scan the working tree without reopening the repo, keeping the open file.
    auto refresh = [&]() {
        if (state.repo_path.empty()) {
            return;
        }
        state.refresh_select = state.current_file;
        load_repo(state.repo_path);
    };

    // Find-in-diff: scan the (lowercased) per-row text for the query, collect
    // matching model rows, and scroll/highlight the current one.
    auto find_show_match = [&]() {
        if (state.find_idx < 0 || state.find_rows.empty()) {
            backend.set_find_current(0);
            backend.set_find_current_row(-1);
            return;
        }
        const int row = state.find_rows[state.find_idx];
        backend.set_find_current(state.find_idx + 1);
        backend.set_find_current_row(row);
        backend.set_diff_scroll_row(row);
    };
    auto run_find = [&](const std::string& query) {
        std::string q = query;
        std::transform(q.begin(), q.end(), q.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        state.find_rows.clear();
        if (!q.empty()) {
            for (size_t i = 0; i < state.row_texts.size(); ++i) {
                if (state.row_texts[i].find(q) != std::string::npos) {
                    state.find_rows.push_back(static_cast<int>(i));
                }
            }
        }
        state.find_idx = state.find_rows.empty() ? -1 : 0;
        backend.set_find_count(static_cast<int>(state.find_rows.size()));
        find_show_match();
    };
    auto find_step = [&](int dir) {
        if (state.find_rows.empty()) {
            return;
        }
        const int n = static_cast<int>(state.find_rows.size());
        state.find_idx = (state.find_idx + dir + n) % n;
        find_show_match();
    };
    auto find_close = [&]() {
        state.find_rows.clear();
        state.find_idx = -1;
        backend.set_find_count(0);
        backend.set_find_current(0);
        backend.set_find_current_row(-1);
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
    backend.on_select_commit([&](slint::SharedString oid) { select_commit(str(oid)); });
    backend.on_navigate([&](slint::SharedString d) { navigate(str(d)); });
    backend.on_refresh([&]() { refresh(); });
    backend.on_find([&](slint::SharedString q) { run_find(str(q)); });
    backend.on_find_next([&]() { find_step(1); });
    backend.on_find_prev([&]() { find_step(-1); });
    backend.on_find_close([&]() { find_close(); });
    // Write actions: delegate to the (UI-agnostic) repo_model, then re-scan.
    backend.on_stage_file([&](slint::SharedString p) {
        if (state.repo) {
            state.repo->stage_file(str(p));
            refresh();
        }
    });
    backend.on_unstage_file([&](slint::SharedString p) {
        if (state.repo) {
            state.repo->unstage_file(str(p));
            refresh();
        }
    });
    backend.on_discard_file([&](slint::SharedString p) {
        if (state.repo) {
            state.repo->discard_changes(str(p));
            refresh();
        }
    });
    backend.on_commit([&](slint::SharedString msg, bool amend) {
        if (!state.repo) {
            return;
        }
        const bool ok = state.repo->commit(str(msg), amend);
        backend.set_status_text(
            ss(ok ? "Committed." : "Commit failed (is user.name / user.email set?)"));
        refresh();
    });
    backend.on_checkout_branch([&](slint::SharedString name) {
        if (!state.repo) {
            return;
        }
        if (!state.repo->checkout_branch(str(name))) {
            backend.set_status_text(ss("Checkout failed — commit or stash changes first."));
        }
        refresh();
    });
    backend.on_set_tab_width([&](int w) {
        state.settings.tab_width = w;
        if (state.pair.ok) {
            relayout();  // tab expansion happens during render; no re-diff needed
        }
    });
    backend.on_set_theme([&](slint::SharedString name) {
        state.settings.theme = str(name);
        gui_theme = diffy::gui::load_gui_theme(state.settings.theme);
        apply_theme_colors();
        if (state.pair.ok) {
            relayout();  // re-render so span colours pick up the new theme
        }
    });
    backend.on_load_more_commits([&]() { fetch_more_commits(kCommitPage); });
    backend.on_load_all_commits([&]() { fetch_more_commits(0); });
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
    // Capture the live option-bar state and window layout so they survive restart.
    state.settings.default_view = options.get_side_by_side() ? "side-by-side" : "unified";
    state.settings.word_wrap = options.get_word_wrap();
    state.settings.show_line_numbers = options.get_show_line_numbers();
    state.settings.syntax_highlight = options.get_syntax_highlight();
    state.settings.ignore_whitespace = options.get_ignore_whitespace();
    state.settings.token_granularity = options.get_token_granularity();
    state.settings.context_lines = options.get_context_lines();
    state.settings.algorithm = options.get_algorithm();
    state.settings.font_family = str(backend.get_mono_font());
    state.settings.font_size = backend.get_font_size();
    state.settings.tab_width = backend.get_tab_width();
    state.settings.theme = str(backend.get_theme_name());
    state.settings.sidebar_width = static_cast<int64_t>(backend.get_sidebar_width());
    state.settings.commits_panel_height =
        static_cast<int64_t>(backend.get_commits_panel_height());
    const auto win_size = ui->window().size();
    const float scale = ui->window().scale_factor();
    if (scale > 0.0f) {
        state.settings.window_width = static_cast<int64_t>(win_size.width / scale);
        state.settings.window_height = static_cast<int64_t>(win_size.height / scale);
    }
    gui_settings_save(state.settings);
    // Remember the open working-tree file for the current repo (front entry).
    if (state.current_commit.empty() && !state.current_file.empty() && !state.repos.empty()) {
        state.repos.front().last_file = state.current_file;
    }
    repos_save(state.repos);
    diffy::gui::git_runtime_shutdown();
    return 0;
}
