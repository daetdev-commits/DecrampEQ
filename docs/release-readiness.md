# Initial publication readiness

Version: **0.2.0**, initial public release.

The cutoff for this development pass is a usable, inspectable initial OSS
release: numerical behavior, realtime ownership, build integration, documentation,
and a license inventory/provenance review have explicit checks. The latter is
limited to the evidence in [the license review](license-audit.md), including the
maintainer's account of AI-generated origins; it is not exhaustive third-party
code clearance. It is a pre-1.0 API, with the approximation limits described in
the README and validation notes.

## Completed local gates

| Gate | Evidence |
| --- | --- |
| Numerical/audio regressions | Independent analog references, processed tones, pole checks, fractional boundaries, and malformed-input tests pass. |
| Parameter coverage | 1,152 stress combinations and 72 required 20 Hz boundary combinations are exercised; unavailable extreme combinations are explicitly allowed by the stress test. |
| Update/state behavior | Reset, disconnected-channel history, repeated targets, pending-update ordering, and crossfade tests pass. |
| SPSC handoff | 20,000 designs cross real producer/consumer threads in order without torn snapshots. |
| Realtime allocation check | Global regular/aligned C++ allocation interception reports zero allocations during coefficient design and the tested queue/update/process/reset paths. |
| MSVC | Windows x64, MSVC 19.44, Release, `/W4 /WX`; correctness, examples, and consumer tests pass. |
| Independent compiler/library | Windows x64 Clang 18.1.6 with libc++ via the verified official Zig 0.13.0 distribution; `-Wall -Wextra -Wpedantic -Werror`; the same tests pass. |
| Memory instrumentation | MSVC AddressSanitizer Release build passes the correctness, allocation, and consumer tests without findings. |
| Installation | Exported C++17 target, config/version files, relocated prefix with spaces, and two-translation-unit `find_package()` / vendored consumers pass. |
| License inventory and reported provenance | Original ZIPs, current source notices/includes, image origin, archive scope, and limited public-code searches reviewed; no conflict found. Maintainer reports new work from a separate AI task and a follow-up check there finding no identifiable dependencies; its transcript was not independently inspected. See [evidence and limits](license-audit.md). MIT notice is in LICENSE and the single header. |
| Reproducibility | Examples, regression tests, an optional benchmark, CI configuration, and recorded commands are included. |

The Clang toolchain's bundled runtime build can emit its own upstream warnings;
the project's C++ sources compile with warnings treated as errors.

## Completed hosted checks

All five jobs in the [initial hosted CI run](https://github.com/daetdev-commits/DecrampEQ/actions/runs/40466354568)
passed on source commit `2693f7d0e7bae9c044770f9a67cdf5cbc6325f97`:
Linux GCC, Linux Clang, macOS AppleClang, Windows MSVC, and Linux ASan+UBSan.
The four compiler jobs include the examples and relocated package consumers;
the sanitizer job includes the correctness and consumer checks.

The [public repository](https://github.com/daetdev-commits/DecrampEQ) contains
the library folder as its root. Subsequent validation is recorded in its
[CI runs](https://github.com/daetdev-commits/DecrampEQ/actions/workflows/ci.yml).

## Publishing a version

Release publication is opt-in. After updating the CMake version, changelog,
and `docs/releases/<version>.md`, commit to main with the exact message
`Release DecrampEQ <version>`. Only that main-branch push can run the release
job, and it depends on all compiler and sanitizer jobs passing. Pull requests
and ordinary pushes run checks without publishing a release. The job archives
the tested commit and attaches its source ZIP, archive checksum, and per-file
SHA-256 manifest to the versioned GitHub Release.

## Material limits carried into the release

- `maxErrorDb` is a sampled estimate, not a formal bound over all frequencies.
  Some extreme Q/fractional settings exceed 0.5 dB error, and some requested
  combinations are unavailable.
- Magnitude fitting does not match the analog prototype's phase.
- Crossfading reconstructs from a finite 2,048-sample history; it cannot promise
  an exactly reconstructed state for every long low-frequency/high-Q tail.
- Callers own realtime thread discipline, queue-full retry, buffer validity,
  finite audio input, and gain/headroom policy.
- Ordinary IEEE floating-point semantics are required; fast-math builds are
  unsupported.
- The public API is pre-1.0. Compiler timings are observations, not guarantees.

## Final source archive check

The distribution is created from this folder alone. It includes dotfiles,
license, documentation, examples, tests, benchmark, and CMake configuration;
it excludes local build products, downloaded compilers, plug-in binaries, and
temporary probes. The archive is then extracted into a fresh path and built
with its examples and tests enabled.
