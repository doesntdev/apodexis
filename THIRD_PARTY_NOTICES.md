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

## Platform tools and optional integrations

The host C library, kernel, assembler, linker and source-bootstrap C compiler
are supplied by the operating system, not vendored into this repository.
The optional HTTP GET capability uses the operator's installed `/usr/bin/curl`;
it is not needed to build Apodexis or run pure programs and is not bundled.
