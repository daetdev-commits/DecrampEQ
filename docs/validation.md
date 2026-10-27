# Validation notes

These are measurements and regression coverage for the 0.2.0 public-release candidate, not a universal error bound.

## Low-cutoff fractional low-pass regression

Parameters: 192 kHz sample rate, 20 Hz cutoff, Q=0.3, 42.5 dB/oct, fit upper limit 20 kHz.

| Measurement | 0.1.1 | 0.1.2 |
| --- | ---: | ---: |
| Sampled error at 20 Hz and above | 4.806 dB | about 0.054 dB |
| Response at 1 Hz | +30.481 dB | about -0.011 dB |
| Response at 20 Hz | +1.796 dB | about -3.027 dB |
| Analog reference at 20 Hz | -3.010 dB | -3.010 dB |

The old fractional stage could settle on a poor local fit with a large subsonic resonance. The revised designer compares a second deterministic starting point when needed. Validation includes mixed linear/logarithmic samples, prototype focus points, and frequencies derived from complex poles and zeros. This helps detect narrow peaks and notches that a uniform grid can miss.

`test_fractional.cpp` checks independent analog-reference values, subsonic gain, a processed 20 Hz sine, and continuity near integer slopes at 44.1, 48, 96, and 192 kHz.

## Parameter grid

`test_design_grid.cpp` attempts 1,152 combinations across eight filter shapes, four sample rates, low and near-Nyquist frequencies, Q extremes, and gain/slope settings. In the measured run, 1,029 combinations returned valid coefficients, compared with 1,001 in 0.1.2. All returned designs passed independent pole-root checks and produced finite 512-sample impulse outputs. The largest reported sampled fit error remained about 0.754 dB, for a 192 kHz / 86.4 kHz high-pass with a 20 kHz fit limit and 42.5 dB/oct slope.

Unavailable combinations are allowed by this stress test. It tests numerical validity of the returned designs; it does not promise that every requested parameter combination is supported or accurate. The sampled error is not a proof over every frequency.

## Frequency boundaries and notch validation

`test_boundaries.cpp` requires all 72 combinations of a 20 Hz cutoff, low-/high-pass, Q=0.3 / 1/sqrt(2) / 10, slopes 12 / 42.5 / 96 dB/oct, and four sample rates to produce valid designs. The requested cutoff must be within 0.2 dB of the normalized -3.0103 dB point and the reported fit error must be below 0.5 dB. Public requests below 20 Hz remain invalid. Internally, resonance normalization may place a prototype below 20 Hz.

The same test checks notch shoulders against an independent analog transfer function, including a fit interval only 0.001 Hz wide starting at the notch center. Notch fitting and validation cancel the common zero before taking logarithms. This gives a finite limiting error at the center rather than subtracting two negative infinities or skipping every sample in a narrow interval. It does not promise an infinitely deep null in floating-point processing.

## Response-helper range

`test_contracts.cpp` checks 16 sections of 1e-100 gain (-32,000 dB), 16 sections of 1e100 gain (+32,000 dB), opposite gains that cancel, and numerator coefficients whose unscaled sum would overflow. These validate the dB response helper; they do not promise that applying such extreme custom gains to audio will produce finite samples.

## Cost and threading

A difficult fractional stage can run two fits, each bounded to 42 iterations and eight damping trials per iteration. Memory remains fixed-size. This work belongs on a control/worker thread. Sample processing and its allocation-free, lock-free behavior are unchanged.

### Design timing

Local Windows x64 / MSVC Release measurements, 41 timed trials per case. Design cases use three warm-up designs and vary the center frequency slightly between trials. The repeated-update case first warms 32 audio blocks. These times are machine-dependent observations, not CI thresholds or real-time guarantees.

| Case | 0.1.3 median | 0.1.4 median |
| --- | ---: | ---: |
| 48 kHz, 18 kHz notch, Q=3 | 2.354 ms | 0.929 ms |
| 48 kHz, 18 kHz band-pass, Q=1 | 7.984 ms | 3.551 ms |
| Repeated identical 96 dB/oct update + 128 stereo samples | 81.4 us | 5.0 us |

The notch/band-pass fitter caches frequency terms and coefficient models, including the twelve perturbed coefficient models used by the finite-difference Jacobian. Iteration limits and fitting objectives remain the same. A local comparison of 128 notch/band-pass designs at four sample rates found no availability changes and a maximum sampled curve change of about 0.000017 dB. Floating-point coefficients are not bit-identical after the refactor.

### Repeated updates

`test_contracts.cpp` warms a 192 kHz / 20 Hz low-pass with constant 0.25 input, then repeatedly sends identical active coefficients every 128 samples. Version 0.1.3 unnecessarily replayed a truncated history and produced up to about 0.131 output deviation from a continuously running reference. Version 0.1.4 skips the redundant transition and has zero measured deviation in this test.

The same test suite checks the ordering of a request arriving exactly after a fade ends at a block boundary. A new target must supersede an older pending target rather than reverting to it after another fade.

Build the standalone benchmark with `-DDECRAMPEQ_BUILD_BENCHMARKS=ON`, build target `decramp_benchmark` in Release mode, and run it to obtain CSV medians, means, sampled errors, and valid-design counts. Timing is kept separate from correctness tests.

## Reproduce

```sh
cmake -S . -B build -DDECRAMPEQ_BUILD_TESTS=ON -DDECRAMPEQ_BUILD_EXAMPLE=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Public-release preparation

The initial-publication gate additionally checks warning-clean MSVC and Clang
builds, MSVC AddressSanitizer, C++ allocation interception, both complete examples,
and a relocated installed package plus a vendored consumer with two translation
units. See [release readiness](release-readiness.md) for the completed local gates
and the separate status of remote Linux/macOS CI.
