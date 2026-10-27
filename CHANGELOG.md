# Changelog

## 0.2.0 (initial public-release candidate)

- Add the MIT license to the project and the standalone header, with mathematical references and dependency attribution.
- Record original archive hashes, maintainer-reported AI origins, source/dependency inventory, limited similarity searches, and the limits of the license review.
- Complete relocatable CMake package/config/version installation and test both `find_package()` and `add_subdirectory()` from two translation units.
- Add strict warning builds, optional sanitizer instrumentation, and a C++ allocation test covering design, queue handoff, updates, processing, and reset.
- Add a complete producer retry/audio-consumer example and run both examples as tests.
- Provide English and Japanese READMEs, public API/threading contracts, contribution guidance, and publication readiness notes.
- Add a consistent C++ formatting configuration and reject a queue capacity that would overflow its sentinel slot.
- Configure GCC/Clang/Linux, AppleClang/macOS, MSVC/Windows, and ASan+UBSan GitHub Actions jobs. Remote jobs run after publication; local MSVC, Clang, and MSVC ASan checks are recorded separately.

## 0.1.4

- Cache notch/band-pass frequency terms and coefficient models across each fitting step, including the finite-difference models. Local measurements show about 2.5x / 2.2x faster design for the benchmark notch / band-pass cases.
- Skip duplicate `SmoothProcessor::set()` targets when active section counts and coefficients match. Accuracy metadata and inactive slots do not trigger a transition.
- Avoid truncated-history replay for unchanged coefficients, fixing a settled low-frequency output disturbance under repeated updates.
- Let a newer request at a fade's block boundary supersede an older pending design, preventing a later reversion.
- Add a standalone opt-in timing benchmark and regressions for repeated identical targets and block-boundary update ordering.

## 0.1.3

- Separate public frequency limits from internal prototype frequencies. A 20 Hz cut can now use a sub-20 Hz natural frequency when resonance normalization requires it; public and compatibility APIs still reject requested frequencies below 20 Hz.
- Cancel the common analog/digital notch zero analytically when evaluating fit error, including its finite limit at the exact notch center. Narrow fit intervals no longer skip the entire error evaluation.
- Add logarithmic, focus, center, and complex-root checks to notch/band-pass error measurements.
- Compute response magnitudes as a sum of scaled section dB values, avoiding intermediate cascade underflow/overflow and numerator overflow.
- Compute normalized angular frequency before multiplying by two pi to avoid overflowing large finite response-helper inputs.
- Add 72 low-boundary design checks, notch-shoulder references, public input-limit regressions, and extreme-response utility tests.

## 0.1.2

- Improve fractional low-pass starting coefficients, including low-cutoff and resonant cases. A 192 kHz / 20 Hz / Q=0.3 / 42.5 dB/oct regression improves from about 4.81 dB to 0.054 dB sampled error and no longer has a subsonic boost.
- Add a high-pass starting point that approaches unity as the fractional order tends to zero, avoiding misplaced notches close to integer slopes.
- Compare at most two bounded local fits and retain the candidate with the lower sampled maximum error and valid coefficients.
- Check mixed linear/logarithmic points, prototype focus points, and complex pole/zero frequencies when measuring cut-filter error. Low-pass checks include the sub-20 Hz region.
- Add analog-reference fixtures, an actual processed 20 Hz tone, subsonic-peak checks, and integer-slope boundary regressions at four sample rates.

## 0.1.1

- Add `decramp::isValid()` to check coefficient counts, finite values, and pole stability with a double-precision margin.
- Reject malformed designs consistently in the processors, update queue, and response helper without replacing active coefficients.
- Reject non-finite fit errors rather than allowing NaNs to disappear in a maximum-error calculation.
- Keep separate smoothing input histories for channels omitted with a null pointer.
- Make `SmoothProcessor::reset()` retain the newest accepted target, including a pending change.
- Add malformed-input, reset, channel-history, threaded queue, and parameter-grid regression tests.
- Keep the public header ASCII-compatible to avoid MSVC source-encoding warnings.

## 0.1.0

- Single-header C++17 coefficient design and audio processing.
- Bell, shelves, notch, tilt, band-pass, and continuous 12-96 dB/oct cuts.
- Fixed-size design queue and crossfading processors.
