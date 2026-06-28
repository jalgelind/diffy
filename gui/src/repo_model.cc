#include "repo_model.hpp"

#include <git2.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

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

}  // namespace diffy::gui
