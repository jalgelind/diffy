#pragma once

// diffy_review — a tiny append-only diagnostic log.
//
// The review layer's first real network calls happen far from the test suite
// (live HTTP + auth), so failures need a durable trace. log_line() appends a
// timestamped line to a file at log_path() (under the OS temp dir). It is
// thread-safe (the HTTP calls run on worker threads). SECRETS MUST NEVER BE
// LOGGED — callers log URLs, methods, statuses and server error text only, never
// Authorization headers, tokens, or request bodies that may carry them.

#include <string>

namespace diffy::review {

// Append one timestamped line to the diagnostic log (best-effort; never throws).
void
log_line(const std::string& msg);

// Absolute path of the log file (…/diffy-review.log in the OS temp dir).
std::string
log_path();

}  // namespace diffy::review
