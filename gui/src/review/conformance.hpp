#pragma once

// diffy_review — the shared provider conformance battery.
//
// Per REVIEW-ROADMAP.md §2/§10 the seam only holds if two independent backends
// (Bitbucket first, GitHub as the abstraction proof) map onto the SAME neutral
// model and honour the SAME capability contract. This file is the reusable set
// of invariant checks every ReviewProvider must satisfy — the checks that must
// hold regardless of backend, expressed against the neutral model / Result /
// Capabilities only.
//
// It runs with no live network: a concrete provider test (#27/#28) constructs
// its provider over a MockHttpClient wired with recorded JSON fixtures, then
// calls run_conformance() and asserts the returned report is ok(). Violations
// are collected as human-readable strings rather than thrown, so a single run
// surfaces every failing invariant at once. This header is dependency-free
// beyond the sibling review headers + std.

#include "review_provider.hpp"

#include <string>
#include <vector>

namespace diffy::review {

// The outcome of a conformance run: a pass/fail tally plus one message per
// violated invariant (empty when ok()). Kept plain so callers can print the
// failures or feed them straight into a doctest CHECK.
struct ConformanceReport {
    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;

    bool
    ok() const {
        return failed == 0;
    }
};

// Run every backend-agnostic invariant against `provider`, using `sample_pr_id`
// as the id fed to the single-item reads (get/refs/files/commits/threads). Each
// violated invariant records a failure string; the function never throws. See
// conformance.cc for the exact contract each check enforces.
ConformanceReport run_conformance(ReviewProvider& provider, const std::string& sample_pr_id);

}  // namespace diffy::review
