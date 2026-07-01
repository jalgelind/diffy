// diffy_review — implementation of the shared provider conformance battery.
//
// Every check here is an invariant that must hold for ANY ReviewProvider,
// stated purely in terms of the neutral model + Result + Capabilities. A check
// that fails appends a message to ConformanceReport::failures and bumps
// `failed`; a check that holds bumps `passed`. Nothing throws — one run reports
// every violation so a provider author fixes them in a batch.
//
// See conformance.hpp for how a concrete provider test wires this up over a
// MockHttpClient with recorded fixtures.

#include "conformance.hpp"

#include "capabilities.hpp"
#include "model.hpp"
#include "result.hpp"

#include <string>

namespace diffy::review {

namespace {

// A tiny recorder so each check reads as one call. pass()/fail() keep the tally
// and the failure list in sync.
struct Checker {
    ConformanceReport report;

    void
    pass() {
        ++report.passed;
    }

    void
    fail(std::string msg) {
        ++report.failed;
        report.failures.push_back(std::move(msg));
    }

    // Record pass/fail from a boolean condition with a message used on failure.
    void
    check(bool cond, std::string msg) {
        if (cond) {
            pass();
        } else {
            fail(std::move(msg));
        }
    }
};

// True for every value the ErrorKind enum defines. Any Result error a provider
// returns must carry one of these (the normalized failure taxonomy).
bool
valid_error_kind(ErrorKind k) {
    switch (k) {
        case ErrorKind::Auth:
        case ErrorKind::RateLimited:
        case ErrorKind::NotFound:
        case ErrorKind::Unsupported:
        case ErrorKind::Network:
        case ErrorKind::Parse:
        case ErrorKind::Other:
            return true;
    }
    return false;
}

bool
valid_granularity(CommentGranularity g) {
    switch (g) {
        case CommentGranularity::Line:
        case CommentGranularity::LineRange:
        case CommentGranularity::CharRange:
            return true;
    }
    return false;
}

bool
valid_side(Side s) {
    switch (s) {
        case Side::Old:
        case Side::New:
            return true;
    }
    return false;
}

// Any Result<T> error must carry a valid, normalized ErrorKind. Call on every
// Result the battery inspects so a malformed error surfaces regardless of which
// method produced it.
template <class T>
void
check_error_normalized(Checker& c, const Result<T>& r, const char* who) {
    if (!r) {
        c.check(valid_error_kind(r.error().kind),
                std::string(who) + "(): error carries an out-of-range ErrorKind");
    }
}

}  // namespace

ConformanceReport
run_conformance(ReviewProvider& provider, const std::string& sample_pr_id) {
    Checker c;

    // --- capabilities(): a sane descriptor -------------------------------
    const Capabilities caps = provider.capabilities();
    c.check(!caps.item_noun.empty(), "capabilities(): item_noun is empty");
    c.check(valid_granularity(caps.granularity),
            "capabilities(): granularity is not a valid CommentGranularity value");

    // --- list_open(): a Page<PullRequest> with well-formed items ---------
    {
        Result<Page<PullRequest>> res = provider.list_open();
        check_error_normalized(c, res, "list_open");
        if (res) {
            const Page<PullRequest>& page = res.value();
            for (const PullRequest& pr : page.items) {
                c.check(!pr.id.empty(), "list_open(): a listed PR has an empty id");
            }
            // Contract: has_more implies a non-empty cursor to page with. A
            // provider that pages differently must still surface a cursor here.
            if (page.has_more) {
                c.check(!page.next_cursor.empty(),
                        "list_open(): has_more is true but next_cursor is empty");
            }
        }
    }

    // --- get(sample): echoes the requested id ----------------------------
    {
        Result<PullRequest> res = provider.get(sample_pr_id);
        check_error_normalized(c, res, "get");
        if (res) {
            const PullRequest& pr = res.value();
            c.check(pr.id == sample_pr_id,
                    "get(): returned id '" + pr.id + "' != expected '" + sample_pr_id + "'");
        }
    }

    // --- refs(sample): non-empty head/base SHAs --------------------------
    {
        Result<PrRefs> res = provider.refs(sample_pr_id);
        check_error_normalized(c, res, "refs");
        if (res) {
            const PrRefs& refs = res.value();
            c.check(!refs.head_sha.empty(), "refs(): head_sha is empty");
            c.check(!refs.base_sha.empty(), "refs(): base_sha is empty");
        }
    }

    // --- files(sample): non-empty paths, non-negative counts -------------
    {
        Result<std::vector<PrFile>> res = provider.files(sample_pr_id);
        check_error_normalized(c, res, "files");
        if (res) {
            for (const PrFile& f : res.value()) {
                c.check(!f.path.empty(), "files(): a PrFile has an empty path");
                c.check(f.additions >= 0, "files(): PrFile '" + f.path + "' has negative additions");
                c.check(f.deletions >= 0, "files(): PrFile '" + f.path + "' has negative deletions");
            }
        }
    }

    // --- threads(sample): well-anchored inline threads -------------------
    {
        Result<std::vector<ReviewThread>> res = provider.threads(sample_pr_id);
        check_error_normalized(c, res, "threads");
        if (res) {
            for (const ReviewThread& t : res.value()) {
                const RangeAnchor& a = t.anchor;
                c.check(valid_side(a.side), "threads(): thread '" + t.id + "' has an invalid anchor side");
                // General (PR-level) threads have no file anchor: an empty
                // new_path marks them, and their line range is not meaningful.
                // Inline threads must carry a 1-based start line and an ordered
                // [start, end] range.
                const bool inline_thread = !a.new_path.empty();
                if (inline_thread) {
                    c.check(a.start.line >= 1,
                            "threads(): inline thread '" + t.id + "' has start.line < 1");
                    if (a.end.line != 0) {
                        c.check(a.end.line >= a.start.line,
                                "threads(): inline thread '" + t.id + "' has end.line < start.line");
                    }
                }
            }
        }
    }

    // --- capability-gated writes: Unsupported when the flag is off -------
    if (!caps.request_changes_state) {
        Result<void> res = provider.request_changes(sample_pr_id);
        check_error_normalized(c, res, "request_changes");
        c.check(!res, "request_changes(): must fail when request_changes_state == false");
        if (!res) {
            c.check(res.error().kind == ErrorKind::Unsupported,
                    "request_changes(): capability off must return ErrorKind::Unsupported");
        }
    }
    if (!caps.pending_review_batch) {
        Review empty_review;
        Result<void> res = provider.submit_review(sample_pr_id, empty_review);
        check_error_normalized(c, res, "submit_review");
        c.check(!res, "submit_review(): must fail when pending_review_batch == false");
        if (!res) {
            c.check(res.error().kind == ErrorKind::Unsupported,
                    "submit_review(): capability off must return ErrorKind::Unsupported");
        }
    }

    return c.report;
}

}  // namespace diffy::review
