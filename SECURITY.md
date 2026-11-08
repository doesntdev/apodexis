# Security policy

Apodexis is experimental. It has checked language operations, bounded native
execution, Linux worker restrictions and explicit brokered host capabilities,
but has not been independently certified as a secure sandbox. Do not use it as
the sole boundary for hostile production workloads.

## Scope

Treat source, JSON input, inline module bundles and MCP requests as untrusted.
The compiler, runtime, trusted broker, QBE, distribution files and operating system
remain trusted components. A defect in one of those components can invalidate a
language-level safety assumption.

The default worker has no ambient file/network/process access. Operators can
explicitly grant supported effects through CLI `run --capabilities` or fixed
per-tool `serve --manifest` policies; those grants
are authority, and the observations/effects are not deterministic. Verification,
source tests and normal MCP calls do not self-grant access. See
[capability policies](docs/capabilities.md) and
[backend boundaries](docs/qbe-native-backend.md#boundaries). Serving callers
cannot submit source, policies, roots or limits. HTTP credential files are
privately snapshotted outside granted roots, bound to explicit origin/path/method
grants and never inherited by the native worker. Rotation requires an explicit
server restart; trusted endpoints can return their own received secrets, so this
is not universal response redaction. Cancellation/errors do not undo completed
external effects. Application-key grants/operations remain denied in serving
mode. See [tool serving](docs/tool-serving.md).

A canonical hash, a passing source test or a successful compiler invocation is not
a formal proof of correctness, absence of implementation vulnerabilities or
permission to widen a host grant.

## Cryptographic boundaries

Optional HTTPS and application-crypto helpers are trusted components. V3 required
HTTPS fixes TLS1.3/X25519MLKEM768/TLS_AES_256_GCM_SHA384, with ordinary certificate
and host verification and no downgrade; certificate signatures are not PQ.
Application encryption uses a deliberately narrow standard CMS profile through
OpenSSL 4.1+ high-level APIs, not handwritten cryptographic primitives. This
integration has not received an independent cryptographic audit or FIPS validation.

Recipient trust is the operator-selected SPKI snapshot. Content authentication
is not sender identity, signatures, replay protection or forward secrecy. Private
PKCS8 files are unencrypted at rest: protect storage/backups, retain strict file
modes and keep keys outside granted file roots. Never put raw keys in source,
policy JSON, prompts or logs. Key generation is operator CLI only. Keys are
snapshotted per execution, not continuously revoked or rediscovered. See the
[post-quantum guide](docs/post-quantum.md) for policy and dependency boundaries.

## Reporting a sensitive issue

Do not publish credentials, private datasets, exploit payloads or sensitive host
details in a public issue.

If this repository has GitHub private vulnerability reporting enabled, use its
**Security → Report a vulnerability** route. This file does not claim that the
hosting feature has already been enabled. If no private route is available,
open only a minimal issue requesting a private security contact; omit exploit
and sensitive details until a private channel is agreed.

For a private report, include the revision/platform, affected interface, a minimal
synthetic reproduction, observed outcome and expected boundary. Distinguish a
suspected source defect from an actually reproduced failure or deployed exposure.

No support SLA, supported-version maintenance window or production deployment
assurance is implied. Retain evidence, avoid probing unrelated systems and do not
rotate or disclose someone else's credentials as part of a public report.
