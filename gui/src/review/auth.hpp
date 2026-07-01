#pragma once

// diffy_review — the authentication abstraction.
//
// WHY: every backend accepts credentials, but the *shape* of a credential differs
// (Bitbucket Cloud uses a username + App-Password/API-token over HTTP Basic;
// GitHub/GitLab take a PAT or OAuth token as a Bearer; some support the OAuth
// device flow). The UI's "Connect" card must render one code path regardless of
// backend, so it needs a neutral vocabulary: what methods a provider accepts
// (AuthDescriptor), a generic credential to hand back (Credential), and — for the
// device flow — plain data types the card can display and poll on. The actual
// network calls (begin/poll device auth) live in the concrete provider that
// supports them; this header is deliberately just data + enums so it can be
// included from model-free code and unit tests. See REVIEW-ROADMAP.md §7 (Auth).
//
// Secrets carried here (Credential::secret, DeviceAuthStart::device_code) are
// SENSITIVE: never log them, never put them in error messages, never persist them
// in plaintext config — they belong in the SecretStore (OS credential vault).

#include <string>
#include <vector>

namespace diffy::review {

// How a credential authenticates against a backend.
//   BasicToken  — principal + secret sent via HTTP Basic. Bitbucket Cloud: a
//                 scoped API token with the Atlassian account email as principal
//                 (the preferred method; App passwords with a username are
//                 deprecated but use the same Basic mechanism).
//   Bearer      — a PAT or OAuth access token sent as `Authorization: Bearer …`
//                 (GitHub/GitLab). principal is usually empty; secret = the token.
//   OAuthDevice — obtained through the OAuth device flow; the resulting access
//                 token is then used as a Bearer. Advertised by providers that
//                 implement begin/poll device auth.
enum class AuthMethod { BasicToken, Bearer, OAuthDevice };

// A generic credential handed to a provider's `make`. `secret` is SENSITIVE (an
// App-Password, PAT, or OAuth access token) and must never be logged or written
// to plaintext config; fetch it from / store it in the SecretStore. `principal`
// is the username for BasicToken and may be empty for Bearer/OAuthDevice.
struct Credential {
    AuthMethod method = AuthMethod::Bearer;
    std::string principal;  // username; may be empty for Bearer
    std::string secret;     // SENSITIVE — never log
};

// A single OAuth scope by its provider-native name (e.g. "pullrequest",
// "repo"). Kept as a struct rather than a bare string so the descriptor can grow
// per-scope metadata later without churning call sites.
struct AuthScope {
    std::string name;
};

// What a provider advertises to the Connect card: which methods it accepts, the
// scopes it needs (read + write pull requests), and a short help string the card
// renders to guide the user (e.g. where to create an App Password/PAT).
struct AuthDescriptor {
    std::vector<AuthMethod> methods;
    std::vector<std::string> scopes;
    std::string help_text;  // shown on the Connect card
};

// --- OAuth device flow: plain data ----------------------------------------
//
// Used only by providers that support AuthMethod::OAuthDevice. The provider's
// begin-device-auth call returns a DeviceAuthStart; the Connect card shows
// verification_uri + user_code and polls every `interval_secs` (respecting
// SlowDown) until `expires_in_secs` elapses. device_code is SENSITIVE.

struct DeviceAuthStart {
    std::string verification_uri;
    std::string user_code;
    std::string device_code;  // SENSITIVE — never log
    int interval_secs = 5;
    int expires_in_secs = 900;
};

// The outcome of one device-flow poll:
//   Pending  — user has not acted yet; keep polling at the current interval.
//   SlowDown — polling too fast; increase the interval before retrying.
//   Approved — the user authorized; the access token is now available.
//   Denied   — the user (or server) refused authorization.
//   Expired  — device_code/user_code lifetime elapsed; must restart the flow.
enum class DeviceAuthStatus { Pending, SlowDown, Approved, Denied, Expired };

}  // namespace diffy::review
