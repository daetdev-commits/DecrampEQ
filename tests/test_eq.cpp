#include <DecrampEQ.hpp>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>

namespace d = decramp;
constexpr double pi = 3.14159265358979323846;

double analogDb(d::Type t, double f, double fc, double q, double gain) {
    const std::complex<double> s(0., f / fc);
    const double A = std::pow(10., gain / 40.);
    const auto beta = std::sqrt(A) / q;
    std::complex<double> h;
    switch (t) {
    case d::Type::bell:
        h = (s * s + (A / q) * s + 1.) / (s * s + s / (A * q) + 1.);
        break;
    case d::Type::lowShelf:
        h = A * (s * s + beta * s + A) / (A * s * s + beta * s + 1.);
        break;
    case d::Type::highShelf:
        h = A * (A * s * s + beta * s + 1.) / (s * s + beta * s + A);
        break;
    case d::Type::lowPass:
        h = 1. / (s * s + s / q + 1.);
        break;
    default:
        h = s * s / (s * s + s / q + 1.);
    }
    return 20. * std::log10(std::abs(h));
}

bool require(bool value, const char *description) {
    if (!value)
        std::cerr << "FAIL: " << description << '\n';
    return value;
}

int main() {
    bool ok = true;
    constexpr double fs = 48000.;
    constexpr double q = 0.7071067811865476;
    for (auto t : {d::Type::bell, d::Type::lowShelf, d::Type::highShelf, d::Type::lowPass,
                   d::Type::highPass}) {
        const double gain = (t == d::Type::lowPass || t == d::Type::highPass) ? 0. : 12.;
        auto result = d::design(fs, {t, 10000., q, gain});
        ok &= require(result.valid, "valid 48k design");
        double maxError = 0.;
        for (int i = 0; i <= 4000; i++) {
            const double f = 20. + 19980. * i / 4000.;
            const double error =
                std::abs(d::responseDb(result, fs, f) - analogDb(t, f, 10000., q, gain));
            maxError = std::max(maxError, error);
        }
        ok &= require(maxError < 0.005, "digital magnitude matches analog");
        for (const auto &s : result.sections)
            ok &= require(s.a2 < 1. && std::abs(s.a1) < 1. + s.a2, "each section has stable poles");
    }

    // Stream audio through both sections, and compare the measured tone gain
    // to the transfer-function result. Check that channel history is separate.
    auto bell = d::design(fs, {d::Type::bell, 10000., q, 12.});
    d::Processor<2, float> processor;
    processor.set(bell, true);
    double inputEnergy = 0., outputEnergy = 0., rightEnergy = 0.;
    constexpr int frames = 16384, block = 128;
    for (int n = 0; n < frames; n += block) {
        std::array<float, block> left{}, right{};
        for (int i = 0; i < block; ++i)
            left[i] = static_cast<float>(std::sin(2 * pi * 8000. * (n + i) / fs));
        processor.process({left.data(), right.data()}, block);
        if (n >= frames / 2)
            for (int i = 0; i < block; ++i) {
                const double input = std::sin(2 * pi * 8000. * (n + i) / fs);
                inputEnergy += input * input;
                outputEnergy += double(left[i]) * left[i];
                rightEnergy += double(right[i]) * right[i];
            }
    }
    const double measured = 10 * std::log10(outputEnergy / inputEnergy);
    ok &= require(std::abs(measured - d::responseDb(bell, fs, 8000.)) < 0.005,
                  "actual processed tone gain matches coefficients");
    ok &= require(rightEnergy == 0., "independent stereo state");

    auto bypass = d::design(fs, {d::Type::bell, 10000., q, 0.});
    processor.set(bypass, true);
    ok &= require(processor.processSample(0, .25f) == .25f, "zero-gain bell is a wire");
    auto highpass = d::design(fs, {d::Type::highPass, 1000., q});
    ok &=
        require(std::isinf(d::responseDb(highpass, fs, 0.)) && d::responseDb(highpass, fs, 0.) < 0.,
                "high-pass DC zero is exact");
    ok &= require(!d::design(32000., {d::Type::bell, 1000., q, 12.}).valid,
                  "unsupported sample rate is rejected");

    // Continuous steep slopes: measured response must match the returned
    // sections, including the partial section, and remain continuous at 24.
    for (auto type : {d::Type::lowPass, d::Type::highPass}) {
        for (double slope : {12., 18., 24., 36., 48., 72., 96.}) {
            auto steep = d::design(fs, {type, 3000., q, 0., 20000., slope});
            ok &= require(steep.valid && steep.count <= 16,
                          "continuous slope designs up to 96 dB/oct");
            ok &= require(std::abs(d::responseDb(steep, fs, 3000.) + 3.01029996) < .1,
                          "cutoff stays close to -3 dB");
            ok &= require(steep.maxErrorDb < .1, "finite-band fractional slope fits analog target");
            if (slope == 12. || slope == 24. || slope == 48. || slope == 96.) {
                const double octave = type == d::Type::lowPass ? 6000. : 1500.;
                const double order = slope / 6.;
                const double ideal = -10. * std::log10(1. + std::pow(2., 2. * order));
                ok &= require(std::abs(d::responseDb(steep, fs, octave) - ideal) < .02,
                              "integer Butterworth slope at one octave");
            }
            for (std::size_t j = 0; j < steep.count; ++j) {
                const auto &s = steep.sections[j];
                ok &=
                    require(s.a2 < 1. && std::abs(s.a1) < 1. + s.a2, "steep filter pole stability");
            }
        }
        auto a = d::design(fs, {type, 3000., q, 0., 20000., 23.999});
        auto b = d::design(fs, {type, 3000., q, 0., 20000., 24.});
        ok &= require(std::abs(d::responseDb(a, fs, 1000.) - d::responseDb(b, fs, 1000.)) < .02,
                      "fractional stage joins integer stage continuously");
    }
    ok &= require(!d::design(fs, {d::Type::lowPass, 3000., q, 0., 20000., 97.}).valid,
                  "slopes above 96 are rejected");
    auto notch = d::design(fs, {d::Type::notch, 10000., q});
    ok &= require(notch.valid && notch.count == 2 && d::responseDb(notch, fs, 10000.) < -180. &&
                      notch.maxErrorDb < .01,
                  "notch center null and sampled analog error");
    auto tilt = d::design(fs, {d::Type::tilt, 3000., q, 12.});
    ok &= require(tilt.valid && tilt.count == 16 && tilt.maxErrorDb < .15,
                  "log-frequency tilt approximates a straight line");
    for (double hz : {20., 100., 300., 1000., 3000., 8000., 18000., 20000.}) {
        const double target = 12. * std::log(hz / 3000.) / std::log(20000. / 20.);
        ok &= require(std::abs(d::responseDb(tilt, fs, hz) - target) < .15,
                      "tilt points follow log-frequency line");
    }
    auto bp = d::design(fs, {d::Type::bandPass, 3000., q});
    ok &= require(bp.valid && bp.count == 2 && std::abs(d::responseDb(bp, fs, 3000.)) < .001 &&
                      d::responseDb(bp, fs, 100.) < -15. && d::responseDb(bp, fs, 18000.) < -15.,
                  "bandpass center and rejected skirts");
    // The high-frequency designs must track independent analog prototypes.
    for (auto t : {d::Type::notch, d::Type::bandPass}) {
        auto high = d::design(fs, {t, 18000., q});
        ok &= require(high.valid && high.maxErrorDb < .05,
                      "high notch/bandpass design fits through 20 kHz");
        const double ratio = std::sqrt(1. + 1. / (4. * q * q));
        const double lo = 18000. * (ratio - 1. / (2. * q));
        const double hi = 18000. * (ratio + 1. / (2. * q));
        const double center = analogDb(d::Type::highPass, 18000., lo, q, 0.) +
                              analogDb(d::Type::lowPass, 18000., hi, q, 0.);
        for (double hz : {1000., 8000., 14000., 16000., 19000., 20000.}) {
            const std::complex<double> s(0., hz / 18000.);
            const double target =
                t == d::Type::notch
                    ? 20. * std::log10(std::abs((s * s + 1.) / (s * s + s / q + 1.)))
                    : analogDb(d::Type::highPass, hz, lo, q, 0.) +
                          analogDb(d::Type::lowPass, hz, hi, q, 0.) - center;
            ok &= require(std::abs(d::responseDb(high, fs, hz) - target) < .05,
                          "high notch/bandpass matches analog sample");
        }
    }

    // Complex transfer should be continuous across all stage count borders.
    const auto transfer = [&](const d::Design &design, double hz) {
        const auto z = std::polar(1., -2 * pi * hz / fs);
        std::complex<double> h(1., 0.);
        for (std::size_t j = 0; j < design.count; ++j) {
            const auto &s = design.sections[j];
            h *= (s.b0 + s.b1 * z + s.b2 * z * z) / (1. + s.a1 * z + s.a2 * z * z);
        }
        return h;
    };
    for (auto t : {d::Type::lowPass, d::Type::highPass})
        for (double slope : {24., 36., 48., 60., 72., 84.}) {
            auto before = d::design(fs, {t, 3000., q, 0., 20000., slope - .001});
            auto at = d::design(fs, {t, 3000., q, 0., 20000., slope});
            auto after = d::design(fs, {t, 3000., q, 0., 20000., slope + .001});
            for (double hz : {1000., 3000., 6000., 12000., 20000.})
                ok &= require(std::abs(transfer(before, hz) - transfer(at, hz)) < .005 &&
                                  std::abs(transfer(after, hz) - transfer(at, hz)) < .005,
                              "complex response joins across stage borders");
        }

    // Send a 96 dB/oct design through the actual sample processor.
    auto steep = d::design(fs, {d::Type::lowPass, 3000., q, 0., 20000., 96.});
    processor.set(steep, true);
    double steepInput = 0., steepOutput = 0.;
    for (int n = 0; n < 16384; ++n) {
        const float in = static_cast<float>(std::sin(2 * pi * 6000. * n / fs));
        const float out = processor.processSample(0, in);
        if (n > 8192) {
            steepInput += double(in) * in;
            steepOutput += double(out) * out;
        }
    }
    ok &= require(std::abs(10. * std::log10(steepOutput / steepInput) -
                           d::responseDb(steep, fs, 6000.)) < .02,
                  "16-section audio agrees with coefficient response");

    d::DesignQueue<2> queue;
    ok &= require(queue.push(bell) && queue.push(bypass), "SPSC queue accepts two updates");
    ok &= require(!queue.push(bell), "full queue returns false");
    d::Design received;
    ok &= require(queue.pop(received) && received.maxErrorDb == bell.maxErrorDb,
                  "queue preserves first update");
    ok &= require(queue.push(highpass), "queue wraps around safely");
    ok &= require(queue.pop(received) && received.maxErrorDb == bypass.maxErrorDb,
                  "queue preserves second update");
    ok &= require(queue.pop(received) && received.maxErrorDb == highpass.maxErrorDb,
                  "queue preserves wrapped update");
    ok &= require(!queue.pop(received), "empty queue returns false");

    // Crossfade a change, send two more while the fade is running, and check
    // that the last pending change finishes without NaNs or large overshoots.
    d::SmoothProcessor<2, float> smooth(240);
    auto base = d::design(fs, {d::Type::lowShelf, 300., q, 0.});
    auto boost = d::design(fs, {d::Type::lowShelf, 300., q, 6.});
    auto newest = d::design(fs, {d::Type::lowShelf, 300., q, 12.});
    smooth.set(base);
    smooth.set(boost);
    std::array<float, 128> l{}, r{};
    l.fill(1.f);
    smooth.process({l.data(), r.data()}, 128);
    smooth.set(base);
    smooth.set(newest);
    for (int blockIndex = 0; blockIndex < 100; ++blockIndex) {
        l.fill(1.f);
        smooth.process({l.data(), r.data()}, l.size());
        for (float v : l)
            ok &= require(std::isfinite(v) && v > 0. && v < 5.,
                          "crossfade remains finite and bounded");
    }
    ok &= require(std::abs(l.back() - std::pow(10., 12. / 20.)) < 0.001,
                  "last pending smooth change takes effect");
    // Adding and removing SOS while a low-pass is passing DC must not
    // inject a burst from stale transposed-form states.
    d::SmoothProcessor<1, float> swept(256);
    swept.set(d::design(fs, {d::Type::lowPass, 3000., q, 0., 20000., 24.}));
    std::array<float, 256> constant{};
    for (int i = 0; i < 16; ++i) {
        constant.fill(1.f);
        swept.process({constant.data()}, constant.size());
    }
    for (double slope : {23.9, 24., 24.1, 35.9, 36., 36.1, 48., 96., 48., 24.}) {
        swept.set(d::design(fs, {d::Type::lowPass, 3000., q, 0., 20000., slope}));
        constant.fill(1.f);
        swept.process({constant.data()}, constant.size());
        for (float value : constant)
            ok &= require(std::abs(value - 1.f) < .005f,
                          "changing section count does not create a DC burst");
    }
    std::cout << (ok ? "All DecrampEQ checks passed\n" : "DecrampEQ failed\n");
    return ok ? 0 : 1;
}
