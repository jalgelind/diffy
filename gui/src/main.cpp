#include "app-window.h"

#include "config/config.hpp"  // config_get_directory
#include "config/gui_config.hpp"
#include "config/repos.hpp"
#include "diff_bridge.hpp"
#include "highlight/highlight_group.hpp"
#include "highlight/highlight_palette.hpp"
#include "render/diff_pipeline.hpp"
#include "repo_model.hpp"
#include "review/log.hpp"
#include "review/providers/bitbucket_cloud.hpp"
#include "review/secret_store.hpp"

#include <slint.h>

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
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
#include <shellapi.h>
#include <dwmapi.h>
#endif

#include <chrono>

using namespace diffy;
using diffy::gui::Repo;

namespace {

#ifdef _WIN32
std::wstring
to_wide(const std::string& s) {
    if (s.empty()) {
        return std::wstring();
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) {
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    }
    return w;
}

void
copy_to_clipboard(const std::string& utf8) {
    if (!OpenClipboard(nullptr)) {
        return;
    }
    EmptyClipboard();
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (n > 0) {
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(n) * sizeof(wchar_t));
        if (h) {
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, static_cast<wchar_t*>(GlobalLock(h)), n);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);  // ownership passes to the clipboard
        }
    }
    CloseClipboard();
}

// libgit2 workdir paths (and our repo-relative paths) use forward slashes;
// Explorer's shell — especially the `/select,` switch — only parses backslashes,
// so an un-normalized path makes it give up and open the default folder.
std::wstring
to_windows_path(const std::string& s) {
    std::wstring w = to_wide(s);
    std::replace(w.begin(), w.end(), L'/', L'\\');
    return w;
}

void
shell_open(const std::string& abs_path) {
    ShellExecuteW(nullptr, L"open", to_windows_path(abs_path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void
shell_reveal(const std::string& abs_path) {
    const std::wstring arg = L"/select,\"" + to_windows_path(abs_path) + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
}
#else
// TODO: xdg-open / pbcopy equivalents for Linux/macOS.
void copy_to_clipboard(const std::string&) {}
void shell_open(const std::string&) {}
void shell_reveal(const std::string&) {}
#endif

#ifdef _WIN32
// The main Slint window's HWND: our process's visible, unowned, sizable top-level
// window (excludes the owned/small popup windows). Title-independent.
HWND
find_main_hwnd() {
    struct Ctx { DWORD pid; HWND hwnd; };
    Ctx ctx{GetCurrentProcessId(), nullptr};
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid == c->pid && IsWindowVisible(h) && GetWindow(h, GW_OWNER) == nullptr) {
                RECT r{};
                GetWindowRect(h, &r);
                if ((r.right - r.left) > 200 && (r.bottom - r.top) > 200) {
                    c->hwnd = h;
                    return FALSE;  // found the main window; stop
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.hwnd;
}

// Tint the OS-drawn title bar to match the theme (Windows 11): immersive dark
// mode + caption/text/border colours. No-op on older Windows for the colours.
void
apply_window_chrome(const diffy::gui::GuiTheme& t) {
    HWND h = find_main_hwnd();
    if (!h) {
        return;
    }
    BOOL dark = t.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(h, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    COLORREF caption = RGB(t.bg.red(), t.bg.green(), t.bg.blue());
    COLORREF text = RGB(t.fg.red(), t.fg.green(), t.fg.blue());
    COLORREF border = RGB(t.divider.red(), t.divider.green(), t.divider.blue());
    DwmSetWindowAttribute(h, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
    DwmSetWindowAttribute(h, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
    DwmSetWindowAttribute(h, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
}
#else
void
apply_window_chrome(const diffy::gui::GuiTheme&) {}
#endif

// Join a repo workdir with a repo-relative path.
std::string
join_path(const std::string& base, const std::string& rel) {
    if (base.empty()) {
        return rel;
    }
    std::string out = base;
    if (out.back() != '/' && out.back() != '\\') {
        out += '/';
    }
    out += rel;
    return out;
}

// Short, user-facing label for a PR's review state (shown in the sidebar).
const char*
approval_str(diffy::review::ApprovalState s) {
    using A = diffy::review::ApprovalState;
    switch (s) {
        case A::Approved: return "approved";
        case A::ChangesRequested: return "changes requested";
        case A::Draft: return "draft";
        case A::Merged: return "merged";
        case A::Declined: return "declined";
        default: return "open";
    }
}

// Greedy word-wrap of prose into lines of at most `cols` columns, respecting
// existing newlines. Used to render review-comment bodies as uniform-height rows.
std::vector<std::string>
wrap_text(const std::string& text, int cols) {
    if (cols < 8) {
        cols = 8;
    }
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t nl = text.find('\n', start);
        std::string para = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        // Greedy-fill words into lines.
        std::string line;
        std::size_t i = 0;
        while (i < para.size()) {
            while (i < para.size() && para[i] == ' ') {
                ++i;
            }
            std::size_t j = i;
            while (j < para.size() && para[j] != ' ') {
                ++j;
            }
            const std::string word = para.substr(i, j - i);
            i = j;
            if (word.empty()) {
                continue;
            }
            if (line.empty()) {
                line = word;
            } else if (static_cast<int>(line.size() + 1 + word.size()) <= cols) {
                line += ' ';
                line += word;
            } else {
                out.push_back(line);
                line = word;
            }
        }
        out.push_back(line);  // may be empty (blank line)
        if (nl == std::string::npos) {
            break;
        }
        start = nl + 1;
    }
    return out;
}

// A small fixed-size pool of worker threads for review (network) requests, so we
// can prefetch aggressively without spawning an unbounded number of threads.
// Each worker owns its own HttpClient (backend-safe). Two priorities: Interactive
// (a user just clicked) always drains before Prefetch (speculative), so a click
// never waits behind background work. Tasks do the network call with the worker's
// client, then post results to the UI thread via slint::invoke_from_event_loop.
class ReviewPool {
  public:
    enum class Prio { Interactive, Prefetch };
    using Task = std::function<void(diffy::review::HttpClient&)>;

    explicit ReviewPool(int workers) {
        for (int i = 0; i < workers; ++i) {
            threads_.emplace_back([this] { run(); });
        }
    }
    ~ReviewPool() { stop(); }

    void
    submit(Prio p, Task fn) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (stop_) {
                return;
            }
            (p == Prio::Interactive ? hi_ : lo_).push_back(std::move(fn));
        }
        cv_.notify_one();
    }

    // Drop pending speculative work (e.g. on a repo/PR switch).
    void
    clear_prefetch() {
        std::lock_guard<std::mutex> lk(mu_);
        lo_.clear();
    }

    void
    stop() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (stop_) {
                return;
            }
            stop_ = true;
            hi_.clear();
            lo_.clear();
        }
        cv_.notify_all();
        for (auto& t : threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        threads_.clear();
    }

  private:
    void
    run() {
        auto client = diffy::review::make_http_client();
        for (;;) {
            Task task;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [this] { return stop_ || !hi_.empty() || !lo_.empty(); });
                if (stop_) {
                    return;
                }
                if (!hi_.empty()) {
                    task = std::move(hi_.front());
                    hi_.pop_front();
                } else {
                    task = std::move(lo_.front());
                    lo_.pop_front();
                }
            }
            if (task && client) {
                task(*client);
            }
        }
    }

    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Task> hi_;
    std::deque<Task> lo_;
    bool stop_ = false;
    std::vector<std::thread> threads_;
};

}  // namespace

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
                                                     static_cast<int>(settings.tab_width),
                                                     /*wrap=*/false, /*wrap_cols=*/0);
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
#ifdef DIFFY_PLATFORM_WINDOWS
    // Per-monitor DPI awareness: without it Windows renders the UI at 96 DPI and
    // bitmap-scales it up on HiDPI displays, which looks blurry. Must run before
    // the Slint window is created. Slint already honours window().scale_factor().
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
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
        std::string base_ref;        // non-empty => diff working tree vs this ref
        diffy::gui::BlobPair pair;
        DiffComputation computation;
        GuiSettings settings;
        uint64_t load_generation = 0;  // bumped per open; old async loads are dropped
        uint64_t diff_generation = 0;  // bumped per diff; stale background diffs dropped
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
        std::vector<int> file_model_index;  // model-row of each shown file (rows incl. headers)
        std::vector<std::string> shown_commits;
        int nav_pane = 0;
        std::string refresh_select;  // file to re-open after a refresh, if present
        bool soft_refreshing = false;  // a periodic background status re-scan is in flight
        std::vector<int> first_visual;  // logical row -> first visual (rendered) row
        std::vector<int> hunk_rows;  // visual-row index of each hunk header
        int cur_hunk = -1;           // index into hunk_rows for n/p navigation
        std::vector<std::string> row_texts;  // lowercased searchable text per logical row
        std::vector<int> find_rows;          // visual rows matching the find query
        int find_idx = -1;                   // index into find_rows
        // Active Bitbucket credential (from a live connect or restored from the
        // vault at startup); drives detection/PR listing for bitbucket.org repos.
        std::optional<diffy::review::Credential> bitbucket_cred;
        // PR detail mode: the open PR's id ("" = not in a PR), its ref SHAs (for
        // sourcing file diffs), and the workspace/repo parsed from origin.
        std::string current_pr;
        diffy::review::PrRefs pr_refs;
        std::string pr_ws;
        std::string pr_repo;
        // Caches so lists show instantly from the last result and refresh in the
        // background (swap-in rather than clear+fetch+show). Keys: PR list by
        // "ws/repo"; PR detail + file blobs by "ws/repo#id" (+ ":path").
        std::unordered_map<std::string, std::vector<diffy::review::PullRequest>> pr_list_cache;
        struct PrDetail {
            diffy::review::PrRefs refs;
            std::vector<diffy::gui::FileChange> files;
            std::vector<diffy::gui::CommitInfo> commits;
            std::vector<diffy::review::ReviewThread> threads;
            std::string title;
            std::string subtitle;
        };
        std::unordered_map<std::string, PrDetail> pr_detail_cache;
        std::unordered_map<std::string, std::pair<std::string, std::string>> pr_file_cache;
        // Threads for the open PR (all files); relayout interleaves those matching
        // the current file into the diff.
        std::vector<diffy::review::ReviewThread> pr_threads;
    } state;
    constexpr int kCommitPage = 50;  // commits per lazy page
    state.settings = settings;
    state.repos = repos_load();

    // Restore a persisted Bitbucket connection: the account email is stored in
    // config, its token in the OS credential vault. (Bearer/access-token connects
    // are session-only — not persisted.)
    if (!state.settings.bitbucket_account.empty()) {
        const std::string base = "https://api.bitbucket.org/2.0";
        if (auto tok = diffy::review::SecretStore::get(diffy::review::build_key(
                "bitbucket-cloud", base, state.settings.bitbucket_account))) {
            diffy::review::Credential c;
            c.method = diffy::review::AuthMethod::BasicToken;
            c.principal = state.settings.bitbucket_account;
            c.secret = *tok;
            state.bitbucket_cred = c;
            backend.set_bitbucket_connected(true);
            backend.set_bitbucket_status(ss("Connected as " + state.settings.bitbucket_account));
        }
    }

    // Outstanding background repo-load threads, joined before libgit2 shuts down.
    std::vector<std::thread> load_threads;

    // Bounded worker pool for review (network) requests + prefetching. Each worker
    // owns its own HttpClient; tasks post results to the UI thread. Stopped (and
    // joined) at shutdown before state is torn down. See REVIEW-ROADMAP.md.
    ReviewPool review_pool(4);

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
        backend.set_dark(gui_theme.dark);  // flips the std-widgets (scrollbar) palette
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

    // Wrap column count, derived by the UI from the diff cell width + the measured
    // monospace advance and reported via Backend.set-wrap-metrics. 0 disables
    // wrapping (the state before the first report).
    int wrap_cols = 0;
    slint::Timer wrap_debounce;  // coalesces re-wraps during a live resize

    // layout-only refresh: reuse the cached annotated hunks
    auto relayout = [&]() {
        auto input = state.computation.input();
        auto vm = build_diff_view(input, state.computation.hunks, layout_opts(),
                                  &state.computation.a_highlights,
                                  &state.computation.b_highlights);

        // build_row_model expands tabs and reports the widest line (in display
        // columns), which drives the horizontal scroll extent.
        auto model = diffy::gui::build_row_model(vm, gui_theme,
                                                 static_cast<int>(state.settings.tab_width),
                                                 options.get_word_wrap(), wrap_cols);
        backend.set_max_cols(model.max_cols);

        // In PR mode, interleave review-comment rows beneath their anchor lines for
        // the current file (comments are wrapped into uniform one-line rows in
        // wrap_text). Threads with no usable anchor (outdated/general) are appended.
        if (state.current_pr.empty() || state.pr_threads.empty()) {
            backend.set_rows(model.rows);
        } else {
            using diffy::review::ReviewThread;
            using diffy::review::Side;
            std::unordered_map<std::string, std::vector<const ReviewThread*>> by_anchor;
            std::vector<const ReviewThread*> outdated;
            for (const auto& th : state.pr_threads) {
                if (th.anchor.new_path != state.current_file) {
                    continue;
                }
                if (th.outdated || th.anchor.start.line < 1) {
                    outdated.push_back(&th);
                    continue;
                }
                by_anchor[std::to_string(th.anchor.start.line) +
                          (th.anchor.side == Side::New ? "|n" : "|o")]
                    .push_back(&th);
            }
            const int wc = wrap_cols > 0 ? wrap_cols : 80;
            auto out = std::make_shared<slint::VectorModel<DiffRowData>>();
            auto push_thread = [&](const ReviewThread* th) {
                for (std::size_t ci = 0; ci < th->comments.size(); ++ci) {
                    const auto& cm = th->comments[ci];
                    const std::string who = ci == 0 ? cm.author : ("\xE2\x86\xB3 " + cm.author);
                    auto lines = wrap_text(cm.body_md, wc);
                    if (lines.empty()) {
                        lines.emplace_back();
                    }
                    for (std::size_t li = 0; li < lines.size(); ++li) {
                        DiffRowData d{};
                        d.is_comment = true;
                        d.comment_author = ss(li == 0 ? who : std::string());
                        d.comment_body = ss(lines[li]);
                        d.comment_outdated = th->outdated;
                        out->push_back(d);
                    }
                }
            };
            const std::size_t n = model.rows->row_count();
            for (std::size_t i = 0; i < n; ++i) {
                DiffRowData row = *model.rows->row_data(i);
                out->push_back(row);
                if (row.is_header || row.is_comment) {
                    continue;
                }
                const std::string nn(std::string_view(row.new_no));
                const std::string on(std::string_view(row.old_no));
                if (!nn.empty()) {
                    if (auto it = by_anchor.find(nn + "|n"); it != by_anchor.end()) {
                        for (auto* th : it->second) push_thread(th);
                    }
                }
                if (!on.empty()) {
                    if (auto it = by_anchor.find(on + "|o"); it != by_anchor.end()) {
                        for (auto* th : it->second) push_thread(th);
                    }
                }
            }
            for (auto* th : outdated) {
                DiffRowData hdr{};
                hdr.is_comment = true;
                hdr.comment_outdated = true;
                hdr.comment_author = ss(std::string("outdated"));
                hdr.comment_body =
                    ss(th->anchor.new_path.empty() ? std::string("(general comment)")
                                                   : th->anchor.new_path);
                out->push_back(hdr);
                push_thread(th);
            }
            backend.set_rows(out);
        }

        // A logical line may render as several wrapped rows; navigation targets the
        // first visual row of the matching logical line.
        state.first_visual = model.first_visual;

        // Record hunk-header row positions for n/p jump-to-hunk navigation.
        state.hunk_rows.clear();
        state.row_texts.clear();
        state.row_texts.reserve(vm.rows.size());
        for (size_t i = 0; i < vm.rows.size(); ++i) {
            const auto& r = vm.rows[i];
            if (r.kind == diffy::RowKind::HunkHeader) {
                state.hunk_rows.push_back(state.first_visual[i]);
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

    // full refresh: re-run the diff for the current blob pair, then lay out. Small
    // pairs diff synchronously (instant preview); large ones run on a background
    // thread so selecting a big file never blocks the event loop (5b).
    constexpr size_t kBackgroundDiffBytes = 256 * 1024;
    auto recompute = [&]() {
        if (!state.pair.ok) {
            return;
        }
        const auto opts = pipeline_opts();
        const size_t bytes = state.pair.old_text.size() + state.pair.new_text.size();
        const uint64_t gen = ++state.diff_generation;
        if (bytes < kBackgroundDiffBytes) {
            state.computation = compute_annotated_diff(state.pair.old_text, state.pair.new_text,
                                                       state.pair.old_name, state.pair.new_name, opts);
            relayout();
            return;
        }
        // Background: copy the inputs, compute off-thread, apply on the UI thread
        // if no newer diff superseded it.
        struct DiffInputs {
            std::string a, b, na, nb;
            DiffPipelineOptions opts;
        };
        auto in = std::make_shared<DiffInputs>(
            DiffInputs{state.pair.old_text, state.pair.new_text, state.pair.old_name,
                       state.pair.new_name, opts});
        backend.set_status_text(ss("Diffing " + state.current_file + " …"));
        load_threads.emplace_back([&, in, gen]() {
            auto comp = std::make_shared<DiffComputation>(
                compute_annotated_diff(in->a, in->b, in->na, in->nb, in->opts));
            slint::invoke_from_event_loop([&, comp, gen]() {
                if (gen != state.diff_generation) {
                    return;  // a newer file/diff won
                }
                state.computation = std::move(*comp);
                relayout();
            });
        });
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
        state.file_model_index.clear();

        auto make_entry = [&](const diffy::gui::FileChange& f, int depth) {
            FileEntry fe;
            fe.path = ss(f.path);
            fe.status = ss(f.status);
            fe.staged = f.staged;
            fe.is_section = false;
            fe.depth = depth;
            const auto slash = f.path.find_last_of('/');
            if (slash == std::string::npos) {
                fe.name = ss(f.path);
                fe.dir = ss("");
            } else {
                fe.name = ss(f.path.substr(slash + 1));
                fe.dir = ss(f.path.substr(0, slash + 1));
            }
            return fe;
        };
        auto push_section = [&](const std::string& label) {
            FileEntry fe;
            fe.is_section = true;
            fe.name = ss(label);
            files->push_back(fe);
        };
        auto push_file = [&](const diffy::gui::FileChange& f, int depth) {
            files->push_back(make_entry(f, depth));
            state.shown_files.push_back(f.path);
            state.file_model_index.push_back(static_cast<int>(files->row_count()) - 1);
        };

        std::vector<const diffy::gui::FileChange*> filtered;
        for (const auto& f : state.all_files) {
            if (needle.empty() || lower(f.path).find(needle) != std::string::npos) {
                filtered.push_back(&f);
            }
        }

        if (backend.get_group_by_folder()) {
            // Group by directory, with a header per folder (2d).
            std::sort(filtered.begin(), filtered.end(),
                      [](const auto* a, const auto* b) { return a->path < b->path; });
            std::string cur_dir = "\x01";  // sentinel that no real path equals
            for (const auto* f : filtered) {
                const auto slash = f->path.find_last_of('/');
                const std::string dir = slash == std::string::npos ? "" : f->path.substr(0, slash);
                if (dir != cur_dir) {
                    push_section(dir.empty() ? "(root)" : dir);
                    cur_dir = dir;
                }
                push_file(*f, 1);
            }
        } else {
            // Staged / unstaged sections (4b). Staged first, header only when both
            // groups are present.
            std::vector<const diffy::gui::FileChange*> staged, unstaged;
            for (const auto* f : filtered) {
                (f->staged ? staged : unstaged).push_back(f);
            }
            if (!staged.empty()) {
                push_section("STAGED (" + std::to_string(staged.size()) + ")");
                for (const auto* f : staged) {
                    push_file(*f, 0);
                }
                if (!unstaged.empty()) {
                    push_section("CHANGES (" + std::to_string(unstaged.size()) + ")");
                }
            }
            for (const auto* f : unstaged) {
                push_file(*f, 0);
            }
        }

        backend.set_files(files);
        // Point the keyboard selection at the open file's model row (or clear it).
        int idx = -1;
        for (size_t i = 0; i < state.shown_files.size(); ++i) {
            if (state.shown_files[i] == state.current_file) {
                idx = state.file_model_index[i];
                break;
            }
        }
        backend.set_file_sel_index(idx);
        backend.set_changes_count(static_cast<int>(state.shown_files.size()));
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

    // Source a PR file's diff from the provider: base_sha content vs head_sha
    // content, rendered by the normal pipeline. Async — the file_at calls hit the
    // network. A missing side (404) => empty, which correctly renders adds/deletes.
    // Render a PR file's diff from the two blob texts (cache or fetched).
    auto render_pr_blobs = [&](const std::string& pr, const std::string& path,
                               const std::string& oldt, const std::string& newt) {
        diffy::gui::BlobPair bp;
        bp.ok = true;
        bp.old_text = oldt;
        bp.new_text = newt;
        bp.old_name = path;
        bp.new_name = path;
        state.pair = bp;
        backend.set_status_text(ss("PR #" + pr + ": " + path));
        recompute();
    };

    auto open_pr_file = [&](const std::string& path) {
        if (state.current_pr.empty() || !state.bitbucket_cred) {
            return;
        }
        state.current_file = path;
        backend.set_current_file(ss(path));
        backend.set_diff_note(ss(""));
        const std::string pr = state.current_pr;
        // Local-first: when both PR commits are present locally (the head ref was
        // fetched), render the exact PR diff offline — merge-base(base,head)..head
        // (three-dot). This is more accurate than the API's two-dot base tip.
        {
            const std::string base = state.pr_refs.base_sha, head = state.pr_refs.head_sha;
            if (state.repo && !base.empty() && !head.empty() && state.repo->has_commit(base) &&
                state.repo->has_commit(head)) {
                const std::string mb = state.repo->merge_base(base, head);
                state.pair = state.repo->diff_oids(mb.empty() ? base : mb, head, path);
                backend.set_status_text(ss("PR #" + pr + ": " + path));
                recompute();
                return;
            }
        }
        const std::string ck = state.pr_ws + "/" + state.pr_repo + "#" + pr + ":" + path;
        // Cached blobs render instantly (content doesn't change within a session).
        auto cit = state.pr_file_cache.find(ck);
        if (cit != state.pr_file_cache.end()) {
            render_pr_blobs(pr, path, cit->second.first, cit->second.second);
            return;
        }
        backend.set_status_text(ss("PR #" + pr + ": " + path + " …"));
        const diffy::review::Credential cred = *state.bitbucket_cred;
        const std::string ws = state.pr_ws, repo = state.pr_repo;
        const std::string base = state.pr_refs.base_sha, head = state.pr_refs.head_sha;
        const uint64_t gen = state.load_generation;
        review_pool.submit(ReviewPool::Prio::Interactive, [&, path, ws, repo, cred, base, head, pr,
                                                           ck, gen](diffy::review::HttpClient& http) {
            diffy::review::BitbucketCloudClient client(http, cred, ws, repo);
            std::string oldt, newt;
            if (auto o = client.file_at(base, path)) {
                oldt = o.value();
            }
            if (auto n = client.file_at(head, path)) {
                newt = n.value();
            }
            slint::invoke_from_event_loop([&, path, oldt, newt, pr, ck, gen]() {
                if (gen != state.load_generation || state.current_pr != pr) {
                    return;
                }
                state.pr_file_cache[ck] = {oldt, newt};
                render_pr_blobs(pr, path, oldt, newt);
            });
        });
    };

    // Open a file's diff. Honours the current mode: PR (provider-sourced),
    // working-tree (HEAD vs disk), or a selected commit (commit vs its parent).
    auto open_file = [&](const std::string& path) {
        if (!state.repo) {
            return;
        }
        if (!state.current_pr.empty()) {
            open_pr_file(path);
            return;
        }
        state.current_file = path;
        std::string ctx;
        if (!state.current_commit.empty()) {
            state.pair = state.repo->diff_commit_file(state.current_commit, path);
            ctx = "Commit " + state.current_commit.substr(0, 8) + ": ";
        } else if (!state.base_ref.empty()) {
            state.pair = state.repo->diff_ref_file(state.base_ref, path);
            ctx = "vs " + state.base_ref + ": ";
        } else {
            state.pair = state.repo->diff_workdir_file(path);
            ctx = "Working tree: ";
        }
        backend.set_current_file(ss(path));
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

    // Fetch a PR's full detail (header + refs + files + commits + threads) with the
    // worker's HttpClient. Captureless (results via out params) so both the
    // interactive open and the background prefetch reuse it.
    auto fetch_pr_detail = [](diffy::review::HttpClient& http, const diffy::review::Credential& cred,
                              const std::string& ws, const std::string& repo, const std::string& id,
                              State::PrDetail& d, std::string& err) {
        diffy::review::BitbucketCloudClient client(http, cred, ws, repo);
        auto pr = client.get(id);
        auto refs = client.refs(id);
        auto files = client.files(id);
        auto commits = client.commits(id);
        auto threads = client.threads(id);
        d.refs = refs ? refs.value() : diffy::review::PrRefs{};
        if (threads) {
            d.threads = std::move(threads.value());
        }
        if (files) {
            for (const auto& f : files.value()) {
                diffy::gui::FileChange c;
                c.path = f.path;
                c.status = f.status == "added"     ? "A"
                           : f.status == "deleted" ? "D"
                           : f.status == "renamed" ? "R"
                                                   : "M";
                d.files.push_back(std::move(c));
            }
        }
        if (commits) {
            for (const auto& c : commits.value()) {
                diffy::gui::CommitInfo x;
                x.oid = c.sha;
                x.short_oid = c.short_sha;
                x.summary = c.summary;
                x.author = c.author;
                d.commits.push_back(std::move(x));
            }
        }
        d.title = pr ? pr.value().title : ("PR #" + id);
        d.subtitle = pr ? (pr.value().src_branch + " → " + pr.value().dst_branch + "  ·  " +
                           approval_str(pr.value().state))
                        : std::string{};
        err = files ? std::string{} : files.error().message;
    };

    // Prefetch (background) a PR's file blobs into pr_file_cache so opening any file
    // is instant. Runs on the UI thread (reads the cache), submits Prefetch tasks;
    // capped and skips already-cached files.
    auto prefetch_pr_blobs = [&](const std::string& id, const State::PrDetail& d) {
        if (!state.bitbucket_cred) {
            return;
        }
        const diffy::review::Credential cred = *state.bitbucket_cred;
        const std::string ws = state.pr_ws, repo = state.pr_repo;
        const std::string base = d.refs.base_sha, head = d.refs.head_sha;
        const uint64_t gen = state.load_generation;
        int count = 0;
        for (const auto& f : d.files) {
            if (count++ >= 40) {
                break;  // cap: don't speculatively fetch hundreds of blobs
            }
            const std::string path = f.path;
            const std::string ck = ws + "/" + repo + "#" + id + ":" + path;
            if (state.pr_file_cache.count(ck)) {
                continue;
            }
            review_pool.submit(
                ReviewPool::Prio::Prefetch,
                [&, path, ws, repo, cred, base, head, ck, id, gen](diffy::review::HttpClient& http) {
                    diffy::review::BitbucketCloudClient client(http, cred, ws, repo);
                    std::string oldt, newt;
                    if (auto o = client.file_at(base, path)) {
                        oldt = o.value();
                    }
                    if (auto n = client.file_at(head, path)) {
                        newt = n.value();
                    }
                    slint::invoke_from_event_loop([&, ck, oldt, newt, id, gen]() {
                        if (gen != state.load_generation || state.current_pr != id) {
                            return;
                        }
                        state.pr_file_cache[ck] = {oldt, newt};
                    });
                });
        }
    };

    // Apply a fetched-or-cached PR detail to the sidebars. `select_first` opens the
    // first file — only on the initial display, so a background swap doesn't yank
    // the user off the file they're reading.
    auto apply_pr_detail = [&](const std::string& id, const State::PrDetail& d, bool select_first) {
        state.pr_refs = d.refs;
        state.pr_threads = d.threads;
        backend.set_pr_title(ss("#" + id + "  " + d.title));
        backend.set_pr_subtitle(ss(d.subtitle));
        set_files(d.files);
        set_commits(d.commits, false);
        if (select_first) {
            if (!d.files.empty()) {
                open_file(d.files.front().path);
            } else {
                backend.set_rows(std::make_shared<slint::VectorModel<DiffRowData>>());
                backend.set_current_file(ss(""));
            }
        } else if (!state.current_file.empty() && state.pair.ok) {
            relayout();  // background swap: re-render the open file with fresh comments
        }
        prefetch_pr_blobs(id, d);  // warm the file cache so switching files is instant
    };

    // Enter PR detail mode: show the cached detail instantly (if any), then fetch
    // in the background and swap in the fresh result — no clear+fetch+show flicker.
    auto open_pr = [&](const std::string& id) {
        if (!state.repo || !state.bitbucket_cred) {
            return;
        }
        auto parsed = diffy::review::parse_remote_url(state.repo->origin_url());
        if (!parsed) {
            return;
        }
        state.current_pr = id;
        state.pr_ws = parsed->owner;
        state.pr_repo = parsed->repo;
        state.current_commit.clear();
        state.base_ref.clear();
        backend.set_on_working_tree(false);
        backend.set_pr_open(true);
        review_pool.clear_prefetch();  // drop a previous PR's speculative blob fetches

        const std::string key = parsed->owner + "/" + parsed->repo + "#" + id;
        auto cit = state.pr_detail_cache.find(key);
        const bool had_cache = cit != state.pr_detail_cache.end();
        if (had_cache) {
            apply_pr_detail(id, cit->second, /*select_first=*/true);
        } else {
            backend.set_pr_title(ss("#" + id + "  loading…"));
            backend.set_pr_subtitle(ss(""));
            set_files({});  // no cache yet — don't leave the working-tree files showing
            set_commits({}, false);
        }

        const diffy::review::Credential cred = *state.bitbucket_cred;
        const std::string ws = parsed->owner, repo = parsed->repo;
        const bool select_first = !had_cache;
        const uint64_t gen = state.load_generation;
        review_pool.submit(ReviewPool::Prio::Interactive, [&, id, ws, repo, cred, key, select_first,
                                                           gen](diffy::review::HttpClient& http) {
            State::PrDetail d;
            std::string err;
            fetch_pr_detail(http, cred, ws, repo, id, d, err);

            slint::invoke_from_event_loop([&, id, d, key, select_first, err, gen]() {
                if (gen != state.load_generation || state.current_pr != id) {
                    return;
                }
                if (err.empty()) {
                    state.pr_detail_cache[key] = d;  // cache + swap in the fresh result
                    apply_pr_detail(id, d, select_first);
                } else {
                    backend.set_status_text(ss("Couldn't load PR: " + err));
                }
            });
        });

        // Fetch the PR's head ref in the background (libgit2 on a fresh Repo, so the
        // shared handle isn't touched off-thread). On success, re-open the shared
        // repo to pick up the new objects and re-render the current file — which
        // then uses the exact local diff (#29).
        const std::string rp = state.repo_path;
        const std::string furl = "https://bitbucket.org/" + ws + "/" + repo + ".git";
        const std::string refspec = "+refs/pull-requests/" + id + "/from:refs/diffy/pr/" + id;
        const std::string fuser = cred.principal, fpass = cred.secret;
        load_threads.emplace_back([&, rp, furl, refspec, fuser, fpass, id, gen]() {
            auto r = Repo::open(rp);
            std::string ferr;
            if (!r || !r->fetch_refspec(furl, refspec, fuser, fpass, &ferr)) {
                diffy::review::log_line("pr-ref fetch failed for #" + id + " (" + furl +
                                        ") user='" + fuser + "': " + ferr);
                return;
            }
            diffy::review::log_line("pr-ref fetched for #" + id + " (" + furl + ")");
            slint::invoke_from_event_loop([&, rp, id, gen]() {
                if (gen != state.load_generation || state.current_pr != id) {
                    return;
                }
                if (auto nr = Repo::open(rp)) {
                    state.repo = std::move(nr);  // see the fetched objects
                }
                if (!state.current_file.empty()) {
                    open_pr_file(state.current_file);  // re-render locally (exact)
                }
            });
        });
    };

    // Prefetch (background) a PR's full detail into pr_detail_cache so opening it is
    // instant. Skips already-cached PRs; cache-only (no UI change).
    auto prefetch_pr_detail = [&](const std::string& id) {
        if (!state.repo || !state.bitbucket_cred) {
            return;
        }
        auto parsed = diffy::review::parse_remote_url(state.repo->origin_url());
        if (!parsed) {
            return;
        }
        const std::string key = parsed->owner + "/" + parsed->repo + "#" + id;
        if (state.pr_detail_cache.count(key)) {
            return;
        }
        const diffy::review::Credential cred = *state.bitbucket_cred;
        const std::string ws = parsed->owner, repo = parsed->repo;
        const uint64_t gen = state.load_generation;
        review_pool.submit(ReviewPool::Prio::Prefetch,
                           [&, id, ws, repo, cred, key, gen](diffy::review::HttpClient& http) {
            State::PrDetail d;
            std::string err;
            fetch_pr_detail(http, cred, ws, repo, id, d, err);
            if (!err.empty()) {
                return;
            }
            slint::invoke_from_event_loop([&, key, d, gen]() {
                if (gen != state.load_generation) {
                    return;
                }
                state.pr_detail_cache[key] = d;  // cache-only; no UI change
            });
        });
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

    // Detect whether the open repo's origin is a connected Bitbucket remote and,
    // if so, list its open PRs into the right sidebar. Runs the network call off
    // the UI thread; a load_generation check drops results if the repo switched.
    // Build the PrEntry model from neutral PRs and push it to the sidebar.
    auto show_prs = [&](const std::vector<diffy::review::PullRequest>& prs) {
        auto model = std::make_shared<slint::VectorModel<PrEntry>>();
        for (const auto& pr : prs) {
            PrEntry e;
            e.id = ss(pr.id);
            e.title = ss(pr.title);
            e.author = ss(pr.author);
            e.source = ss(pr.src_branch);
            e.dest = ss(pr.dst_branch);
            e.state = ss(approval_str(pr.state));
            e.comments = pr.comment_count;
            e.updated = ss(pr.updated);
            model->push_back(e);
        }
        backend.set_pull_requests(model);
    };

    auto refresh_prs = [&]() {
        if (!state.repo || !state.bitbucket_cred) {
            backend.set_has_bitbucket(false);
            return;
        }
        auto parsed = diffy::review::parse_remote_url(state.repo->origin_url());
        if (!parsed || parsed->host.find("bitbucket.org") == std::string::npos) {
            backend.set_has_bitbucket(false);
            return;
        }
        backend.set_has_bitbucket(true);
        const std::string ws = parsed->owner;
        const std::string repo = parsed->repo;
        const std::string key = ws + "/" + repo;
        // Show the last result instantly; only clear when nothing is cached (so a
        // repo switch doesn't leave the previous repo's PRs showing).
        auto it = state.pr_list_cache.find(key);
        if (it != state.pr_list_cache.end()) {
            show_prs(it->second);
        } else {
            backend.set_pull_requests(std::make_shared<slint::VectorModel<PrEntry>>());
        }
        backend.set_loading_prs(true);
        const diffy::review::Credential cred = *state.bitbucket_cred;
        const uint64_t gen = state.load_generation;
        review_pool.submit(ReviewPool::Prio::Interactive,
                           [&, ws, repo, key, cred, gen](diffy::review::HttpClient& http) {
            diffy::review::BitbucketCloudClient client(http, cred, ws, repo);
            auto prs = client.list_open();
            auto items = std::make_shared<std::vector<diffy::review::PullRequest>>();
            std::string err;
            if (prs) {
                *items = std::move(prs.value().items);
            } else {
                err = prs.error().message;
                diffy::review::log_line("list_open(" + key + ") failed: " + err);
            }
            slint::invoke_from_event_loop([&, items, key, gen, err]() {
                if (gen != state.load_generation) {
                    return;  // repo switched while listing
                }
                backend.set_loading_prs(false);
                if (err.empty()) {
                    state.pr_list_cache[key] = *items;  // cache + swap in the fresh result
                    show_prs(*items);
                    // Warm each PR's detail in the background so opening any is instant.
                    int pf = 0;
                    for (const auto& pr : *items) {
                        if (pf++ >= 20) {
                            break;  // cap speculative detail prefetch (rate limits)
                        }
                        prefetch_pr_detail(pr.id);
                    }
                } else {
                    backend.set_status_text(ss("Couldn't load PRs: " + err));
                }
            });
        });
    };

    auto apply_files = [&](std::shared_ptr<LoadFiles> f, uint64_t gen) {
        if (gen != state.load_generation) {
            return;
        }
        state.repo = std::move(f->repo);
        state.current_commit.clear();  // opening a repo starts in working-tree mode
        backend.set_on_working_tree(true);
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

        // Detect a hosted-review remote and list its PRs into the sidebar
        // (cache-first + background swap; see refresh_prs).
        refresh_prs();
    };

    auto load_repo = [&](const std::string& path) {
        const uint64_t gen = ++state.load_generation;
        backend.set_loading(true);
        backend.set_status_text(ss("Opening " + path + " …"));
        // Leave any PR detail mode when (re)opening a repo.
        state.current_pr.clear();
        state.pr_threads.clear();
        backend.set_pr_open(false);
        review_pool.clear_prefetch();  // drop speculative work for the previous repo
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
        if (!state.repo || !state.current_pr.empty()) {
            return;  // in PR detail mode the commits list is read-only for now
        }
        state.current_commit = oid;
        backend.set_on_working_tree(false);
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

    // Leave a historical commit and return to the working-tree (uncommitted) view.
    // Re-scans status so the changes list and staged count are current, then opens
    // the first changed file. Cheap enough to run on the UI thread.
    auto select_working_tree = [&]() {
        if (!state.repo) {
            return;
        }
        state.current_commit.clear();
        backend.set_on_working_tree(true);
        backend.set_commit_sel_index(-1);
        auto files = state.repo->status();
        int staged = 0;
        for (const auto& fc : files) {
            if (fc.staged) {
                ++staged;
            }
        }
        backend.set_staged_count(staged);
        set_files(files);
        backend.set_status_text(ss(std::to_string(files.size()) + " changed file(s)"));
        if (!files.empty()) {
            open_file(files.front().path);
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
            // The list may contain section headers, so navigate by ordinal among
            // the file rows (derived from the open file) and map back to a model
            // row for the scroll/highlight.
            int ord = -1;
            for (size_t i = 0; i < list.size(); ++i) {
                if (list[i] == state.current_file) {
                    ord = static_cast<int>(i);
                    break;
                }
            }
            if (dir == "activate") {
                if (ord >= 0) {
                    open_file(list[ord]);
                }
                return;
            }
            ord = step(ord, static_cast<int>(list.size()), dir);
            backend.set_file_sel_index(state.file_model_index[ord]);
            open_file(list[ord]);  // selection opens the diff (mirrors a click)
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

    // Periodic background re-scan that updates the changes list only when it
    // actually changed, leaving the open diff and selection untouched. (Slint
    // exposes no window-focus event, so this is time-based.)
    struct SoftStatus {
        std::vector<diffy::gui::FileChange> files;
        std::vector<Repo::BranchInfo> branches;
    };
    auto soft_refresh = [&]() {
        if (state.repo_path.empty() || state.soft_refreshing || backend.get_loading() ||
            !state.current_commit.empty() || !state.current_pr.empty()) {
            return;  // busy, mid-load, browsing a commit, or in PR mode — skip
        }
        state.soft_refreshing = true;
        const uint64_t gen = state.load_generation;
        const std::string path = state.repo_path;
        load_threads.emplace_back([&, gen, path]() {
            auto out = std::make_shared<SoftStatus>();
            if (auto r = Repo::open(path)) {
                out->files = r->status();
                out->branches = r->branches();
            }
            slint::invoke_from_event_loop([&, out, gen]() {
                state.soft_refreshing = false;
                if (gen != state.load_generation) {
                    return;
                }
                bool changed = out->files.size() != state.all_files.size();
                for (size_t i = 0; !changed && i < out->files.size(); ++i) {
                    const auto& a = out->files[i];
                    const auto& b = state.all_files[i];
                    changed = a.path != b.path || a.status != b.status || a.staged != b.staged;
                }
                if (!changed) {
                    return;
                }
                set_files(out->files);
                int staged = 0;
                for (const auto& fc : out->files) {
                    if (fc.staged) {
                        ++staged;
                    }
                }
                backend.set_staged_count(staged);
                auto names = std::make_shared<slint::VectorModel<slint::SharedString>>();
                for (const auto& bi : out->branches) {
                    if (!bi.remote) {
                        names->push_back(ss(bi.name));
                    }
                }
                backend.set_branch_names(names);
            });
        });
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
                    // Highlight/scroll to the logical line's first visual row.
                    state.find_rows.push_back(state.first_visual[i]);
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

    // Inject text into the focused field via a synthetic, modifier-free key event.
    // Works around the upstream Slint/winit stuck-modifier bug (Slint #6263) that
    // drops AltGr-composed characters like "@" on Nordic layouts: the synthetic
    // event carries no Ctrl/Alt, so the text is inserted at the cursor regardless
    // of the corrupted physical-modifier state.
    backend.on_type_text([&](slint::SharedString t) {
        // Insert `t` at the focused field's cursor via a synthetic, modifier-free
        // key event (the .slint re-focuses the field first). Logged so we can see
        // the handler fire and confirm the dispatch path.
        diffy::review::log_line(std::string("type_text: dispatching '") + str(t) + "'");
        ui->window().dispatch_key_press_event(t);
        ui->window().dispatch_key_release_event(t);
    });

    backend.on_refresh_prs([&]() { refresh_prs(); });
    backend.on_open_pr([&](slint::SharedString id) { open_pr(str(id)); });
    backend.on_close_pr([&]() {
        // Leave PR detail mode and restore the repo view (working tree + local
        // commits + the PR list) by reloading the repo.
        state.current_pr.clear();
        backend.set_pr_open(false);
        if (!state.repo_path.empty()) {
            load_repo(state.repo_path);
        }
    });

    // Connect Bitbucket: verify the credentials live (whoami) off the UI thread,
    // store the token in the OS credential vault on success, and — when a
    // workspace+repo are supplied — fetch an open-PR count so the connection can
    // be tested end-to-end. (Surfacing PRs in the sidebar is the rest of P1.)
    backend.on_connect_bitbucket([&](slint::SharedString ws, slint::SharedString repo,
                                     slint::SharedString user, slint::SharedString pass) {
        backend.set_bitbucket_connecting(true);
        backend.set_bitbucket_status(ss("Connecting…"));
        const std::string w = str(ws), rp = str(repo), u = str(user), p = str(pass);
        review_pool.submit(ReviewPool::Prio::Interactive,
                           [&, w, rp, u, p](diffy::review::HttpClient& http) {
            namespace rv = diffy::review;
            rv::Credential cred;
            if (u.empty()) {
                // No email → a Bitbucket Access token (scoped), sent as Bearer.
                cred.method = rv::AuthMethod::Bearer;
                cred.secret = p;
            } else {
                // Email present → an Atlassian API token / app password over Basic.
                cred.method = rv::AuthMethod::BasicToken;
                cred.principal = u;
                cred.secret = p;
            }
            rv::BitbucketCloudClient client(http, cred, w, rp);
            rv::log_line("connect bitbucket: ws='" + w + "' repo='" + rp +
                         "' auth=" + (u.empty() ? "bearer(access-token)" : "basic(email)"));

            bool ok = false;
            std::string status;
            std::string account;
            int pr_count = -1;

            auto describe = [](const rv::Error& e, const std::string& action) {
                std::string m = "Couldn't " + action;
                if (e.http_status) {
                    m += " (HTTP " + std::to_string(e.http_status) + ")";
                }
                if (e.kind == rv::ErrorKind::Auth && e.http_status == 403) {
                    m += " — the token is valid but lacks the required scope";
                } else if (e.kind == rv::ErrorKind::Auth) {
                    m += " — credentials rejected";
                }
                if (!e.message.empty()) {
                    m += ": " + e.message;
                }
                return m;
            };

            // Verify with the capability we actually need: with a repo, list its
            // PRs (needs only the pull-request read scope the token was made for);
            // without one, fall back to whoami() (which needs an account scope some
            // tokens omit — a common cause of a spurious "sign-in failed").
            if (!rp.empty()) {
                auto prs = client.list_open();
                if (prs) {
                    ok = true;
                    pr_count = static_cast<int>(prs.value().items.size());
                } else {
                    status = describe(prs.error(), "list pull requests");
                }
            } else {
                auto who = client.whoami();
                if (who) {
                    ok = true;
                    account = who.value().display_name.empty() ? who.value().username
                                                               : who.value().display_name;
                } else {
                    status = describe(who.error(), "sign in");
                }
            }

            if (ok) {
                if (account.empty()) {
                    if (auto who = client.whoami()) {  // best-effort display name
                        account = who.value().display_name.empty() ? who.value().username
                                                                   : who.value().display_name;
                    }
                }
                rv::SecretStore::set(rv::build_key("bitbucket-cloud",
                                                   "https://api.bitbucket.org/2.0",
                                                   u.empty() ? w : u),
                                     p);
                status = account.empty() ? "Connected" : ("Connected as " + account);
                if (pr_count >= 0) {
                    status += "  ·  " + std::to_string(pr_count) + " open PR(s) in " + w + "/" + rp;
                }
            }
            rv::log_line(std::string("connect result: ") + (ok ? "OK — " : "FAIL — ") + status);

            slint::invoke_from_event_loop([&, ok, status, u, p]() {
                backend.set_bitbucket_connecting(false);
                backend.set_bitbucket_connected(ok);
                backend.set_bitbucket_status(ss(status));
                if (ok) {
                    rv::Credential c;
                    if (u.empty()) {
                        c.method = rv::AuthMethod::Bearer;
                        c.secret = p;
                    } else {
                        c.method = rv::AuthMethod::BasicToken;
                        c.principal = u;
                        c.secret = p;
                    }
                    state.bitbucket_cred = c;
                    // Persist the Basic/email account so it auto-reconnects next
                    // launch (Bearer access tokens stay session-only).
                    if (!u.empty()) {
                        state.settings.bitbucket_account = u;
                        gui_settings_save(state.settings);
                    }
                    // If the open repo is a Bitbucket remote, populate the sidebar now.
                    refresh_prs();
                }
            });
        });
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
    backend.on_select_working_tree([&]() { select_working_tree(); });
    backend.on_navigate([&](slint::SharedString d) { navigate(str(d)); });
    backend.on_refresh([&]() { refresh(); });
    backend.on_auto_refresh([&]() { soft_refresh(); });
    backend.on_regroup_files([&]() { render_files(); });
    backend.on_set_base_ref([&](slint::SharedString r) {
        state.base_ref = str(r);
        // Re-diff the open file against the new base (working-tree mode only).
        if (state.repo && state.current_commit.empty() && !state.current_file.empty()) {
            open_file(state.current_file);
        }
    });
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
    // --- context-menu actions ----------------------------------------------
    backend.on_copy_to_clipboard([&](slint::SharedString t) { copy_to_clipboard(str(t)); });
    backend.on_open_file([&](slint::SharedString p) {
        if (state.repo) {
            shell_open(join_path(state.repo->workdir(), str(p)));
        }
    });
    backend.on_reveal_file([&](slint::SharedString p) {
        if (state.repo) {
            shell_reveal(join_path(state.repo->workdir(), str(p)));
        }
    });
    backend.on_checkout_commit([&](slint::SharedString oid) {
        if (!state.repo) {
            return;
        }
        if (!state.repo->checkout_commit(str(oid))) {
            backend.set_status_text(ss("Checkout failed — commit or stash changes first."));
        }
        refresh();
    });
    backend.on_create_branch([&](slint::SharedString oid, slint::SharedString name) {
        if (!state.repo || str(name).empty()) {
            return;
        }
        if (!state.repo->create_branch_at(str(name), str(oid))) {
            backend.set_status_text(ss("Create branch failed (name exists or dirty tree)."));
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
        apply_window_chrome(gui_theme);  // retint the title bar
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

    // The UI reports the cell width + measured font advance; derive the wrap column
    // count and re-wrap the rows when it changes (only meaningful with wrap on).
    backend.on_set_wrap_metrics([&](float cell_px, float advance_px) {
        // Columns that fit: 6px left pad + slack for the vertical scrollbar so the
        // last column never clips (better to wrap a char early than to truncate).
        int wc = (advance_px > 0.5f) ? static_cast<int>((cell_px - 16.0f) / advance_px) : 0;
        if (wc < 1) {
            wc = 1;
        }
        if (wc == wrap_cols) {
            return;
        }
        wrap_cols = wc;
        if (!options.get_word_wrap() || !state.pair.ok) {
            return;
        }
        // Re-wrapping rebuilds every row — too heavy to run on each resize tick (it
        // stalls painting, so the window goes blank). Coalesce: re-wrap once the
        // size settles.
        wrap_debounce.start(slint::TimerMode::SingleShot, std::chrono::milliseconds(80), [&]() {
            if (options.get_word_wrap() && state.pair.ok) {
                relayout();
            }
        });
    });

    set_repo_names();
    if (settings.restore_last_repo && !state.repos.empty()) {
        load_repo(state.repos.front().path);
    }

    // Theme the OS title bar once the native window exists (after the loop starts).
    slint::Timer::single_shot(std::chrono::milliseconds(120),
                              [&]() { apply_window_chrome(gui_theme); });

    ui->run();

    // Stop the review pool (join its workers) and let any in-flight background
    // loads finish before libgit2 is torn down.
    review_pool.stop();
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
