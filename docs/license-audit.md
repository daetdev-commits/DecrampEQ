# License and provenance review

Reviewed: **2026-10-27**, source candidate **0.2.0**.

**Result: no conflicting third-party license or identifiable imported C++
implementation was found in the reviewed source distribution.** This is a
bounded dependency, notice, and provenance review, not a certificate that all
generated code is unique or that no third-party rights can exist.

## Origin recorded

The maintainer reports that the original library was newly generated in a
separate AI task. It was not supplied as a third-party repository or an existing
author's library. The original AI task transcript, prompts, and any sources
retrieved during that task were not available for this review. AI generation
alone is not treated as evidence of an absence of third-party material.

The maintainer additionally reports that a follow-up check in the original AI
task found no identifiable dependencies. This corroborating report was received
on 2026-10-27; the original task's check procedure and transcript were not
independently inspected here. It supplements the archive and source checks below
without changing their scope.

The three input ZIP versions contain no LICENSE file, third-party copyright
notice, or identified external C++ implementation. Their absence is an inventory
finding, not permission inferred from an unlicensed third-party work. The MIT
notice was added during this project's publication preparation, following the
maintainer's license choice; it was not inherited from those ZIPs.

| Input archive | SHA-256 |
| --- | --- |
| `DecrampEQ-0.1.0.zip` | `a0a1466087c5d9c959a6547c64bbd55015b65e0fa9f491e8ec12447a7a86f1df` |
| `DecrampEQ-0.1.0(1).zip` | `283b19f19845f7eb02b3cba9ad7500c5a866e4d1509178b2985f2145be1919b1` |
| `DecrampEQ-0.1.0(2).zip` | `bb3687140e8d0de9bdb487eb79dc8fdc7efd7492aa98634cf2c9c1b5e6377a43` |

The workspace's initial ZIP and the download with the same name have identical
hashes. The update memo supplied with the last ZIP was also inspected.

## Scope and findings

- Inspected every original ZIP member, text notices/comments, and current
  distributed text file. Searches covered copyright, SPDX, license/permission,
  attribution, source URLs, and common third-party license and library names.
  No GPL, LGPL, AGPL, Apache, BSD, or other external license notice was found.
  This string search cannot detect code whose original notice was omitted.
- Checked the C++ include lists and CMake dependencies. The library, examples,
  tests, and benchmark use the project header, standard C++ facilities, and a
  platform allocation header in the allocation test. CMake uses its built-in
  modules and Threads target. No JUCE, VST SDK, external DSP or numerical library,
  copied standard-library implementation, compiler runtime, or binary is bundled.
- The GitHub Actions workflow invokes `actions/checkout@v7` as an external CI
  tool. Its implementation is not part of the source archive. Compiler, CMake,
  CI-action, and standard-library licenses remain with their respective tools;
  the project's MIT notice does not relicense them.
- The only distributed image, `docs/corrected_curves.png`, is byte-identical to
  the comparison plot in the last input ZIP. It contains plotted responses,
  labels, and a legend. Its PNG metadata identifies Matplotlib 3.10.8. No font
  file, Matplotlib implementation, or other image asset is bundled. Its origin
  relies on the same maintainer account as the original library.
- The original header identifies the RBJ Audio EQ Cookbook as its mathematical
  reference and retains an earlier standalone namespace for compatibility.
  No separate third-party origin was identified for that namespace. The
  cookbook reference, Butterworth placement, and solver methods are documented
  in [THIRD_PARTY.md](../THIRD_PARTY.md). The referenced W3C page contains
  equations and explanations, not a C++ implementation imported by this project.
- Queried the available GitHub code search for `analog_eq_2sos`,
  `hpNyquistGain pairFromPolynomial`, and `DecrampEQ.hpp`. Queried Web search for
  those quoted identifiers and the original comment "Cholesky with positive
  diagonal from Levenberg regularization". No relevant exact match was returned.
  Search indexing, access, and exact wording limit this check; it is not an
  exhaustive clone-detection scan.
- Verified that the source ZIP contains exactly the library source tree, with
  matching per-file hashes. The ZIP and per-file SHA-256 manifests are delivered
  beside each other. Packaging excludes Valence, build outputs, downloaded
  toolchains, and temporary probes.

Reviewed public-header SHA-256:

```text
5b798f3f95a5b30ec926a314ea80cec68486be1cea3c2243b3d891f025263ee0
```

## License scope and future changes

The project is offered under [MIT](../LICENSE). The full notice is also in the
header so it travels with single-file use. Mathematical reference documents keep
their own terms: the [W3C Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/)
identifies Robert Bristow-Johnson and the
[W3C Software and Document License (2015)](https://www.w3.org/copyright/software-license-2015/).
The reference is not a claim that the W3C document is MIT-licensed.

If an identifiable copied implementation, excerpt, or asset is later found,
check its actual source and terms, preserve required notices, and update the
distribution before claiming it is covered solely by the project license. New
contributions must record external sources and preserve their applicable terms,
as described in [CONTRIBUTING.md](../CONTRIBUTING.md).

The checks above do not establish a complete chain of authorship for AI output
or an exhaustive match against all public and private code. Publication notes
must preserve that distinction from the completed engineering tests.
