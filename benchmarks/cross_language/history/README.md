# Historical cross-language observations

`2026-11-05-results.json` preserves the previous published dataset byte-for-byte.
It refers to its original source commit, frozen workload and captured binaries, not the current compiler.
Source fingerprints were independently checked against the earlier public `282d69059297f602156381547169d6cbd991a59f` tree: all 186 match.
The original source commit `8ee21c0969247a92017d0fa27af9f84bc737df13` is also publicly accessible.

Current observations are in `../results.json`; reconstruct either dataset using the public coordinator `summarize(protocol, samples)` function.
Do not use separate campaigns as paired speedup measurements. No GitHub CI or workflow is added.
