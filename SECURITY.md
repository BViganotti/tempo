# Security policy

Tempo is a local developer tool. It binds only `127.0.0.1`, requires a per-session
token for the UI and API, validates Host/Origin, sends a restrictive CSP, and
contains no network-runtime dependencies. Scenario code is native code compiled
by the embedding application; Tempo does not sandbox it.

The full threat model, file sandbox, parser limits and non-goals are in
[docs/SECURITY.md](docs/SECURITY.md). Limitations are in
[LIMITATIONS.md](LIMITATIONS.md).

## Reporting a vulnerability

Please use GitHub's private security advisory feature for
[BViganotti/tempo](https://github.com/BViganotti/tempo/security/advisories/new)
rather than opening a public issue with exploit details. Include:

- the affected commit or version,
- a minimal reproduction (request, record or scenario),
- the expected and observed behavior.

There is no guaranteed response time for this hobby-scale project, but reports
that demonstrate a concrete issue (token bypass, sandbox escape from the file
capability, denial of service through malformed input, or memory unsafety in a
parser) will be investigated and documented in `CHANGELOG.md` when fixed.

## Supported versions

Only the current `master` branch is supported. There are no releases or LTS
branches yet; records carry a format version and the tool version that wrote
them.

## Scope

In scope: the workbench HTTP server, token handling, JSON/record parsers, the
file/env/socket capability sandbox, and the debugger/replay logic under its
documented cooperative model.

Out of scope for this policy: native code in a scenario doing anything it wants,
data already readable by the local user, the lack of TLS on loopback, and
screenshots or records the user chooses to share.
