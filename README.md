# DecrampEQ

A C++17 single-header library for causal IIR equalizers whose magnitude responses
are fitted to analog prototypes over a finite frequency band. It includes
coefficient design, planar audio processing, click-reducing transitions, and an
SPSC coefficient queue. Runtime dependency: the C++ standard library.

[日本語](README.ja.md) · [API and threading](docs/api.md) ·
[Validation](docs/validation.md) · [Changelog](CHANGELOG.md) · [MIT license](LICENSE)

Version 0.2.0 is the initial public release. The API is pre-1.0.

[Releases](https://github.com/daetdev-commits/DecrampEQ/releases) ·
[CI results](https://github.com/daetdev-commits/DecrampEQ/actions/workflows/ci.yml)

## Features

- Bell, low/high shelf, low/high pass, notch, tilt, and band-pass.
- Continuous 12–96 dB/oct cut slopes, up to 16 biquads per band.
- `float` or `double` samples; coefficients and filter states use `double`.
- Fixed-size processing state and bounded coefficient fitting.
- Crossfading updates with per-channel input-history reconstruction.
- Allocation-free, lock-free sample processing; a single-producer,
  single-consumer update queue.

The fit targets **magnitude**. Phase follows the designed digital IIR transfer
function. Fractional slopes are finite-band approximations, and some parameter
combinations are unavailable. Check both validity and fit error for your use.

## Quick start

Copy [`include/DecrampEQ.hpp`](include/DecrampEQ.hpp) into your include path,
or use one of the CMake options below.

```cpp
#include <DecrampEQ.hpp>
#include <array>

int main() {
    auto coefficients = decramp::design(48000.,
        {decramp::Type::bell, 10000., 0.7071067811865476, 12.});
    if (!decramp::isValid(coefficients) || coefficients.maxErrorDb > 0.1)
        return 1;

    decramp::Processor<2, float> eq;
    if (!eq.set(coefficients, true)) return 2;
    std::array<float, 256> left{}, right{};
    left[0] = right[0] = 1.f;
    eq.process({left.data(), right.data()}, left.size());
}
```

For changing parameters in a plug-in, design on a worker/control thread and
transfer complete results through `DesignQueue`. Drain the queue at the start of
an audio block and apply the newest result to `SmoothProcessor`. Keep the queue
and processor alive across blocks. Retry a full queue from the producer thread.
The complete [realtime example](examples/realtime.cpp) demonstrates this pattern.

## CMake

Vendored source or `FetchContent`:

```cmake
add_subdirectory(path/to/DecrampEQ)
target_link_libraries(my_target PRIVATE DecrampEQ::DecrampEQ)
```

Installed package:

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/path/to/prefix
cmake --install build
```

```cmake
find_package(DecrampEQ 0.2 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE DecrampEQ::DecrampEQ)
```

Pass `-DCMAKE_PREFIX_PATH=/path/to/prefix` to the consuming project's configure
command when needed. The installed package is relocatable and exports only its
include directory and C++17 requirement. Project warning and sanitizer options
are private to its own executables.

## Supported inputs and accuracy

| Parameter | Supported input |
| --- | --- |
| Sample rate | Finite, at least 44.1 kHz |
| Center/cutoff | At least 20 Hz, below Nyquist |
| Q | 0.3–10 |
| Bell/shelf/tilt gain | −24 to +24 dB |
| Low/high-pass slope | 12–96 dB/oct, continuous |
| Fit upper limit | 20 Hz to Nyquist; default 20 kHz |

The fit interval for tilt, notch, and band-pass must have a positive width above
20 Hz. The tilt pivot must be within that interval. At the default cut Q, integer
slopes use Butterworth pole placements; `frequencyHz` is the normalized −3.0103 dB
cutoff. The API guide explains the other shape conventions.

`valid` and `isValid()` establish coefficient format, finiteness, and pole
stability with a numerical margin. They do not establish a particular fit
tolerance or output level. `maxErrorDb` is a sampled estimate, not a proven bound
over every frequency. Extreme Q and fractional slopes can exceed 0.5 dB error.
Accuracy beyond `fitTopHz` is not promised. [Validation notes](docs/validation.md)
give measured cases, regression coverage, and limits.

Use ordinary IEEE floating-point semantics: `-ffast-math` and `/fp:fast` are
unsupported because they can invalidate finite-value checks and fitting
assumptions. Audio buffers and samples must satisfy the API's input contract.

## Tests, sanitizers, and benchmarks

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DDECRAMPEQ_BUILD_TESTS=ON -DDECRAMPEQ_BUILD_EXAMPLE=ON \
  -DDECRAMPEQ_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

GCC/Clang builds can add `-DDECRAMPEQ_ENABLE_SANITIZERS=ON` for AddressSanitizer
and UndefinedBehaviorSanitizer. MSVC uses AddressSanitizer; its runtime DLL must
be discoverable when running tests. Configure a fresh build directory when
changing toolchains or instrumentation.

`-DDECRAMPEQ_BUILD_BENCHMARKS=ON` adds `decramp_benchmark`, which prints timing and
error measurements as CSV. Timing is not a correctness gate.

Tests cover independent analog references, processed tone gains, stable poles,
fractional-order boundaries, malformed input, history/reset behavior, update
ordering, SPSC transfer, C++ allocations, and installed/vendored consumers in
multiple translation units. The CI workflow adds Linux GCC/Clang, macOS, Windows,
and sanitizer configurations. The [release checklist](docs/release-readiness.md)
records locally completed checks and the initial hosted CI results.

## License and contributions

MIT; the complete notice is included in the header for single-file distribution.
Keep that notice when redistributing it. See [LICENSE](LICENSE),
[references](THIRD_PARTY.md), the [license/provenance review](docs/license-audit.md),
and [contribution guidance](CONTRIBUTING.md).
