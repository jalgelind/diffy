#pragma once

// diffy_review — the backend-agnostic pull-request review layer.
//
// This library is deliberately Slint-free (like repo_model): it does HTTP + JSON
// and maps hosted PRs onto a neutral domain model, so it can be unit-tested by
// diffy-gui-logic-test and reused by a future CLI. See REVIEW-ROADMAP.md for the
// full design. This header is scaffolding — the neutral model, provider
// interface, HttpClient, SecretStore and the concrete providers arrive in the
// later P0/P0.5 tasks.

#include <string>

namespace diffy::review {

// Identifies the review layer and the JSON version it was built against. It
// exists so the freshly-scaffolded static lib carries a real symbol and to prove
// the vendored nlohmann/json dependency compiles and links end-to-end.
std::string
library_version();

}  // namespace diffy::review
