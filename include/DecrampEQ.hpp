// SPDX-License-Identifier: MIT
// Copyright (c) 2026 DecrampEQ contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

// DecrampEQ: single-header C++17 EQ library. Public API: namespace decramp.
// The coefficient designer below is kept in namespace analog_eq_2sos for
// compatibility with the original standalone design header.
// Analog prototypes follow the RBJ Audio EQ Cookbook.
// Design coefficients on the control / worker thread, e.g. when a UI parameter
// changes; publish a complete Design to the audio thread and smooth transitions
// there. This is not cheap enough for per-sample or every-block redesign.
// The designer uses fixed-size stack arrays, a fixed iteration bound, and no
// heap allocation. Process samples with the returned biquads in order;
// a0 is always 1, and the order is b0,b1,b2,a1,a2.
// Designed for |gain| <= 24 dB (shelves and bell), Q in [0.3, 10],
// 20 Hz <= center/cutoff < Nyquist, sample rates >= 44.1 kHz.
// The shelf's Q is the Cookbook's Q, not S; Q=1/sqrt(2) corresponds to S=1.
// Check Result::maxErrorDb: two sections cannot guarantee every tolerance.
// Example:
//   using namespace analog_eq_2sos;
//   auto low = design(Type::lowShelf, 48000., 300., 0.70710678, 6.);
//   auto hp  = design(Type::highPass, 48000., 80., 0.70710678);
//   if (low.valid && hp.valid) { /* publish both sections atomically */ }

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <type_traits>

namespace analog_eq_2sos {

enum class Type {
    bell,
    lowShelf,
    highShelf,
    lowPass,
    highPass,
    notch,
    tilt,
    bandPass,
    partialHighPass
};

struct SOS {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};
struct Result {
    std::array<SOS, 2> sections{};
    double maxErrorDb = 0; // sampled amplitude error over fit band, not a proof
    bool valid = false;
};

namespace detail {
inline bool validSection(const SOS &section) noexcept {
    // Jury stability conditions with enough margin to avoid poles rounding
    // onto the unit circle when the coefficients are used in double precision.
    const double margin = 16. * std::numeric_limits<double>::epsilon();
    return std::isfinite(section.b0) && std::isfinite(section.b1) && std::isfinite(section.b2) &&
           std::isfinite(section.a1) && std::isfinite(section.a2) &&
           std::abs(section.a2) < 1. - margin &&
           1. + section.a2 - std::abs(section.a1) >
               margin * (1. + std::abs(section.a1) + std::abs(section.a2));
}

inline double sectionDb(const SOS &section, const std::complex<double> &z) {
    const double scale =
        std::max({std::abs(section.b0), std::abs(section.b1), std::abs(section.b2)});
    if (scale == 0.)
        return -std::numeric_limits<double>::infinity();
    const auto z2 = z * z;
    const auto numerator = section.b0 / scale + section.b1 / scale * z + section.b2 / scale * z2;
    const auto denominator = 1. + section.a1 * z + section.a2 * z2;
    return 20. * std::log10(scale) + 20. * std::log10(std::abs(numerator)) -
           20. * std::log10(std::abs(denominator));
}

constexpr int n = 8;
constexpr int grid = 96;
constexpr double pi = 3.14159265358979323846;
constexpr double log20 = 8.685889638065036553;
using Vec = std::array<double, n>;

inline double clamp(double x, double lo, double hi) {
    return std::max(lo, std::min(hi, x));
}
inline double inverseTanh(double x) {
    x = clamp(x, -0.999999999999, 0.999999999999);
    return 0.5 * std::log((1 + x) / (1 - x));
}
inline void pairFromPolynomial(const std::complex<double> &r1, const std::complex<double> &r2,
                               double &x, double &y) {
    const double c1 = -(r1 + r2).real();
    const double c2 = (r1 * r2).real();
    y = inverseTanh(c2);
    x = inverseTanh(c1 / (1 + c2));
}

// Analog prototype: H(s)=(s^2+A*w0*s/Q+w0^2)/(s^2+w0*s/(A*Q)+w0^2).
// Map its poles and zeros using z=exp(s/fs) for the initial estimate.
inline Vec initial(Type type, double fs, double f0, double q, double gainDb,
                   double targetScale = 1.) {
    const double A = std::pow(10., gainDb / 40.);
    const double w0 = 2 * pi * f0;
    auto roots = [fs, w0](double damping) {
        const std::complex<double> radical =
            std::sqrt(std::complex<double>(damping * damping - 4 * w0 * w0, 0));
        return std::array<std::complex<double>, 2>{std::exp((-damping + radical) / (2 * fs)),
                                                   std::exp((-damping - radical) / (2 * fs))};
    };
    std::array<std::complex<double>, 2> zeros{}, poles{};
    if (type == Type::bell) {
        zeros = roots(A * w0 / q);
        poles = roots(w0 / (A * q));
    } else if (type == Type::highShelf || type == Type::lowShelf) {
        // Cookbook shelf: zero/pole natural frequencies are w0/sqrt(A) and
        // w0*sqrt(A), each with Q; low shelf exchanges zero/pole pairs.
        auto shelfRoots = [fs, q](double natural) {
            const double damping = natural / q;
            const auto d =
                std::sqrt(std::complex<double>(damping * damping - 4 * natural * natural, 0.));
            return std::array<std::complex<double>, 2>{std::exp((-damping + d) / (2 * fs)),
                                                       std::exp((-damping - d) / (2 * fs))};
        };
        auto lo = shelfRoots(w0 / std::sqrt(A));
        auto hi = shelfRoots(w0 * std::sqrt(A));
        zeros = type == Type::highShelf ? lo : hi;
        poles = type == Type::highShelf ? hi : lo;
    } else {
        poles = roots(w0 / q);
    }
    Vec p{};
    if (type == Type::lowPass) {
        p[0] = p[1] = 0.; // Zero pair at z=0.
    } else if (type != Type::highPass) {
        if (type == Type::partialHighPass) {
            const double r = 1. - std::pow(1e-5, targetScale);
            zeros = {r, r};
        }
        pairFromPolynomial(zeros[0], zeros[1], p[0], p[1]);
    }
    pairFromPolynomial(poles[0], poles[1], p[2], p[3]);
    if (type == Type::lowPass && targetScale < 1.) {
        // Seed the fractional stage between bypass and the full prototype.
        // Starting at the full low-pass zeros often traps LM when Q is near 0.5.
        const double a2 = std::tanh(p[3]);
        const double a1 = std::tanh(p[2]) * (1. + a2);
        const double b2 = (1. - targetScale) * a2;
        const double b1 = (1. - targetScale) * a1;
        p[1] = inverseTanh(b2);
        p[0] = inverseTanh(b1 / (1. + b2));
    }
    // A near-canceling pair near Nyquist is the second section's starting point.
    constexpr double root1 = -0.65;
    constexpr double root2 = -0.25;
    const double c2 = root1 * root2;
    const double c1 = -(root1 + root2);
    p[4] = p[6] = inverseTanh(c1 / (1 + c2));
    p[5] = p[7] = inverseTanh(c2);
    return p;
}

inline SOS section(const Vec &p, int offset, Type type, double gainDb, double hpNyquistGain) {
    const double nb = std::tanh(p[offset]);
    const double mb = std::tanh(p[offset + 1]);
    const double na = std::tanh(p[offset + 2]);
    const double ma = std::tanh(p[offset + 3]);
    if (type == Type::highPass && offset == 0) {
        const double b0 = hpNyquistGain * std::exp(p[0]) * (1 - na) * (1 + ma) / 4;
        return {b0, -2 * b0, b0, na * (1 + ma), ma};
    }
    if (type == Type::highPass || type == Type::partialHighPass) {
        const double scale = ((1 - na) * (1 + ma)) / ((1 - nb) * (1 + mb));
        return {scale, scale * nb * (1 + mb), scale * mb, na * (1 + ma), ma};
    }
    const double scale = ((1 + na) * (1 + ma)) / ((1 + nb) * (1 + mb)) *
                         (offset == 0 && type == Type::lowShelf ? std::pow(10., gainDb / 20.) : 1.);
    return {scale, scale * nb * (1 + mb), scale * mb, na * (1 + ma), ma};
}

struct Node {
    double z1r, z1i, z2r, z2i, sinHalf, target;
};
inline double analogDb(Type type, double f, double f0, double q, double gainDb) {
    if (f == 0.) {
        if (type == Type::highPass || type == Type::partialHighPass)
            return -std::numeric_limits<double>::infinity();
        if (type == Type::lowShelf)
            return gainDb;
        return 0.;
    }
    const double x = f / f0;
    const double A = std::pow(10., gainDb / 40.);
    const double base = (1 - x * x) * (1 - x * x);
    const double v = x / q;
    const double factor = 10. / std::log(10.);
    if (type == Type::bell)
        return factor * std::log((base + A * A * v * v) / (base + v * v / (A * A)));
    if (type == Type::lowPass)
        return -factor * std::log(base + v * v);
    if (type == Type::highPass || type == Type::partialHighPass)
        return factor * (4 * std::log(x) - std::log(base + v * v));
    const double beta2 = A * v * v;
    const double lo = (1 - A * x * x) * (1 - A * x * x) + beta2;
    const double hi = (A - x * x) * (A - x * x) + beta2;
    return 20 * std::log10(A) + factor * std::log(type == Type::highShelf ? lo / hi : hi / lo);
}
inline Node makeNode(Type type, double f, double fs, double f0, double q, double gainDb) {
    const double w = 2 * pi * f / fs;
    return {std::cos(w),      -std::sin(w),    std::cos(2 * w),
            -std::sin(2 * w), std::sin(w / 2), analogDb(type, f, f0, q, gainDb)};
}

// Evaluates the response and its analytic derivatives in stable lattice
// coordinates. Stable poles are guaranteed by |tanh(p[i])|<1.
inline double evaluate(const Vec &p, const Node &node, Type type, double gainDb,
                       double hpNyquistGain, Vec *jac = nullptr) {
    if (jac)
        jac->fill(0.);
    double total = type == Type::lowShelf ? gainDb : 0.;
    for (int s = 0; s < 2; ++s) {
        const int j = s * 4;
        const double k[4] = {std::tanh(p[j]), std::tanh(p[j + 1]), std::tanh(p[j + 2]),
                             std::tanh(p[j + 3])};
        double db[2]{};
        for (int t = 0; t < 2; ++t) {
            if (type == Type::highPass && s == 0 && t == 0) {
                // (1-z^-1)^2 has two exact DC zeros. Nyquist gain starts
                // at the analog value and may vary for a better band fit.
                db[0] = std::log(hpNyquistGain) + p[0] + 2 * std::log(node.sinHalf);
                if (jac)
                    (*jac)[0] = log20;
                continue;
            }
            const double v = k[2 * t], u = k[2 * t + 1];
            const double c1 = v * (1 + u);
            const double pr = 1 + c1 * node.z1r + u * node.z2r;
            const double imaginary = c1 * node.z1i + u * node.z2i;
            const double sq = pr * pr + imaginary * imaginary;
            if (!(sq > 1e-28))
                return std::numeric_limits<double>::quiet_NaN();
            const double signRef =
                (type == Type::highPass || type == Type::partialHighPass) ? -1. : 1.;
            db[t] = 0.5 * std::log(sq) - std::log1p(signRef * v) - std::log1p(u);
            if (jac) {
                const double dvr = (1 + u) * node.z1r;
                const double dvi = (1 + u) * node.z1i;
                const double dur = v * node.z1r + node.z2r;
                const double dui = v * node.z1i + node.z2i;
                const double sign = t == 0 ? 1. : -1.;
                (*jac)[j + 2 * t] =
                    sign * log20 * (1 - v * v) *
                    ((pr * dvr + imaginary * dvi) / sq - signRef / (1 + signRef * v));
                (*jac)[j + 2 * t + 1] =
                    sign * log20 * (1 - u * u) * ((pr * dur + imaginary * dui) / sq - 1 / (1 + u));
            }
        }
        total += log20 * (db[0] - db[1]);
    }
    return total;
}

inline bool solve(double matrix[n][n], double rhs[n], double out[n]) {
    // Cholesky with positive diagonal from Levenberg regularization.
    double L[n][n]{};
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j <= i; ++j) {
            double x = matrix[i][j];
            for (int k = 0; k < j; ++k)
                x -= L[i][k] * L[j][k];
            if (i == j) {
                if (!(x > 1e-24) || !std::isfinite(x))
                    return false;
                L[i][i] = std::sqrt(x);
            } else
                L[i][j] = x / L[j][j];
        }
    }
    double y[n]{};
    for (int i = 0; i < n; ++i) {
        double x = rhs[i];
        for (int k = 0; k < i; ++k)
            x -= L[i][k] * y[k];
        y[i] = x / L[i][i];
    }
    for (int i = n - 1; i >= 0; --i) {
        double x = y[i];
        for (int k = i + 1; k < n; ++k)
            x -= L[k][i] * out[k];
        out[i] = x / L[i][i];
    }
    return true;
}

inline double cost(const Vec &p, const std::array<Node, grid> &nodes, Type type, double gainDb,
                   double hpNyquistGain) {
    double sum = 0.;
    for (const auto &node : nodes) {
        const double e = evaluate(p, node, type, gainDb, hpNyquistGain) - node.target;
        if (!std::isfinite(e))
            return std::numeric_limits<double>::infinity();
        sum += e * e;
    }
    return sum;
}

// One bounded local fit. Kept separate so difficult fractional stages can
// compare two deterministic starting points without duplicating the solver.
inline Vec refine(Vec p, const std::array<Node, grid> &nodes, Type type, double g,
                  double hpNyquistGain) {
    double oldCost = cost(p, nodes, type, g, hpNyquistGain);
    double damping = 1e-7;
    const int maxIterations = 42;
    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        double h[n][n]{};
        double gradient[n]{};
        for (const auto &node : nodes) {
            Vec jac{};
            const double e = evaluate(p, node, type, g, hpNyquistGain, &jac) - node.target;
            for (int i = 0; i < n; ++i) {
                gradient[i] -= jac[i] * e;
                for (int j = 0; j <= i; ++j)
                    h[i][j] += jac[i] * jac[j];
            }
        }
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
                h[i][j] = h[j][i];
        bool accepted = false;
        for (int trial = 0; trial < 8; ++trial) {
            double m[n][n]{};
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j)
                    m[i][j] = h[i][j];
                m[i][i] += damping * (1 + h[i][i]);
            }
            double step[n]{};
            if (solve(m, gradient, step)) {
                auto candidate = p;
                for (int i = 0; i < n; ++i)
                    candidate[i] = clamp(p[i] + step[i], -12., 12.);
                const double newCost = cost(candidate, nodes, type, g, hpNyquistGain);
                if (newCost < oldCost) {
                    const double improvement = oldCost - newCost;
                    p = candidate;
                    oldCost = newCost;
                    damping = std::max(damping * .3, 1e-12);
                    accepted = true;
                    if (improvement < 1e-15)
                        iteration = maxIterations;
                    break;
                }
            }
            damping = std::min(damping * 10., 1e12);
        }
        if (!accepted && damping >= 1e12)
            break;
    }
    return p;
}

inline Vec fractionalLowPassInitial(double fs, double center, double q, double scale, double top) {
    if (q > 1. && scale >= .1) {
        // Preserve a resonant pole pair in the initial model. A real-pole
        // starting point can otherwise settle on a non-resonant local fit.
        auto p = initial(Type::lowPass, fs, center, std::pow(q, scale) * .7071067811865476, 0.);
        pairFromPolynomial(std::complex<double>(std::exp(-16. * pi * center / fs), 0.),
                           std::complex<double>(0., 0.), p[0], p[1]);
        return p;
    }
    // Split a real analog pole into two logarithmically spaced pole/zero
    // pairs. For a complex prototype, bracket its natural frequency with
    // distinct branches so the fit can recover the resonance. This follows the
    // fractional roll-off over a wide band without starting with a spurious
    // high-Q pair close to DC. The bounded fit then refines all eight values.
    Vec p{};
    const double radical = std::sqrt(std::max(0., 1. / (q * q) - 4.));
    for (int sectionIndex = 0; sectionIndex < 2; ++sectionIndex) {
        const double corner =
            q <= .5 ? center * .5 * (1. / q + (sectionIndex == 0 ? -radical : radical))
                    : center * (sectionIndex == 0 ? .5 : 2.);
        const double ratio = std::max(1.001, top / corner);
        std::array<std::complex<double>, 2> poles{}, zeros{};
        for (int j = 0; j < 2; ++j) {
            const double poleHz = corner * std::pow(ratio, (j + (1. - scale) * .5) / 2.);
            const double zeroHz = corner * std::pow(ratio, (j + (1. + scale) * .5) / 2.);
            poles[j] = std::exp(-2. * pi * poleHz / fs);
            zeros[j] = std::exp(-2. * pi * zeroHz / fs);
        }
        pairFromPolynomial(zeros[0], zeros[1], p[sectionIndex * 4], p[sectionIndex * 4 + 1]);
        pairFromPolynomial(poles[0], poles[1], p[sectionIndex * 4 + 2], p[sectionIndex * 4 + 3]);
    }
    return p;
}

inline Vec fractionalHighPassInitial(double fs, double center, double q, double scale) {
    auto p = initial(Type::partialHighPass, fs, center, q, 0., scale);
    const double a2 = std::tanh(p[3]);
    const double a1 = std::tanh(p[2]) * (1. + a2);
    // Approach an exact pole/zero cancellation as the fractional order tends
    // to zero, and the full DC-zero numerator as it tends to one. The old
    // frequency-independent seed could create a narrow, misplaced notch.
    const double b2 = (1. - scale) * a2 + scale;
    const double b1 = (1. - scale) * a1 - 2. * scale;
    p[1] = inverseTanh(b2);
    p[0] = inverseTanh(b1 / (1. + b2));
    return p;
}

template <class Inspect>
inline void criticalFrequencies(const SOS &section, double fs, double first, double top,
                                Inspect inspect) {
    // A narrow feature can fall between both uniform and logarithmic grid
    // points. Inspect each complex root's angle and its radial bandwidth too.
    auto pair = [&](double c1, double c2) {
        if (!(c2 > 0.) || !(c1 * c1 < 4. * c2))
            return;
        const double radius = std::sqrt(c2);
        const double frequency = fs / (2. * pi) * std::acos(clamp(-c1 / (2. * radius), -1., 1.));
        const double width = std::abs(std::log(radius)) * fs / (2. * pi);
        if (frequency < first || frequency > top)
            return;
        for (double offset : {0., -.25, .25, -1., 1., -2., 2.})
            inspect(clamp(frequency + offset * width, first, top));
    };
    pair(section.a1, section.a2);
    if (section.b0 != 0.)
        pair(section.b1 / section.b0, section.b2 / section.b0);
}

inline double sampledError(const Vec &p, const std::array<Node, grid> &nodes, Type type, double fs,
                           double center, double q, double gain, double top, double scale,
                           double hpNyquistGain) {
    double worst = 0.;
    auto inspect = [&](const Node &node) {
        const double error = std::abs(evaluate(p, node, type, gain, hpNyquistGain) - node.target);
        if (!std::isfinite(error))
            return false;
        worst = std::max(worst, error);
        return true;
    };
    // Retain the fitting focus points and add independent linear/log checks.
    // Low-pass validation includes sub-20 Hz content: a bad fractional fit
    // must not hide a large subsonic resonance outside the old linear grid.
    for (const auto &node : nodes)
        if (!inspect(node))
            return std::numeric_limits<double>::infinity();
    const bool highPass = type == Type::highPass || type == Type::partialHighPass;
    const double first = highPass ? 20. : 0.;
    const double logFirst = highPass ? 20. : std::min(20., center * .01);
    for (int i = 0; i <= 1024; ++i) {
        auto node = makeNode(type, first + (top - first) * i / 1024., fs, center, q, gain);
        node.target *= scale;
        if (!inspect(node))
            return std::numeric_limits<double>::infinity();
    }
    for (int i = 0; i <= 128; ++i) {
        auto node =
            makeNode(type, logFirst * std::pow(top / logFirst, i / 128.), fs, center, q, gain);
        node.target *= scale;
        if (!inspect(node))
            return std::numeric_limits<double>::infinity();
    }
    bool criticalValid = true;
    for (int offset : {0, 4}) {
        criticalFrequencies(section(p, offset, type, gain, hpNyquistGain), fs, first, top,
                            [&](double hz) {
                                auto node = makeNode(type, hz, fs, center, q, gain);
                                node.target *= scale;
                                criticalValid &= inspect(node);
                            });
    }
    if (!criticalValid)
        return std::numeric_limits<double>::infinity();
    return worst;
}

} // namespace detail

// Fit range: DC..fitTopHz, or 20 Hz..fitTopHz for high-pass (DC is -infinity).
// The cutoff is the analog prototype's natural frequency; for low/high pass
// the magnitude there is Q, so it is -3.01 dB only for Q=1/sqrt(2).
// At most two fits of 42 LM iterations; no dynamic memory or unbounded loop.
namespace detail {
inline Result designPrototype(Type type, double sampleRate, double centerHz, double q,
                              double gainDb = 0., double fitTopHz = 20000.,
                              double targetScale = 1.) {
    Result result{};
    if (!std::isfinite(sampleRate) || !std::isfinite(centerHz) || !std::isfinite(q) ||
        !std::isfinite(gainDb) || !std::isfinite(fitTopHz) || !std::isfinite(targetScale) ||
        targetScale <= 0. || targetScale > 1. || sampleRate < 44100. || centerHz <= 0. ||
        centerHz >= sampleRate * 0.5 || q < 0.3 || q > 10. || std::abs(gainDb) > 24. ||
        fitTopHz < 20. || fitTopHz > sampleRate * 0.5)
        return result;
    if (type != Type::bell && type != Type::lowShelf && type != Type::highShelf &&
        type != Type::lowPass && type != Type::highPass && type != Type::partialHighPass)
        return result;
    if (targetScale != 1. && type != Type::lowPass && type != Type::partialHighPass)
        return result;
    const bool gainFilter = type == Type::bell || type == Type::lowShelf || type == Type::highShelf;
    if (gainFilter && std::abs(gainDb) < 1e-10) {
        result.valid = true;
        return result;
    }
    const double g = gainFilter ? std::abs(gainDb) : 0.;
    const double hpNyquistGain =
        type == Type::highPass
            ? std::pow(10., detail::analogDb(type, sampleRate / 2., centerHz, q, 0.) / 20.)
            : 1.;
    std::array<detail::Node, detail::grid> nodes{};
    // Mixed linear/log and focus points: makes broad shelves and narrow peaks
    // visible to the fit. All nodes are generated once per parameter update.
    for (int i = 0; i < detail::grid; ++i) {
        double f;
        if (i < 48)
            f = fitTopHz * i / 47.;
        else if (i < 72)
            f = 20. * std::pow(fitTopHz / 20., (i - 48.) / 23.);
        else
            f = detail::clamp(centerHz + centerHz / q * (i - 84.) / 12., 0., fitTopHz);
        if (type == Type::highPass || type == Type::partialHighPass)
            f = std::max(20., f);
        nodes[i] = detail::makeNode(type, f, sampleRate, centerHz, q, g);
        nodes[i].target *= targetScale;
    }
    auto p = detail::initial(type, sampleRate, centerHz, q, g, targetScale);
    p = detail::refine(p, nodes, type, g, hpNyquistGain);
    double error = detail::sampledError(p, nodes, type, sampleRate, centerHz, q, g, fitTopHz,
                                        targetScale, hpNyquistGain);
    if ((type == Type::lowPass || type == Type::partialHighPass) && targetScale < 1. &&
        error > .05) {
        auto candidate =
            type == Type::lowPass
                ? detail::fractionalLowPassInitial(sampleRate, centerHz, q, targetScale, fitTopHz)
                : detail::fractionalHighPassInitial(sampleRate, centerHz, q, targetScale);
        auto candidateNodes = nodes;
        for (int i = 72; i < detail::grid; ++i) {
            const double distance = (i - 84.) / 12.;
            double hz = detail::clamp(centerHz + centerHz / q * 4. * distance * std::abs(distance),
                                      0., fitTopHz);
            if (type == Type::partialHighPass)
                hz = std::max(20., hz);
            candidateNodes[i] = detail::makeNode(type, hz, sampleRate, centerHz, q, g);
            candidateNodes[i].target *= targetScale;
        }
        candidate = detail::refine(candidate, candidateNodes, type, g, hpNyquistGain);
        const double candidateError =
            detail::sampledError(candidate, nodes, type, sampleRate, centerHz, q, g, fitTopHz,
                                 targetScale, hpNyquistGain);
        if (candidateError < error &&
            detail::validSection(detail::section(candidate, 0, type, g, hpNyquistGain)) &&
            detail::validSection(detail::section(candidate, 4, type, g, hpNyquistGain))) {
            p = candidate;
            error = candidateError;
        }
    }
    result.sections = {detail::section(p, 0, type, g, hpNyquistGain),
                       detail::section(p, 4, type, g, hpNyquistGain)};
    if (gainFilter && gainDb < 0) {
        // Reciprocal EQ: swapping numerator/denominator gives an exactly
        // reciprocal cut, valid because both zero and pole pairs are stable.
        for (auto &s : result.sections) {
            const double b0 = s.b0, b1 = s.b1, b2 = s.b2;
            const double inv = 1. / b0;
            s.b0 = inv;
            s.b1 = s.a1 * inv;
            s.b2 = s.a2 * inv;
            s.a1 = b1 * inv;
            s.a2 = b2 * inv;
        }
    }
    result.maxErrorDb = error;
    result.valid = std::isfinite(result.maxErrorDb) && detail::validSection(result.sections[0]) &&
                   detail::validSection(result.sections[1]);
    return result;
}
} // namespace detail

// The standalone compatibility API keeps its 20 Hz input limit. A cascaded
// filter may use a lower internal natural frequency to put its joint cutoff
// at 20 Hz; that private prototype is still checked for numerical stability.
inline Result design(Type type, double sampleRate, double centerHz, double q, double gainDb = 0.,
                     double fitTopHz = 20000., double targetScale = 1.) {
    if (centerHz < 20.)
        return {};
    return detail::designPrototype(type, sampleRate, centerHz, q, gainDb, fitTopHz, targetScale);
}

// Source-compatible bell overload for code using the earlier standalone API.
inline Result design(double sampleRate, double centerHz, double q, double gainDb,
                     double fitTopHz = 20000.) {
    return design(Type::bell, sampleRate, centerHz, q, gainDb, fitTopHz);
}

} // namespace analog_eq_2sos

namespace decramp {

using Type = analog_eq_2sos::Type;
using SOS = analog_eq_2sos::SOS;
// A 96 dB/oct filter uses eight decramped second-order prototypes,
// each designed with two SOS. Bell and shelves still use two SOS.
constexpr std::size_t maxSections = 16;
struct Design {
    std::array<SOS, maxSections> sections{};
    std::size_t count = 0;
    double maxErrorDb = 0.;
    bool valid = false;
};

struct Band {
    Type type = Type::bell;
    double frequencyHz = 1000.;
    double q = 0.7071067811865476;
    double gainDb = 0.;
    double fitTopHz = 20000.;
    double slopeDbPerOct = 12.; // lowPass / highPass: 12..96, continuous
};

// Validate public/custom Designs before accepting them into a processing path.
// A valid zero-section Design is unity. Only active sections are inspected.
// Finite coefficients and poles inside the unit circle with a small numerical
// margin are required;
// this does not promise any particular magnitude-fit tolerance or output gain.
inline bool isValid(const Design &designResult) noexcept {
    if (!designResult.valid || designResult.count > maxSections ||
        !std::isfinite(designResult.maxErrorDb) || designResult.maxErrorDb < 0.)
        return false;
    for (std::size_t i = 0; i < designResult.count; ++i)
        if (!analog_eq_2sos::detail::validSection(designResult.sections[i]))
            return false;
    return true;
}

namespace detail {
inline std::complex<double> transfer(const SOS &s, double f, double fs) {
    const auto z = std::polar(1., (-2 * analog_eq_2sos::detail::pi) * (f / fs));
    return (s.b0 + s.b1 * z + s.b2 * z * z) / (1. + s.a1 * z + s.a2 * z * z);
}
inline bool append(Design &out, const analog_eq_2sos::Result &part) {
    if (!part.valid || out.count + 2 > maxSections)
        return false;
    out.sections[out.count++] = part.sections[0];
    out.sections[out.count++] = part.sections[1];
    out.maxErrorDb += part.maxErrorDb;
    return true;
}
inline double db(double amplitude) {
    return 20. * std::log10(amplitude);
}
using Shape = std::array<double, 8>;
using ShapeSections = std::array<SOS, 2>;
struct ShapeNode {
    double frequency, reference;
    std::complex<double> z{}, z2{};
};
inline SOS denominator(const Shape &p, int index) {
    const double v = std::tanh(p[index]), u = std::tanh(p[index + 1]);
    return {1., 0., 0., v * (1. + u), u};
}
inline std::array<SOS, 2> shapeSections(const Shape &p, Type type, double fs, double center) {
    SOS first = denominator(p, 0), second = denominator(p, 4);
    const double v = std::tanh(p[2]), u = std::tanh(p[3]);
    second.b0 = 1.;
    second.b1 = v * (1. + u);
    second.b2 = u;
    if (type == Type::notch) {
        const double c = -2. * std::cos(2. * analog_eq_2sos::detail::pi * center / fs);
        const double gain = (1. + first.a1 + first.a2) / (2. + c);
        first.b0 = gain;
        first.b1 = gain * c;
        first.b2 = gain;
        const double normalized =
            (1. + second.a1 + second.a2) / (second.b0 + second.b1 + second.b2);
        second.b0 *= normalized;
        second.b1 *= normalized;
        second.b2 *= normalized;
    } else {
        first.b0 = 1.;
        first.b1 = -2.;
        first.b2 = 1.;
        const double atCenter =
            std::abs(transfer(first, center, fs) * transfer(second, center, fs));
        first.b0 /= atCenter;
        first.b1 /= atCenter;
        first.b2 /= atCenter;
    }
    return {first, second};
}
inline double shapeResponse(const Shape &p, Type type, double fs, double center, double frequency) {
    const auto sections = shapeSections(p, type, fs, center);
    return db(
        std::abs(transfer(sections[0], frequency, fs) * transfer(sections[1], frequency, fs)));
}
inline double shapeTarget(Type type, double f, double center, double q) {
    if (type == Type::notch) {
        const double x = f / center;
        const double a = (1. - x * x) * (1. - x * x);
        return 10. * std::log10(a / (a + x * x / (q * q)));
    }
    const double ratio = std::sqrt(1. + 1. / (4. * q * q));
    const double lo = center * (ratio - 1. / (2. * q));
    const double hi = center * (ratio + 1. / (2. * q));
    const auto response = [&](double hz) {
        return analog_eq_2sos::detail::analogDb(Type::highPass, hz, lo, 0.7071067811865476, 0.) +
               analog_eq_2sos::detail::analogDb(Type::lowPass, hz, hi, 0.7071067811865476, 0.);
    };
    return response(f) - response(center);
}

inline ShapeNode shapeNode(Type type, double f, double fs, double center, double q) {
    ShapeNode node{f, 0.};
    node.z = std::polar(1., (-2. * analog_eq_2sos::detail::pi) * (f / fs));
    node.z2 = node.z * node.z;
    if (type != Type::notch) {
        node.reference = shapeTarget(type, f, center, q);
        return node;
    }
    // Cancel the analog and digital notch zeros analytically before taking
    // logarithms. The quotient has a finite limit even at the exact center.
    const double u = center / fs, v = f / fs;
    const double angle = analog_eq_2sos::detail::pi * (v - u);
    const double square = angle * angle;
    const double sinc = std::abs(angle) < 1.e-4 ? 1. - square / 6. + square * square / 120.
                                                : std::sin(angle) / angle;
    const double zeroRatio = 4. * analog_eq_2sos::detail::pi * u * u / (u + v) *
                             std::sin(analog_eq_2sos::detail::pi * (u + v)) * sinc;
    const double x = f / center;
    node.reference = db(zeroRatio) + db(std::hypot(1. - x * x, x / q));
    return node;
}

inline double shapeError(const ShapeSections &sections, Type type, const ShapeNode &node) {
    const auto &z = node.z;
    const auto &z2 = node.z2;
    const auto firstDenominator = 1. + sections[0].a1 * z + sections[0].a2 * z2;
    if (type != Type::notch) {
        const auto first =
            (sections[0].b0 + sections[0].b1 * z + sections[0].b2 * z2) / firstDenominator;
        const auto second = (sections[1].b0 + sections[1].b1 * z + sections[1].b2 * z2) /
                            (1. + sections[1].a1 * z + sections[1].a2 * z2);
        return db(std::abs(first * second)) - node.reference;
    }
    return db(std::abs(sections[0].b0)) - db(std::abs(firstDenominator)) +
           analog_eq_2sos::detail::sectionDb(sections[1], z) + node.reference;
}

inline double shapeError(const Shape &p, Type type, double fs, double center,
                         const ShapeNode &node) {
    return shapeError(shapeSections(p, type, fs, center), type, node);
}

inline double shapeCost(const Shape &p, Type type, double fs, double center,
                        const std::array<ShapeNode, 96> &nodes) {
    double sum = 0.;
    const auto sections = shapeSections(p, type, fs, center);
    for (const auto &node : nodes) {
        const double diff = shapeError(sections, type, node);
        if (!std::isfinite(diff))
            return std::numeric_limits<double>::infinity();
        sum += diff * diff;
    }
    return sum;
}
inline Design fitShape(Type type, double fs, double center, double q, double top) {
    Design out{};
    Shape p{};
    auto polePair = [&](double centerFrequency, int offset) {
        const double frequency = 2. * analog_eq_2sos::detail::pi * centerFrequency;
        const double damping = frequency / 0.7071067811865476;
        const auto rad =
            std::sqrt(std::complex<double>(damping * damping - 4. * frequency * frequency, 0.));
        const auto a = std::exp((-damping + rad) / (2. * fs));
        const auto b = std::exp((-damping - rad) / (2. * fs));
        analog_eq_2sos::detail::pairFromPolynomial(a, b, p[offset], p[offset + 1]);
    };
    if (type == Type::notch) {
        const double frequency = 2. * analog_eq_2sos::detail::pi * center;
        const double damping = frequency / q;
        const auto rad =
            std::sqrt(std::complex<double>(damping * damping - 4. * frequency * frequency, 0.));
        analog_eq_2sos::detail::pairFromPolynomial(std::exp((-damping + rad) / (2. * fs)),
                                                   std::exp((-damping - rad) / (2. * fs)), p[0],
                                                   p[1]);
        const double a = -.65, b = -.25, c = a * b, d = -(a + b);
        p[2] = p[4] = analog_eq_2sos::detail::inverseTanh(d / (1. + c));
        p[3] = p[5] = analog_eq_2sos::detail::inverseTanh(c);
    } else {
        const double ratio = std::sqrt(1. + 1. / (4. * q * q));
        polePair(center * (ratio - 1. / (2. * q)), 0);
        polePair(center * (ratio + 1. / (2. * q)), 4);
        p[2] = p[3] = 0.;
    }
    std::array<ShapeNode, 96> nodes{};
    for (int i = 0; i < 96; ++i) {
        double hz;
        if (i < 32)
            hz = 20. + (top - 20.) * i / 31.;
        else if (i < 64)
            hz = 20. * std::pow(top / 20., (i - 32.) / 31.);
        else {
            // Dense samples on both sides of the exact notch / pass peak.
            const int sign = i < 80 ? -1 : 1;
            const double span = center * std::pow(2., (i % 16 - 8.) / 3.);
            hz = analog_eq_2sos::detail::clamp(center + sign * span / q, 20., top);
        }
        nodes[i] = shapeNode(type, hz, fs, center, q);
    }
    double previous = shapeCost(p, type, fs, center, nodes), damping = .001;
    for (int iteration = 0; iteration < 50; ++iteration) {
        double h[8][8]{}, gradient[8]{};
        const auto sections = shapeSections(p, type, fs, center);
        std::array<ShapeSections, 6> aboveSections{}, belowSections{};
        constexpr double derivativeStep = 1.e-4;
        for (int j = 0; j < 6; ++j) {
            auto moved = p;
            moved[j] += derivativeStep;
            aboveSections[j] = shapeSections(moved, type, fs, center);
            moved[j] -= 2. * derivativeStep;
            belowSections[j] = shapeSections(moved, type, fs, center);
        }
        for (const auto &node : nodes) {
            const double error = shapeError(sections, type, node);
            double jac[6]{};
            for (int j = 0; j < 6; ++j) {
                const double above = shapeError(aboveSections[j], type, node);
                const double below = shapeError(belowSections[j], type, node);
                jac[j] = (above - below) / (2. * derivativeStep);
            }
            for (int j = 0; j < 6; ++j) {
                gradient[j] -= jac[j] * error;
                for (int k = 0; k <= j; ++k)
                    h[j][k] += jac[j] * jac[k];
            }
        }
        for (int i = 0; i < 6; ++i)
            for (int j = i + 1; j < 6; ++j)
                h[i][j] = h[j][i];
        bool accepted = false;
        for (int trial = 0; trial < 7; ++trial) {
            double matrix[8][8]{};
            for (int i = 0; i < 8; ++i) {
                for (int j = 0; j < 8; ++j)
                    matrix[i][j] = h[i][j];
                matrix[i][i] += damping * (1. + h[i][i]);
            }
            double step[8]{};
            if (analog_eq_2sos::detail::solve(matrix, gradient, step)) {
                Shape candidate = p;
                for (int j = 0; j < 6; ++j)
                    candidate[j] = analog_eq_2sos::detail::clamp(p[j] + step[j], -10., 10.);
                const double next = shapeCost(candidate, type, fs, center, nodes);
                if (next < previous) {
                    const double delta = previous - next;
                    previous = next;
                    p = candidate;
                    damping = std::max(damping * .3, 1e-12);
                    accepted = true;
                    if (delta < 1e-11)
                        iteration = 50;
                    break;
                }
            }
            damping = std::min(damping * 10., 1e12);
        }
        if (!accepted && damping >= 1e12)
            break;
    }
    const auto sections = shapeSections(p, type, fs, center);
    out.sections[0] = sections[0];
    out.sections[1] = sections[1];
    out.count = 2;
    bool finiteError = true;
    auto inspect = [&](double hz) {
        const double error =
            std::abs(shapeError(sections, type, shapeNode(type, hz, fs, center, q)));
        finiteError &= std::isfinite(error);
        out.maxErrorDb = std::max(out.maxErrorDb, error);
    };
    for (int i = 0; i <= 1024; ++i)
        inspect(20. + (top - 20.) * i / 1024.);
    for (int i = 0; i <= 256; ++i)
        inspect(20. * std::pow(top / 20., i / 256.));
    for (const auto &node : nodes)
        inspect(node.frequency);
    if (center >= 20. && center <= top)
        inspect(center);
    for (const auto &section : sections)
        analog_eq_2sos::detail::criticalFrequencies(section, fs, 20., top, inspect);
    out.valid = finiteError && std::isfinite(out.maxErrorDb);
    if (!isValid(out))
        return {};
    return out;
}
} // namespace detail

// Coefficient calculation uses a bounded nonlinear fit and should run on the
// control thread. Move the resulting POD-like Design safely to the audio
// thread, then call Processor::set() only at a block boundary.
inline Design design(double sampleRate, const Band &band) {
    Design out{};
    const double f = band.frequencyHz, q = band.q, top = band.fitTopHz;
    if (band.type == Type::partialHighPass || !std::isfinite(sampleRate) || !std::isfinite(f) ||
        !std::isfinite(q) || !std::isfinite(top) || !std::isfinite(band.gainDb) ||
        !std::isfinite(band.slopeDbPerOct) || sampleRate < 44100. || f < 20. ||
        f >= sampleRate / 2. || q < .3 || q > 10. || top < 20. || top > sampleRate / 2. ||
        std::abs(band.gainDb) > 24.)
        return out;
    const auto fit = [&](Type type, double freq, double quality, double gain = 0.) {
        return analog_eq_2sos::detail::designPrototype(type, sampleRate, freq, quality, gain, top);
    };
    if (band.type == Type::lowPass || band.type == Type::highPass) {
        if (band.slopeDbPerOct < 12. || band.slopeDbPerOct > 96.)
            return out;
        const double stages = band.slopeDbPerOct / 12.;
        const int whole = static_cast<int>(stages);
        const double fraction = stages - whole;
        std::array<double, 8> quality{};
        // Butterworth pole pairs at integer stage counts. The same formula
        // moves each pole continuously between adjacent integer orders.
        // Spread user resonance over the entire order, so Q at 12 dB/oct
        // remains exactly the value supplied in Band::q.
        const double resonance = std::pow(q / 0.7071067811865476, 1. / stages);
        for (int i = 0; i < whole + (fraction > 1e-12 ? 1 : 0); ++i) {
            const double angle = (2 * i + 1) * analog_eq_2sos::detail::pi / (4. * stages);
            quality[i] = analog_eq_2sos::detail::clamp(resonance / (2. * std::sin(angle)), .3, 10.);
        }
        // Account for resonance when placing the joint -3 dB frequency.
        // Search on the falling side of the analog response near the nominal
        // cutoff. The Butterworth default places it at the requested f.
        auto targetAt = [&](double x) {
            double sum = 0.;
            for (int i = 0; i < whole; ++i)
                sum += analog_eq_2sos::detail::analogDb(band.type, x, 1., quality[i], 0.);
            if (fraction > 1e-12)
                sum += fraction *
                       analog_eq_2sos::detail::analogDb(band.type, x, 1., quality[whole], 0.);
            return sum;
        };
        double lower = 1e-6, upper = 1e6;
        for (int i = 0; i < 64; ++i) {
            const double middle = std::sqrt(lower * upper);
            if ((band.type == Type::lowPass) == (targetAt(middle) > -3.010299956639812))
                lower = middle;
            else
                upper = middle;
        }
        const double sectionF = f / std::sqrt(lower * upper);
        if (sectionF <= 0. || sectionF >= sampleRate / 2.)
            return out;
        for (int i = 0; i < whole; ++i)
            if (!detail::append(out, fit(band.type, sectionF, quality[i])))
                return {};
        if (fraction > 1e-12) {
            const auto partial = analog_eq_2sos::detail::designPrototype(
                band.type == Type::highPass ? Type::partialHighPass : Type::lowPass, sampleRate,
                sectionF, quality[whole], 0., top, fraction);
            if (!detail::append(out, partial))
                return {};
        }
        // Sample the actual total error against the continuously varying
        // analog prototype, including the fractional last pole pair.
        out.maxErrorDb = 0.;
        bool finiteError = true;
        auto inspectFrequency = [&](double hz) {
            double desired = 0., actual = 0.;
            for (int j = 0; j < whole + (fraction > 1e-12 ? 1 : 0); ++j) {
                const auto h = detail::transfer(out.sections[2 * j], hz, sampleRate) *
                               detail::transfer(out.sections[2 * j + 1], hz, sampleRate);
                actual += detail::db(std::abs(h));
                desired +=
                    (j == whole ? fraction : 1.) *
                    analog_eq_2sos::detail::analogDb(band.type, hz, sectionF, quality[j], 0.);
            }
            out.maxErrorDb = std::max(out.maxErrorDb, std::abs(actual - desired));
            finiteError &= std::isfinite(actual) && std::isfinite(desired);
        };
        const double first = band.type == Type::lowPass ? 0. : 20.;
        const double logFirst = band.type == Type::lowPass ? std::min(20., f * .01) : 20.;
        for (int i = 0; i <= 512; ++i)
            inspectFrequency(first + (top - first) * i / 512.);
        for (int i = 0; i <= 256; ++i)
            inspectFrequency(logFirst * std::pow(top / logFirst, i / 256.));
        inspectFrequency(std::min(f, top));
        for (int j = 0; j < whole + (fraction > 1e-12 ? 1 : 0); ++j) {
            for (int i = -8; i <= 8; ++i)
                inspectFrequency(analog_eq_2sos::detail::clamp(
                    sectionF + sectionF / quality[j] * i / 8., first, top));
        }
        for (std::size_t i = 0; i < out.count; ++i)
            analog_eq_2sos::detail::criticalFrequencies(out.sections[i], sampleRate, first, top,
                                                        inspectFrequency);
        if (!finiteError)
            return {};
    } else if (band.type == Type::tilt) {
        if (top <= 20. || f > top)
            return out;
        if (std::abs(band.gainDb) < 1e-10) {
            out.count = 1;
            out.valid = true;
            return out;
        }
        // Eight staggered, decramped shelves approximate a straight line
        // against log frequency. These weights are a least-squares fit to a
        // 12 dB span on a logarithmic grid (20 Hz..20 kHz); normalized center
        // positions make the same shape work for other fit limits.
        constexpr double weights[8] = {2.79960997, .85355834,  1.80385614, 1.40929828,
                                       1.40929828, 1.80385614, .85355834,  2.79960997};
        for (int i = 0; i < 8; ++i) {
            const double center = 20. * std::pow(top / 20., (i + .5) / 8.);
            if (!detail::append(out,
                                fit(Type::highShelf, center, .5, weights[i] * band.gainDb / 12.)))
                return {};
        }
        std::complex<double> atPivot(1., 0.);
        for (std::size_t i = 0; i < out.count; ++i)
            atPivot *= detail::transfer(out.sections[i], f, sampleRate);
        const double correction = 1. / std::abs(atPivot);
        out.sections[0].b0 *= correction;
        out.sections[0].b1 *= correction;
        out.sections[0].b2 *= correction;
        out.maxErrorDb = 0.;
        for (int i = 0; i <= 512; ++i) {
            const double hz = 20. * std::pow(top / 20., i / 512.);
            std::complex<double> h(1., 0.);
            for (std::size_t j = 0; j < out.count; ++j)
                h *= detail::transfer(out.sections[j], hz, sampleRate);
            const double target = band.gainDb * std::log(hz / f) / std::log(top / 20.);
            const double actual = detail::db(std::abs(h));
            if (!std::isfinite(actual) || !std::isfinite(target))
                return {};
            out.maxErrorDb = std::max(out.maxErrorDb, std::abs(actual - target));
        }
    } else if (band.type == Type::bandPass || band.type == Type::notch) {
        if (top <= 20.)
            return out;
        return detail::fitShape(band.type, sampleRate, f, q, top);
    } else {
        if (!detail::append(out, fit(band.type, f, q, band.gainDb)))
            return {};
    }
    out.valid = out.count > 0 && std::isfinite(out.maxErrorDb);
    if (!isValid(out))
        return {};
    return out;
}

// Useful for plotting the digital magnitude that the audio processor actually
// produces. This is a UI/control-thread helper, not part of sample processing.
inline double responseDb(const Design &designResult, double sampleRate, double frequencyHz) {
    if (!isValid(designResult) || !std::isfinite(sampleRate) || !std::isfinite(frequencyHz) ||
        sampleRate <= 0. || frequencyHz < 0. || frequencyHz > sampleRate * .5)
        return std::numeric_limits<double>::quiet_NaN();
    const auto z = std::polar(1., (-2 * analog_eq_2sos::detail::pi) * (frequencyHz / sampleRate));
    double db = 0.;
    for (std::size_t i = 0; i < designResult.count; ++i) {
        const double sectionDb = analog_eq_2sos::detail::sectionDb(designResult.sections[i], z);
        if (sectionDb == -std::numeric_limits<double>::infinity())
            return sectionDb;
        db += sectionDb;
    }
    return db;
}

// Bounded, allocation-free queue: one control-thread producer and one
// audio-thread consumer. A full queue returns false from push (no blocking).
// Drain it once per audio block and pass the last result to SmoothProcessor.
template <std::size_t Capacity = 8> class DesignQueue {
    static_assert(Capacity > 0, "Queue needs positive capacity");
    static_assert(Capacity < std::numeric_limits<std::size_t>::max(),
                  "Queue capacity must leave room for its sentinel slot");
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
                  "A lock-free size_t atomic is required for the audio queue");

  public:
    bool push(const Design &designResult) noexcept {
        if (!isValid(designResult))
            return false;
        const auto write = write_.load(std::memory_order_relaxed);
        const auto next = (write + 1) % (Capacity + 1);
        if (next == read_.load(std::memory_order_acquire))
            return false;
        slots_[write] = designResult;
        write_.store(next, std::memory_order_release);
        return true;
    }
    bool pop(Design &out) noexcept {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire))
            return false;
        out = slots_[read];
        read_.store((read + 1) % (Capacity + 1), std::memory_order_release);
        return true;
    }

  private:
    std::array<Design, Capacity + 1> slots_{};
    std::atomic<std::size_t> write_{0}, read_{0};
};

// Processes Channels planar streams of Sample (float or double). No locks,
// allocations, transcendental functions, or parameter fitting on audio data.
// States use double even when samples use float for low-frequency precision.
template <std::size_t Channels, class Sample = float> class Processor {
    static_assert(Channels > 0, "Processor needs at least one channel");
    static_assert(std::is_floating_point<Sample>::value, "Sample must be float or double");

  public:
    bool set(const Design &d, bool clearHistory = false) noexcept {
        if (!isValid(d))
            return false;
        sections_ = d.sections;
        count_ = d.count;
        if (clearHistory)
            reset();
        return true;
    }

    void reset() noexcept {
        for (auto &channel : state_)
            for (auto &section : channel)
                section = {};
    }

    Sample processSample(std::size_t channel, Sample input) noexcept {
        if (channel >= Channels)
            return input;
        double x = input;
        for (std::size_t i = 0; i < count_; ++i) {
            const auto &c = sections_[i];
            auto &st = state_[channel][i];
            const double y = c.b0 * x + st.z1;
            st.z1 = c.b1 * x - c.a1 * y + st.z2;
            st.z2 = c.b2 * x - c.a2 * y;
            x = y;
        }
        return static_cast<Sample>(x);
    }

    void process(const std::array<Sample *, Channels> &channels, std::size_t samples) noexcept {
        for (std::size_t ch = 0; ch < Channels; ++ch) {
            if (!channels[ch])
                continue;
            for (std::size_t i = 0; i < samples; ++i)
                channels[ch][i] = processSample(ch, channels[ch][i]);
        }
        flushDenormals();
    }

    void flushDenormals() noexcept {
        // Quiet IIR states that are below a very low threshold. Prevents
        // denormals from needlessly consuming CPU after tails have decayed.
        for (auto &channel : state_)
            for (auto &s : channel)
                if (std::abs(s.z1) < 1e-24 && std::abs(s.z2) < 1e-24)
                    s = {};
    }

  private:
    struct State {
        double z1 = 0., z2 = 0.;
    };
    std::array<SOS, maxSections> sections_{};
    std::array<std::array<State, maxSections>, Channels> state_{};
    std::size_t count_ = 0;
};

// Reduces switching clicks by crossfading two running filter paths.
// Design on a control thread; call set() and process() from the same audio
// thread. Changes arriving during a fade are coalesced to the newest Design.
template <std::size_t Channels, class Sample = float> class SmoothProcessor {
  public:
    explicit SmoothProcessor(std::size_t fadeSamples = 480) noexcept
        : fadeSamples_(std::max<std::size_t>(fadeSamples, 1)) {}

    bool set(const Design &d) noexcept {
        if (!isValid(d))
            return false;
        if (initialized_ && sameCoefficients(d, target_))
            return true;
        target_ = d;
        if (!initialized_) {
            active_.set(d, true);
            initialized_ = true;
            return true;
        }
        if (remaining_ != 0) {
            pending_ = d;
            hasPending_ = true;
        } else {
            // A fade may have ended on the previous block's last sample.
            // This newer request supersedes any update still waiting there.
            hasPending_ = false;
            startFade(d);
        }
        return true;
    }

    // Clear audio history and finish any transition at the newest requested
    // target. reset() does not silently revert an accepted coefficient change.
    void reset() noexcept {
        if (hasPending_)
            active_.set(pending_, true);
        else {
            if (remaining_ != 0)
                active_ = incoming_;
            active_.reset();
        }
        incoming_.reset();
        remaining_ = 0;
        hasPending_ = false;
        historyCount_.fill(0);
        historyWrite_.fill(0);
    }

    void process(const std::array<Sample *, Channels> &channels, std::size_t samples) noexcept {
        for (std::size_t i = 0; i < samples; ++i) {
            if (remaining_ == 0 && hasPending_) {
                startFade(pending_);
                hasPending_ = false;
            }
            const double mix =
                remaining_ ? double(fadeSamples_ - remaining_ + 1) / double(fadeSamples_) : 0.;
            for (std::size_t ch = 0; ch < Channels; ++ch) {
                if (!channels[ch])
                    continue;
                const Sample input = channels[ch][i];
                history_[ch][historyWrite_[ch]] = input;
                historyWrite_[ch] = (historyWrite_[ch] + 1) % historySize;
                if (historyCount_[ch] < historySize)
                    ++historyCount_[ch];
                const double before = active_.processSample(ch, input);
                if (remaining_) {
                    const double after = incoming_.processSample(ch, input);
                    channels[ch][i] = static_cast<Sample>(before + mix * (after - before));
                } else
                    channels[ch][i] = static_cast<Sample>(before);
            }
            if (remaining_ && --remaining_ == 0)
                active_ = incoming_;
        }
        active_.flushDenormals();
        if (remaining_)
            incoming_.flushDenormals();
    }

  private:
    static bool sameCoefficients(const Design &a, const Design &b) noexcept {
        if (a.count != b.count)
            return false;
        for (std::size_t i = 0; i < a.count; ++i) {
            const auto &left = a.sections[i];
            const auto &right = b.sections[i];
            if (left.b0 != right.b0 || left.b1 != right.b1 || left.b2 != right.b2 ||
                left.a1 != right.a1 || left.a2 != right.a2)
                return false;
        }
        return true;
    }

    void startFade(const Design &d) noexcept {
        // Reconstruct the new filter's state from recent unprocessed input.
        // Copying transposed-form states between different pole layouts can
        // produce a burst when a section is inserted or removed.
        incoming_.set(d, true);
        for (std::size_t ch = 0; ch < Channels; ++ch) {
            for (std::size_t i = 0; i < historyCount_[ch]; ++i) {
                const auto index =
                    (historyWrite_[ch] + historySize - historyCount_[ch] + i) % historySize;
                incoming_.processSample(ch, history_[ch][index]);
            }
        }
        remaining_ = fadeSamples_;
    }
    Processor<Channels, Sample> active_{};
    Processor<Channels, Sample> incoming_{};
    Design pending_{};
    Design target_{};
    std::size_t fadeSamples_;
    std::size_t remaining_ = 0;
    bool initialized_ = false;
    bool hasPending_ = false;
    static constexpr std::size_t historySize = 2048;
    std::array<std::array<Sample, historySize>, Channels> history_{};
    std::array<std::size_t, Channels> historyWrite_{}, historyCount_{};
};

} // namespace decramp
