#include "repo_model.hpp"

#include <git2.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace diffy::gui {

namespace {

std::string
read_disk_file(const std::string& abs_path) {
    std::ifstream f(abs_path, std::ios::binary);
    if (!f) {
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Content of `path` inside a git tree, or "" if the path isn't present.
std::string
blob_text_in_tree(git_tree* tree, const std::string& path) {
    if (!tree) {
        return {};
    }
    git_tree_entry* entry = nullptr;
    if (git_tree_entry_bypath(&entry, tree, path.c_str()) != 0) {
        return {};
    }
    std::string out;
    git_object* obj = nullptr;
    if (git_tree_entry_to_object(&obj, git_tree_owner(tree), entry) == 0) {
        if (git_object_type(obj) == GIT_OBJECT_BLOB) {
            auto* blob = reinterpret_cast<git_blob*>(obj);
            const void* raw = git_blob_rawcontent(blob);
            auto size = static_cast<size_t>(git_blob_rawsize(blob));
            out.assign(static_cast<const char*>(raw), size);
        }
        git_object_free(obj);
    }
    git_tree_entry_free(entry);
    return out;
}

// Peel a commit to its tree (caller frees the tree).
git_tree*
commit_tree(git_commit* commit) {
    if (!commit) {
        return nullptr;
    }
    git_tree* tree = nullptr;
    if (git_commit_tree(&tree, commit) != 0) {
        return nullptr;
    }
    return tree;
}

// Look up a commit by a full or abbreviated hex sha (caller frees).
git_commit*
lookup_commit_flex(git_repository* repo, const std::string& sha) {
    git_oid oid;
    if (sha.empty() || git_oid_fromstrn(&oid, sha.data(), sha.size()) != 0) {
        return nullptr;
    }
    git_commit* commit = nullptr;
    const int rc = (sha.size() >= GIT_OID_HEXSZ)
                       ? git_commit_lookup(&commit, repo, &oid)
                       : git_commit_lookup_prefix(&commit, repo, &oid, sha.size());
    return rc == 0 ? commit : nullptr;
}

git_tree*
head_tree(git_repository* repo) {
    git_object* obj = nullptr;
    if (git_revparse_single(&obj, repo, "HEAD^{tree}") != 0) {
        return nullptr;
    }
    return reinterpret_cast<git_tree*>(obj);
}

std::string
delta_status_code(git_delta_t s) {
    switch (s) {
        case GIT_DELTA_ADDED:
            return "A";
        case GIT_DELTA_DELETED:
            return "D";
        case GIT_DELTA_MODIFIED:
            return "M";
        case GIT_DELTA_RENAMED:
            return "R";
        case GIT_DELTA_COPIED:
            return "C";
        case GIT_DELTA_TYPECHANGE:
            return "T";
        default:
            return "?";
    }
}

// Above this size (per side) a token diff is too costly / unhelpful.
constexpr size_t kMaxDiffBytes = 2 * 1024 * 1024;

// Classify a blob pair: "" means plain text (diff it), otherwise a reason the
// content shouldn't be diffed (shown verbatim by the frontend).
std::string
classify_blob_pair(const std::string& a, const std::string& b) {
    if (a.size() > kMaxDiffBytes || b.size() > kMaxDiffBytes) {
        return "File too large to diff";
    }
    // A NUL byte is git's own binary heuristic; good enough here.
    if (a.find('\0') != std::string::npos || b.find('\0') != std::string::npos) {
        return "Binary file";
    }
    return "";
}

std::string
status_code(unsigned int s) {
    // Prefer the working-tree state, fall back to the index state.
    if (s & GIT_STATUS_WT_NEW) return "?";
    if (s & GIT_STATUS_WT_DELETED) return "D";
    if (s & GIT_STATUS_WT_MODIFIED) return "M";
    if (s & GIT_STATUS_WT_RENAMED) return "R";
    if (s & GIT_STATUS_WT_TYPECHANGE) return "T";
    if (s & GIT_STATUS_INDEX_NEW) return "A";
    if (s & GIT_STATUS_INDEX_DELETED) return "D";
    if (s & GIT_STATUS_INDEX_MODIFIED) return "M";
    if (s & GIT_STATUS_INDEX_RENAMED) return "R";
    return "?";
}

}  // namespace

void
git_runtime_init() {
    git_libgit2_init();
}

void
git_runtime_shutdown() {
    git_libgit2_shutdown();
}

std::optional<Repo>
Repo::open(const std::string& path) {
    git_repository* repo = nullptr;
    if (git_repository_open_ext(&repo, path.c_str(), 0, nullptr) != 0) {
        return std::nullopt;
    }
    return Repo{repo};
}

Repo::~Repo() {
    if (repo_) {
        git_repository_free(repo_);
    }
}

Repo::Repo(Repo&& other) noexcept : repo_(std::exchange(other.repo_, nullptr)) {}

Repo&
Repo::operator=(Repo&& other) noexcept {
    if (this != &other) {
        if (repo_) {
            git_repository_free(repo_);
        }
        repo_ = std::exchange(other.repo_, nullptr);
    }
    return *this;
}

std::string
Repo::workdir() const {
    const char* wd = git_repository_workdir(repo_);
    return wd ? wd : "";
}

std::string
Repo::head_branch() const {
    git_reference* head = nullptr;
    if (git_repository_head(&head, repo_) != 0) {
        return "(detached)";
    }
    const char* name = git_reference_shorthand(head);
    std::string out = name ? name : "";
    git_reference_free(head);
    return out;
}

std::string
Repo::origin_url() const {
    git_remote* remote = nullptr;
    if (git_remote_lookup(&remote, repo_, "origin") != 0) {
        return "";
    }
    const char* url = git_remote_url(remote);
    std::string out = url ? url : "";
    git_remote_free(remote);
    return out;
}

std::vector<FileChange>
Repo::status() const {
    std::vector<FileChange> out;

    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    // EXCLUDE_SUBMODULES stops status from recursing into every submodule, and
    // not setting RECURSE_UNTRACKED_DIRS keeps untracked directories collapsed.
    // Together these keep `status` fast on large trees (e.g. an ESP-IDF project
    // with many components/submodules and a big untracked build/ directory).
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX |
                 GIT_STATUS_OPT_SORT_CASE_SENSITIVELY | GIT_STATUS_OPT_EXCLUDE_SUBMODULES;

    git_status_list* list = nullptr;
    if (git_status_list_new(&list, repo_, &opts) != 0) {
        return out;
    }

    const size_t count = git_status_list_entrycount(list);
    for (size_t i = 0; i < count; ++i) {
        const git_status_entry* e = git_status_byindex(list, i);
        if (!e || e->status == GIT_STATUS_CURRENT) {
            continue;
        }
        const char* path = nullptr;
        if (e->index_to_workdir && e->index_to_workdir->new_file.path) {
            path = e->index_to_workdir->new_file.path;
        } else if (e->head_to_index && e->head_to_index->new_file.path) {
            path = e->head_to_index->new_file.path;
        }
        if (!path) {
            continue;
        }
        FileChange fc;
        fc.path = path;
        // A wholly-untracked directory is collapsed by libgit2 into a single entry
        // whose path carries a trailing slash (we deliberately don't set
        // RECURSE_UNTRACKED_DIRS — see the flags above). Drop that slash so the UI
        // renders it as one named row (e.g. "build-gui") instead of an empty-named
        // "?" leaf hanging under a phantom folder of the same name.
        if (fc.path.size() > 1 && fc.path.back() == '/') {
            fc.path.pop_back();
        }
        fc.status = status_code(e->status);
        fc.staged = (e->status & (GIT_STATUS_INDEX_NEW | GIT_STATUS_INDEX_MODIFIED |
                                  GIT_STATUS_INDEX_DELETED | GIT_STATUS_INDEX_RENAMED |
                                  GIT_STATUS_INDEX_TYPECHANGE)) != 0;
        out.push_back(std::move(fc));
    }

    git_status_list_free(list);
    return out;
}

std::vector<CommitInfo>
Repo::commits(int skip, int max_count) const {
    std::vector<CommitInfo> out;

    git_revwalk* walk = nullptr;
    if (git_revwalk_new(&walk, repo_) != 0) {
        return out;
    }
    git_revwalk_sorting(walk, GIT_SORT_TIME);
    if (git_revwalk_push_head(walk) != 0) {
        git_revwalk_free(walk);
        return out;
    }

    git_oid oid;
    for (int skipped = 0; skipped < skip && git_revwalk_next(&oid, walk) == 0; ++skipped) {
        // discard the first `skip` commits
    }

    int n = 0;
    while ((max_count <= 0 || n < max_count) && git_revwalk_next(&oid, walk) == 0) {
        git_commit* commit = nullptr;
        if (git_commit_lookup(&commit, repo_, &oid) != 0) {
            continue;
        }
        CommitInfo ci;
        char buf[GIT_OID_HEXSZ + 1] = {0};
        git_oid_tostr(buf, sizeof(buf), &oid);
        ci.oid = buf;
        ci.short_oid = std::string(buf).substr(0, 8);
        const char* summary = git_commit_summary(commit);
        ci.summary = summary ? summary : "";
        const git_signature* author = git_commit_author(commit);
        ci.author = author ? author->name : "";
        ci.time = static_cast<int64_t>(git_commit_time(commit));
        out.push_back(std::move(ci));
        git_commit_free(commit);
        ++n;
    }

    git_revwalk_free(walk);
    return out;
}

std::vector<CommitInfo>
Repo::recent_commits(int max_count) const {
    return commits(0, max_count);
}

std::vector<FileChange>
Repo::commit_files(const std::string& commit_oid) const {
    std::vector<FileChange> out;

    git_oid oid;
    if (git_oid_fromstr(&oid, commit_oid.c_str()) != 0) {
        return out;
    }
    git_commit* commit = nullptr;
    if (git_commit_lookup(&commit, repo_, &oid) != 0) {
        return out;
    }

    git_tree* tree = commit_tree(commit);
    git_tree* parent_tree = nullptr;
    git_commit* parent = nullptr;
    if (git_commit_parentcount(commit) > 0 && git_commit_parent(&parent, commit, 0) == 0) {
        parent_tree = commit_tree(parent);
    }

    git_diff* diff = nullptr;
    git_diff_options dopts = GIT_DIFF_OPTIONS_INIT;
    if (git_diff_tree_to_tree(&diff, repo_, parent_tree, tree, &dopts) == 0) {
        const size_t n = git_diff_num_deltas(diff);
        for (size_t i = 0; i < n; ++i) {
            const git_diff_delta* d = git_diff_get_delta(diff, i);
            const char* path =
                d->new_file.path ? d->new_file.path : (d->old_file.path ? d->old_file.path : nullptr);
            if (!path) {
                continue;
            }
            FileChange fc;
            fc.path = path;
            fc.status = delta_status_code(d->status);
            fc.staged = false;
            out.push_back(std::move(fc));
        }
        git_diff_free(diff);
    }

    if (parent_tree) {
        git_tree_free(parent_tree);
    }
    if (parent) {
        git_commit_free(parent);
    }
    if (tree) {
        git_tree_free(tree);
    }
    git_commit_free(commit);
    return out;
}

BlobPair
Repo::diff_workdir_file(const std::string& path) const {
    BlobPair pair;
    pair.old_name = path;
    pair.new_name = path;

    git_tree* tree = head_tree(repo_);
    pair.old_text = blob_text_in_tree(tree, path);
    if (tree) {
        git_tree_free(tree);
    }

    const std::string abs = (fs::path(workdir()) / path).string();
    pair.new_text = read_disk_file(abs);

    pair.note = classify_blob_pair(pair.old_text, pair.new_text);
    pair.ok = true;
    return pair;
}

BlobPair
Repo::diff_commit_file(const std::string& commit_oid, const std::string& path) const {
    BlobPair pair;
    pair.old_name = path;
    pair.new_name = path;

    git_oid oid;
    if (git_oid_fromstr(&oid, commit_oid.c_str()) != 0) {
        return pair;
    }
    git_commit* commit = nullptr;
    if (git_commit_lookup(&commit, repo_, &oid) != 0) {
        return pair;
    }

    git_tree* tree = commit_tree(commit);
    pair.new_text = blob_text_in_tree(tree, path);
    if (tree) {
        git_tree_free(tree);
    }

    git_commit* parent = nullptr;
    if (git_commit_parentcount(commit) > 0 && git_commit_parent(&parent, commit, 0) == 0) {
        git_tree* parent_tree = commit_tree(parent);
        pair.old_text = blob_text_in_tree(parent_tree, path);
        if (parent_tree) {
            git_tree_free(parent_tree);
        }
        git_commit_free(parent);
    }

    git_commit_free(commit);
    pair.note = classify_blob_pair(pair.old_text, pair.new_text);
    pair.ok = true;
    return pair;
}

BlobPair
Repo::diff_oids(const std::string& old_sha, const std::string& new_sha,
                const std::string& path) const {
    BlobPair pair;
    pair.old_name = path;
    pair.new_name = path;
    auto blob_at = [&](const std::string& sha) -> std::string {
        git_commit* commit = lookup_commit_flex(repo_, sha);
        if (!commit) {
            return "";
        }
        git_tree* tree = commit_tree(commit);
        std::string text = blob_text_in_tree(tree, path);
        if (tree) {
            git_tree_free(tree);
        }
        git_commit_free(commit);
        return text;
    };
    if (!old_sha.empty()) {
        pair.old_text = blob_at(old_sha);
    }
    if (!new_sha.empty()) {
        pair.new_text = blob_at(new_sha);
    }
    pair.note = classify_blob_pair(pair.old_text, pair.new_text);
    pair.ok = true;
    return pair;
}

bool
Repo::has_commit(const std::string& sha) const {
    git_commit* commit = lookup_commit_flex(repo_, sha);
    if (!commit) {
        return false;
    }
    git_commit_free(commit);
    return true;
}

Repo::CommitText
Repo::commit_text(const std::string& sha) const {
    CommitText out;
    git_commit* commit = lookup_commit_flex(repo_, sha);
    if (!commit) {
        return out;
    }
    char buf[GIT_OID_HEXSZ + 1] = {0};
    git_oid_tostr(buf, 8, git_commit_id(commit));  // 7 hex + NUL
    out.short_sha = buf;
    if (const char* s = git_commit_summary(commit)) {
        out.summary = s;
    }
    if (const char* m = git_commit_message(commit)) {
        out.message = m;
    }
    if (const git_signature* a = git_commit_author(commit)) {
        out.author = a->name ? a->name : "";
        out.time = static_cast<int64_t>(a->when.time);
    }
    out.ok = true;
    git_commit_free(commit);
    return out;
}

std::string
Repo::merge_base(const std::string& a, const std::string& b) const {
    git_commit* ca = lookup_commit_flex(repo_, a);
    git_commit* cb = lookup_commit_flex(repo_, b);
    std::string out;
    if (ca && cb) {
        git_oid base;
        if (git_merge_base(&base, repo_, git_commit_id(ca), git_commit_id(cb)) == 0) {
            char buf[GIT_OID_HEXSZ + 1] = {0};
            git_oid_tostr(buf, sizeof(buf), &base);
            out = buf;
        }
    }
    if (ca) {
        git_commit_free(ca);
    }
    if (cb) {
        git_commit_free(cb);
    }
    return out;
}

std::vector<std::string>
pr_branch_refspecs(const std::string& head_ref, const std::string& base_ref,
                   const std::string& pr_id) {
    // A bare branch name lives under refs/heads/; a ref that already starts with
    // "refs/" (e.g. GitHub's refs/pull/<id>/head) is fetchable as-is. Prepending
    // refs/heads/ unconditionally produced "refs/heads/refs/pull/.../head", which no
    // remote has — silently disabling GitHub's local diff.
    auto qualify = [](const std::string& ref) {
        return ref.rfind("refs/", 0) == 0 ? ref : "refs/heads/" + ref;
    };
    std::vector<std::string> out;
    if (head_ref.empty()) {
        return out;
    }
    out.push_back("+" + qualify(head_ref) + ":refs/diffy/pr/" + pr_id + "/head");
    if (!base_ref.empty()) {
        out.push_back("+" + qualify(base_ref) + ":refs/diffy/pr/" + pr_id + "/base");
    }
    return out;
}

bool
Repo::git_fetch(const std::string& repo_path, const std::string& remote,
                const std::vector<std::string>& refspecs, const std::string& user,
                const std::string& pass, std::string* error) {
#if defined(_WIN32)
    (void)repo_path;
    (void)remote;
    (void)refspecs;
    (void)user;
    (void)pass;
    if (error) {
        *error = "system git fetch not implemented on this platform";
    }
    return false;
#else
    if (refspecs.empty()) {
        return false;
    }
    // exec-style argv (no shell): branch/refspec text is passed verbatim and can't
    // be interpreted as shell syntax. --no-write-fetch-head keeps FETCH_HEAD clean;
    // --quiet suppresses progress on stdout.
    std::vector<std::string> args = {"git",     "-C",      repo_path, "fetch", "--no-tags",
                                     "--quiet", "--no-write-fetch-head", remote};
    args.insert(args.end(), refspecs.begin(), refspecs.end());
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& a : args) {
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        if (error) {
            *error = "pipe() failed";
        }
        return false;
    }
    // Close-on-exec so a concurrently-spawned git (another PR's fetch) doesn't inherit
    // this pipe's write end and keep our read() from ever seeing EOF (this child gets
    // stderr via the dup2 in the file actions below, which clears CLOEXEC on fd 2).
    fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], STDERR_FILENO);  // capture git's stderr
    posix_spawn_file_actions_addclose(&fa, pipefd[0]);
    posix_spawn_file_actions_addclose(&fa, pipefd[1]);

    // Inherit the environment but force GIT_TERMINAL_PROMPT=0 so a missing credential
    // fails fast rather than blocking this background thread on an interactive prompt.
    std::vector<std::string> envs;
    for (char** e = environ; e && *e; ++e) {
        if (std::string(*e).rfind("GIT_TERMINAL_PROMPT=", 0) != 0) {
            envs.emplace_back(*e);
        }
    }
    envs.emplace_back("GIT_TERMINAL_PROMPT=0");
    // Authenticate with the app-held token (Bitbucket API token / GitHub PAT) via a
    // throwaway GIT_ASKPASS helper that echoes the credential from the child env — the
    // secret is never on argv or in .git/config. Falls back to ambient git creds/SSH
    // when no token is given.
    std::string askpass_path;
    if (!user.empty() || !pass.empty()) {
        char tmpl[] = "/tmp/diffy-askpass-XXXXXX";
        const int afd = mkstemp(tmpl);
        if (afd >= 0) {
            static const char* kScript =
                "#!/bin/sh\ncase \"$1\" in\n"
                "Username*) printf %s \"$DIFFY_GIT_USER\" ;;\n"
                "*) printf %s \"$DIFFY_GIT_PASS\" ;;\nesac\n";
            const size_t len = std::strlen(kScript);
            const bool wrote = write(afd, kScript, len) == static_cast<ssize_t>(len);
            close(afd);
            if (wrote && chmod(tmpl, 0700) == 0) {
                askpass_path = tmpl;
                envs.emplace_back("GIT_ASKPASS=" + askpass_path);
                envs.emplace_back("DIFFY_GIT_USER=" + user);
                envs.emplace_back("DIFFY_GIT_PASS=" + pass);
            } else {
                unlink(tmpl);
            }
        }
    }
    std::vector<char*> envp;
    envp.reserve(envs.size() + 1);
    for (auto& e : envs) {
        envp.push_back(const_cast<char*>(e.c_str()));
    }
    envp.push_back(nullptr);

    pid_t pid = 0;
    const int spawn_rc = posix_spawnp(&pid, "git", &fa, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&fa);
    close(pipefd[1]);  // parent doesn't write
    if (spawn_rc != 0) {
        close(pipefd[0]);
        if (error) {
            *error = "could not launch git (is it on PATH?)";
        }
        return false;
    }

    std::string captured;
    char buf[512];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        captured.append(buf, static_cast<size_t>(n));
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (!askpass_path.empty()) {
        unlink(askpass_path.c_str());
    }
    const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!ok && error) {
        while (!captured.empty() && (captured.back() == '\n' || captured.back() == '\r')) {
            captured.pop_back();
        }
        const auto nl = captured.find_last_of('\n');
        *error = nl == std::string::npos ? captured : captured.substr(nl + 1);
        if (error->empty()) {
            *error = "git fetch failed";
        }
    }
    return ok;
#endif
}

bool
Repo::stage_file(const std::string& path) const {
    git_index* idx = nullptr;
    if (git_repository_index(&idx, repo_) != 0) {
        return false;
    }
    std::error_code ec;
    const bool on_disk = fs::exists(fs::path(workdir()) / path, ec);
    int rc = on_disk ? git_index_add_bypath(idx, path.c_str())
                     : git_index_remove_bypath(idx, path.c_str());
    if (rc == 0) {
        rc = git_index_write(idx);
    }
    git_index_free(idx);
    return rc == 0;
}

bool
Repo::unstage_file(const std::string& path) const {
    char* paths[1] = {const_cast<char*>(path.c_str())};
    git_strarray arr{paths, 1};

    git_object* head = nullptr;
    if (git_revparse_single(&head, repo_, "HEAD") != 0) {
        // Unborn HEAD: there's nothing to reset to, so just drop it from the index.
        git_index* idx = nullptr;
        if (git_repository_index(&idx, repo_) != 0) {
            return false;
        }
        int rc = git_index_remove_bypath(idx, path.c_str());
        if (rc == 0) {
            rc = git_index_write(idx);
        }
        git_index_free(idx);
        return rc == 0;
    }
    const int rc = git_reset_default(repo_, head, &arr);
    git_object_free(head);
    return rc == 0;
}

bool
Repo::discard_changes(const std::string& path) const {
    unsigned int st = 0;
    if (git_status_file(&st, repo_, path.c_str()) == 0 && (st & GIT_STATUS_WT_NEW)) {
        // Untracked: discarding means deleting the file.
        std::error_code ec;
        fs::remove(fs::path(workdir()) / path, ec);
        return !ec;
    }
    git_checkout_options opts;
    git_checkout_options_init(&opts, GIT_CHECKOUT_OPTIONS_VERSION);
    opts.checkout_strategy = GIT_CHECKOUT_FORCE;
    char* paths[1] = {const_cast<char*>(path.c_str())};
    opts.paths.strings = paths;
    opts.paths.count = 1;
    return git_checkout_head(repo_, &opts) == 0;
}

bool
Repo::commit(const std::string& message, bool amend) const {
    git_index* idx = nullptr;
    if (git_repository_index(&idx, repo_) != 0) {
        return false;
    }
    git_oid tree_oid;
    int rc = git_index_write_tree(&tree_oid, idx);
    git_index_free(idx);
    if (rc != 0) {
        return false;
    }
    git_tree* tree = nullptr;
    if (git_tree_lookup(&tree, repo_, &tree_oid) != 0) {
        return false;
    }
    git_signature* sig = nullptr;
    if (git_signature_default(&sig, repo_) != 0) {
        git_tree_free(tree);
        return false;
    }

    git_object* head_obj = nullptr;
    git_commit* head_commit = nullptr;
    if (git_revparse_single(&head_obj, repo_, "HEAD") == 0) {
        git_commit_lookup(&head_commit, repo_, git_object_id(head_obj));
    }

    git_oid out_oid;
    if (amend && head_commit) {
        rc = git_commit_amend(&out_oid, head_commit, "HEAD", sig, sig, nullptr, message.c_str(),
                              tree);
    } else {
        const git_commit* parents[1] = {head_commit};
        const size_t nparents = head_commit ? 1 : 0;
        rc = git_commit_create(&out_oid, repo_, "HEAD", sig, sig, nullptr, message.c_str(), tree,
                               nparents, parents);
    }

    if (head_commit) {
        git_commit_free(head_commit);
    }
    if (head_obj) {
        git_object_free(head_obj);
    }
    git_signature_free(sig);
    git_tree_free(tree);
    return rc == 0;
}

std::vector<Repo::BranchInfo>
Repo::branches() const {
    std::vector<BranchInfo> out;
    git_branch_iterator* it = nullptr;
    if (git_branch_iterator_new(&it, repo_, GIT_BRANCH_ALL) != 0) {
        return out;
    }
    git_reference* ref = nullptr;
    git_branch_t type;
    while (git_branch_next(&ref, &type, it) == 0) {
        const char* name = nullptr;
        if (git_branch_name(&name, ref) == 0 && name) {
            BranchInfo bi;
            bi.name = name;
            bi.remote = (type == GIT_BRANCH_REMOTE);
            bi.current = (git_branch_is_head(ref) == 1);
            out.push_back(std::move(bi));
        }
        git_reference_free(ref);
    }
    git_branch_iterator_free(it);
    return out;
}

bool
Repo::checkout_branch(const std::string& name) const {
    git_object* treeish = nullptr;
    if (git_revparse_single(&treeish, repo_, name.c_str()) != 0) {
        return false;
    }
    git_checkout_options opts;
    git_checkout_options_init(&opts, GIT_CHECKOUT_OPTIONS_VERSION);
    opts.checkout_strategy = GIT_CHECKOUT_SAFE;  // refuses to clobber dirty changes
    const int rc = git_checkout_tree(repo_, treeish, &opts);
    git_object_free(treeish);
    if (rc != 0) {
        return false;
    }
    const std::string refname = "refs/heads/" + name;
    return git_repository_set_head(repo_, refname.c_str()) == 0;
}

bool
Repo::checkout_commit(const std::string& commit_oid) const {
    git_object* obj = nullptr;
    if (git_revparse_single(&obj, repo_, commit_oid.c_str()) != 0) {
        return false;
    }
    git_checkout_options opts;
    git_checkout_options_init(&opts, GIT_CHECKOUT_OPTIONS_VERSION);
    opts.checkout_strategy = GIT_CHECKOUT_SAFE;  // refuses to clobber dirty changes
    int rc = git_checkout_tree(repo_, obj, &opts);
    if (rc == 0) {
        rc = git_repository_set_head_detached(repo_, git_object_id(obj));
    }
    git_object_free(obj);
    return rc == 0;
}

bool
Repo::create_branch_at(const std::string& name, const std::string& commit_oid) const {
    git_object* obj = nullptr;
    if (git_revparse_single(&obj, repo_, commit_oid.c_str()) != 0) {
        return false;
    }
    git_commit* commit = nullptr;
    const int look = git_commit_lookup(&commit, repo_, git_object_id(obj));
    git_object_free(obj);
    if (look != 0) {
        return false;
    }
    git_reference* branch = nullptr;
    const int rc = git_branch_create(&branch, repo_, name.c_str(), commit, 0);
    git_commit_free(commit);
    if (rc != 0) {
        return false;  // name already exists, invalid, etc.
    }
    git_reference_free(branch);
    // Switch to the new branch (safe checkout + move HEAD).
    return checkout_branch(name);
}

BlobPair
Repo::diff_ref_file(const std::string& base_ref, const std::string& path) const {
    BlobPair pair;
    pair.old_name = path;
    pair.new_name = path;

    git_object* obj = nullptr;
    const std::string spec = base_ref + "^{tree}";
    if (git_revparse_single(&obj, repo_, spec.c_str()) == 0) {
        pair.old_text = blob_text_in_tree(reinterpret_cast<git_tree*>(obj), path);
        git_object_free(obj);
    }
    const std::string abs = (fs::path(workdir()) / path).string();
    pair.new_text = read_disk_file(abs);
    pair.note = classify_blob_pair(pair.old_text, pair.new_text);
    pair.ok = true;
    return pair;
}

}  // namespace diffy::gui
