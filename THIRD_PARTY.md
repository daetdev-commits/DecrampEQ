# Dependencies and references

The distributed C++ library uses the C++17 standard library. Its single header,
examples, tests, and CMake integration do not bundle an audio framework, a plug-in
SDK, or another DSP library. Compiler and standard-library implementations have
their own licenses and are supplied by the consumer's toolchain.

## Mathematical references

- Robert Bristow-Johnson's analog equalizer prototypes, as documented in the
  [W3C Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/), are the
  reference for the bell, shelf, and second-order cut responses. The W3C document
  identifies its editor, original author, and document license. This repository
  links to that document rather than redistributing its text.
- Integer cut slopes use Butterworth pole placements. Fractional orders and
  decramping are bounded numerical fits over the configured finite band.
- The coefficient solver uses damped least squares and Cholesky factorization.

Names of mathematical methods identify the methods used; they are not bundled
third-party implementations or endorsements of this project.

The project is offered under the MIT license in [LICENSE](LICENSE). A source
inventory and limited provenance review found no conflicting third-party
license or identifiable imported C++ implementation; its evidence and limits
are recorded in [the license review](docs/license-audit.md). This finding is not
a guarantee of unique AI-generated code or permission to relicense material
that might later be identified as third-party work. Validation toolchains and
generated build directories are separate from the source distribution.
