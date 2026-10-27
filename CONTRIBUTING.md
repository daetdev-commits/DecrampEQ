# Contributing

Bug reports and focused improvements are welcome. Include the compiler and
version, sample rate, complete `Band` parameters, `valid`/`maxErrorDb` result, and
a minimal example. For an audible issue, describe the signal, block size, update
sequence, and expected versus observed result.

Before proposing a change, run the correctness and consumer tests with warnings
as errors. Add a regression that captures the user-visible failure. Numerical
tests should use independent reference responses or measured processed audio
rather than asserting a copy of the implementation.

Keep the application API in namespace `decramp`, retain the C++17 single-header
option, and keep audio processing free of allocation and locks. Discuss public
API changes and additional dependencies before implementing them. Use four-space
indentation and keep changes focused.

The checked-in `.clang-format` records the source style. The release preparation
used clang-format 19; format modified C++ files before submitting them.

Use the opt-in benchmark for performance claims. Report the platform, compiler,
configuration, and parameters; timing must not become a machine-dependent CI
pass/fail threshold.

Contributions submitted for inclusion are distributed under this project's MIT
license. Submit only work you have permission to share and preserve applicable
attribution notices.
