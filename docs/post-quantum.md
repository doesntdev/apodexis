# Post-quantum encryption

Apodexis has two separate, explicitly authorized surfaces:

| Surface | Fixed supported profile | Activation |
| --- | --- | --- |
| HTTPS key establishment | TLS 1.3, X25519MLKEM768 hybrid group, TLS_AES_256_GCM_SHA384; verified chain and DNS/IP identity | Optional system OpenSSL TLS helper and operator v3 network grants. V3 defaults to `pq_required`. |
| Application encryption | Standard DER CMS AuthEnvelopedData: ML-KEM-768, HKDF-SHA256, AES256-WRAP and AES256-GCM | Optional static OpenSSL crypto helper and named operator v3 key grants for `pqc_seal`/`pqc_open`. |

Neither surface is enabled by importing a module. Default execution, tests,
verification, MCP run/invoke and standalone generated programs have no grants.
The native compiler and pure generated worker remain libc-only. No curl/libcurl,
Python, Rust or cryptographic provider service is needed at runtime.

This is an experimental integration of established library cryptography, not an
independent cryptographic audit or FIPS 140 validation. ML-KEM-768 is a NIST
category-3 KEM; the AES256 choice does not turn the whole construction into a
uniform 256-bit public-key security claim.

## Required hybrid HTTPS

See [HTTP client](http-client.md) for complete network grants and response shapes.
With `apodexis.host_policy.v3`, omitted `tls_security` means `pq_required`:
plaintext grants are refused at policy load; only the fixed TLS 1.3/group/cipher
combination is accepted. Chain and host verification, DNS SNI and HTTP/1.1 ALPN
remain mandatory. Negotiated parameters are checked before HTTP request bytes.
A classical-only peer, missing helper or unavailable required algorithm fails;
there is no automatic downgrade, redirect, retry or replay.

Explicit `tls_security: "standard"` opts into ordinary HTTP and TLS 1.2..1.3.
It is compatibility mode, not a guarantee of PQ key establishment. V1/v2 policies
retain their earlier behavior and reject the new fields. The qualified system
TLS library is OpenSSL 3.5.5; compiling/executing other versions is a separate
compatibility question. An older library without the group must refuse required
PQ rather than silently select a classical group.

Hybrid key establishment targets confidentiality, including harvest-now,
decrypt-later risk. **Certificate authentication remains ordinary WebPKI**:
this is not post-quantum certificate signatures or a claim of PQ-authenticated
TLS against an active attacker able to forge those classical signatures.
Application encryption is separate from transport encryption.

## Build the optional application helper

Default `make`, `make check`, `make test` and `make dist` require no OpenSSL.
The optional application backend uses OpenSSL **4.1+ public CMS/KEM APIs**, not
the older system TLS helper. The reference backend is the checksum/signature-
verified OpenSSL 4.1.0 source pinned in
[`third_party/openssl-pqc-provenance.json`](../third_party/openssl-pqc-provenance.json).
Nothing is downloaded, installed globally or upgraded by the Makefile.

Obtain and authenticate the upstream archive and detached signature using the
[OpenSSL release instructions](https://openssl-library.org/source/). Compare the
archive with the pinned SHA256, and independently establish trust in its signing
key. A downloaded checksum alone does not authenticate the publisher.

If building that dependency yourself, OpenSSL additionally needs its own source
prerequisites, including Perl and a C toolchain. From the extracted, verified
OpenSSL 4.1.0 source, use a **new absolute user-owned prefix**, not `/usr`:

```sh
perl ./Configure linux-x86_64 no-shared no-module no-dso no-tests no-legacy --prefix=/absolute/path/to/openssl-pqc-prefix --openssldir=/absolute/path/to/openssl-pqc-prefix/ssl
make -j2
make install_sw
```

The reference configuration disables dynamic modules/configuration dependencies.
The `no-tests` option here describes the recorded dependency build; Apodexis's
real crypto/interop/negative tests below are separate and do not claim to be the
complete upstream OpenSSL test suite. Preserve upstream licenses when distributing
its code. Apodexis does not modify the upstream source.

From the Apodexis checkout, point explicitly at the completed static SDK:

```sh
make pqc PQC_OPENSSL_PREFIX=/absolute/path/to/openssl-pqc-prefix
make test-pqc PQC_OPENSSL_PREFIX=/absolute/path/to/openssl-pqc-prefix
make test-pqc-sanitize PQC_OPENSSL_PREFIX=/absolute/path/to/openssl-pqc-prefix
```

The output is `.build/tools/apx-pqc`. Its static libcrypto/default provider needs
no SDK prefix or provider DSO at runtime. The compiler and pure worker do not link
it. `capabilities` reports `post_quantum.application_helper_available`, meaning
an installed executable, not successful key loading or peer negotiation.
The sanitizer target instruments Apodexis's driver/broker/helper/profile code;
the reused static OpenSSL archive itself is not sanitizer-instrumented. Only the
test helper omits its address-space limit for ASan shadow memory.

For an explicit bundle containing both network helpers and the crypto helper:

```sh
make dist-pqc PQC_OPENSSL_PREFIX=/absolute/path/to/openssl-pqc-prefix
```

This produces `.build/apodexis-linux-amd64-pqc.tar.gz`, including `PQC-PROFILE`,
`HTTP-PROFILE`, the full OpenSSL license and provenance. HTTPS still separately
needs compatible system libssl.so.3/libcrypto.so.3 and trust roots. Default `dist`
never includes stale optional helpers just because they exist in the build tree.
See [binary installation](binary-distribution.md).

## Operator-owned keys

The operator, not an authored program or MCP caller, generates/provisions keys:

```sh
bin/apodexis pqc-keygen /absolute/path/to/NEW-key-directory --json
```

The directory must not exist. The operation creates it as `0700`, `public.pem`
as SPKI PEM (`0644`) and `private.pem` as unencrypted PKCS8 PEM (`0600`), using
the library's secure RNG. It refuses existing files/directories, symlinks and dot
path components. No private bytes are printed. Success has schema
`apodexis.pqc_keygen.v1`, status `ok`, algorithm `ML-KEM-768`, the two file paths
and `key_id_sha256`, the lowercase SHA256 of canonical SPKI DER.

An error is a versioned, payload-free `pqc_keygen_failed` or `pqc_unavailable`.
Inspect its status and exit code. Normal failure cleanup removes only this
operation's owned new files; interruption can leave protected, unadvertised
partial files. Inspect those exact paths rather than blindly repeating or
recursively deleting a directory. Key generation is not a language builtin or
MCP tool, and it does not alter host OpenSSL, certificate trust or provider config.

Private files are **not encrypted at rest**. Keep them in a trusted private
location with suitable storage/backups; do not paste their contents into source,
policy JSON, prompts, logs or chat. Exported passphrase/encrypted-key handling,
rotation and revocation are not implemented as language operations.

## Give each direction only the authority it needs

This is a policy template: replace the absolute placeholders with actual reviewed
paths. The key directory must be outside any granted read/write root.

```json
{
  "schema": "apodexis.host_policy.v3",
  "crypto_keys": [
    {
      "name": "recipient",
      "public_key_file": "/absolute/path/to/key-directory/public.pem",
      "private_key_file": "/absolute/path/to/key-directory/private.pem"
    }
  ],
  "crypto_timeout_ms": 5000,
  "max_bytes": 1048576,
  "max_requests": 16
}
```

`public_key_file` grants seal, `private_key_file` grants open. Either can be
omitted; at least one must be present. A private-only slot does not implicitly
grant seal. Up to 16 unique, case-sensitive names are accepted, each 1..64 ASCII
letters/digits/underscore/hyphen. Unknown, duplicate, mistyped or extra fields
fail closed. V1/v2 reject crypto fields. These are operator paths, never paths
chosen by the authored request.

At policy load the broker checks component-wise no-symlink/no-dot paths, regular
single-link files, size, ownership and modes; validates ML-KEM-768 keys; then
stores bounded immutable **sealed memfd snapshots**. Public input is at most
16384 bytes, operator/root-owned and not group/world-writable; private input is
at most 32768 bytes, current-operator-owned and `0400` or `0600`. Only one
appropriate unencrypted PEM block is accepted, without extra blocks/NUL bytes.
A private source beneath either file root is refused by directory-object ancestry,
not a string-prefix comparison. Existing file-broker hardlink/type checks remain.

Later operator file replacement does not change an already loaded execution's
snapshots. Load a new policy/execution to use a changed key; this is not continuous
revocation monitoring. The guest and unrelated helpers inherit no key fds. Only
the selected crypto helper receives one sealed key fd plus its private channel.
Private key bytes never cross into language values or JSON/argv/environment.
Internal cryptographic randomness does not grant the separate `random_hex` API.

## Typed language calls

The ordinary sibling module [`pqc.apx`](../examples/pqc/pqc.apx) exports
`Body`, `Envelope`, `text`, `body_text`, `seal` and `open`:

| Function | Signature |
| --- | --- |
| `text` | str -> Body (UTF8) |
| `body_text` | Body -> result str str; base64 returns `pqc_non_utf8` |
| `seal` | str slot, Body -> result Envelope str |
| `open` | str slot, Envelope -> result Body str |

[`main.apx`](../examples/pqc/main.apx) accepts `{key,body}` and seals it. Save a
normal typed input, for example `{"key":"recipient","body":{"encoding":"utf8","data":"hello"}}`,
then run the shipped entrypoint with the reviewed policy:

```sh
bin/apodexis verify examples/pqc/main.apx --json
bin/apodexis run examples/pqc/main.apx --input /absolute/path/to/input.json --capabilities /absolute/path/to/policy.json --json
```

Verification tests have no key grants. The actual run returns an ordinary tagged
result inside the native execution envelope; inspect both levels. Without a
policy the application result is `capability_denied`, even if execution exits zero.

For decryption, place this entrypoint beside a copy of the ordinary `pqc.apx`
module and provide input `{key,envelope}`:

```apodexis
M decrypt_document
I p "pqc"
T Input struct
  key:str
  envelope:p::Envelope
F main input:Input->result p::Body str
  R (p::open input.key input.envelope)
```

`Body` has exactly `{encoding,data}`, using `utf8` or canonical padded `base64`.
UTF8/NUL/binary/empty plaintext round-trips without guessing or dropping bytes.
`Envelope` has exactly `{format,data}`, where format is
`cms-authenveloped-mlkem768-aes256gcm-v1` and data is canonical padded base64 DER.
The format label identifies this supported subset; the encrypted bytes are
standard CMS, not a proprietary crypto envelope.

The underlying appended builtins are `(pqc_seal request_json)` (1008) and
`(pqc_open request_json)` (1009), both str -> result str str. Seal input is exactly
`{key,body}`; open input exactly `{key,envelope}`. Success is the encoded Envelope
or Body JSON document respectively. The typed module handles this serialization.
No raw key, password, algorithm/provider option, extra field or host path is
accepted from the guest.

## Profile, bounds and failures

The supported standard profile is deliberately narrow: one key-id ML-KEM-768
KEMRecipientInfo (1088-byte KEM ciphertext), 32-byte SHA256 SPKI key-id,
HKDF-SHA256 with absent parameters, a 32-byte KEK, AES256-WRAP (40-byte wrapped
CEK), AES256-GCM (12-byte nonce/16-byte tag), version zero and attached `id-data`.
Originator information, UKM, authenticated/unauthenticated attributes, detached
content, streaming BER, multiple recipients and other algorithms are rejected.
Canonical DER is checked, including the opaque GCM parameter sequence, before
library decryption. The library performs KEM, KDF, wrapping and authenticated
encryption; the ASN.1 validator does not implement those primitives.

`max_bytes` bounds plaintext/ciphertext content, at most 1048576 bytes; full DER
has a separate `max_bytes + 4096` allowance. Base64 and outer JSON add overhead;
input/output, arena and frame limits still apply independently. Every attempted
host operation consumes the shared request budget, including denial/failure.
`crypto_timeout_ms` can only reduce the default 5000 ms and is also capped by the
enclosing execution deadline. Startup, parsing, crypto, output and child reaping
are supervised; cancellation kills/reaps the owned helper without a retry.

| Error | Meaning |
| --- | --- |
| `capability_denied` | No trusted execution policy/channel. |
| `pqc_key_denied` | Slot absent or that direction not granted. |
| `invalid_pqc_request` | Outer request/Body shape or seal encoding invalid. |
| `pqc_decrypt_failed` | Authorized open failed profile/recipient/authentication validation, including malformed base64/DER and tampering; no plaintext. |
| `pqc_byte_limit` | Plaintext, ciphertext or crypto IPC byte bound exceeded. |
| `pqc_timeout` | Crypto/enclosing deadline or cancellation. |
| `pqc_unavailable` | Missing/unusable backend or invalid helper protocol response. |
| `pqc_key_unavailable` | A policy key could not be validated/loaded. |
| `host_request_limit`, `host_frame_limit` | Independent shared broker request/frame bounds. |
| `invalid_pqc_response` | Defensive typed-module fallback for an impossible malformed native success. |

Plaintext is buffered privately and returned only after complete profile checking
and CMS authentication. Wrong keys, malformed envelopes and tampered data yield
no partial plaintext or error stacks. Other terminal language traps remain
separate from ordinary result errors.

## Trust and interoperability limits

Recipient trust comes from the operator-selected SPKI snapshot. Internally the
high-level CMS API uses an **in-memory public-key/SKI X509 carrier**, which is
never serialized as an identity certificate. There is no certificate-issuance or
recipient-certificate provisioning dependency. ML-KEM is a KEM, not a signature.

AEAD provides content integrity, **not sender authentication**, signatures,
replay protection or forward secrecy. Anyone with the public key can encrypt to
it. Establish recipient-key authenticity independently; applications must add
any required sender/replay/context binding at their own protocol level.

Binary interchange is qualified in both directions with the OpenSSL 4.1 CLI and
the fixed profile, using synthetic recipient certificates only in the interop
fixture. This is same-library CLI interoperability, not independent-implementation
validation or support for every CMS document. No Bouncy Castle/GnuTLS/provider
matrix, raw sockets, streaming encryption, PQ signatures or FIPS certification
is claimed.

Standards: [FIPS 203 (ML-KEM)](https://csrc.nist.gov/pubs/fips/203/final),
[RFC 5083](https://www.rfc-editor.org/rfc/rfc5083),
[RFC 9629](https://www.rfc-editor.org/rfc/rfc9629), and
[RFC 9936](https://www.rfc-editor.org/rfc/rfc9936).
