# Third-party notices

Apodexis first-party source and documentation are licensed under the
[MIT License](LICENSE), except where a file carries its own notice. Vendored
components retain their original copyright and license terms.

## QBE 1.3

- Project: [QBE compiler backend](https://c9x.me/compile/).
- Vendored source: `third_party/qbe/`.
- License: MIT; retain the [complete upstream notice](third_party/qbe/LICENSE).
- Copyright: © 2015–2026 Quentin Carbonneaux.
- Pinned release archive and SHA-256: [QBE provenance](third_party/qbe-provenance.json).

The native source build uses the vendored QBE source without downloading it.
The binary distribution includes the QBE executable, its upstream license at
both `QBE-LICENSE` and `third_party/qbe/LICENSE`, and its provenance record.
The Apodexis license does not replace or remove upstream attribution.

## Optional application-crypto backend: OpenSSL 4.1.0

The explicit `pqc` build statically links libcrypto and its default provider from
an operator-supplied, authenticated SDK. The reference upstream source, release
signature, SHA256, signing fingerprint and build configuration are recorded in
[OpenSSL provenance](third_party/openssl-pqc-provenance.json). Upstream source is
unmodified and is not vendored or auto-downloaded by the Apodexis Makefile.

OpenSSL is licensed under Apache License 2.0, Copyright The OpenSSL Project
Authors. Retain the [complete upstream license](third_party/OPENSSL-LICENSE).
The explicit `-pqc` bundle contains this full license as both `OPENSSL-LICENSE`
and `third_party/OPENSSL-LICENSE`, plus its provenance and `PQC-PROFILE`. This
notice and the Apodexis MIT license do not replace upstream terms.

Only the optional crypto helper embeds this backend; the default compiler and
pure generated worker do not link it. See the [PQ guide](docs/post-quantum.md).

## Platform tools and optional integrations

The host C library, kernel, assembler, linker and source-bootstrap C compiler
are supplied by the operating system, not vendored into this repository.
The native HTTP helper uses operating-system libc/sockets, with no curl or libcurl.
The optional HTTPS helper dynamically links system OpenSSL (tested with 3.5.5),
not vendored cryptography. OpenSSL is distributed by its upstream authors under
the Apache License 2.0: https://www.openssl.org/source/license.html and
https://github.com/openssl/openssl . Apodexis's MIT license does not replace those
terms. Default builds, distributions and pure generated programs do not link
OpenSSL; the explicit TLS profile requires compatible system libssl.so.3 and
libcrypto.so.3 and does not bundle those libraries.
