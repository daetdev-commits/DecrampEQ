#include <DecrampEQ.hpp>
#include <cmath>
#include <complex>
#include <iostream>

namespace d = decramp;

double analogNotch(double hz, double center, double q) {
    const std::complex<double> s(0., hz / center);
    return 20. * std::log10(std::abs((s * s + 1.) / (s * s + s / q + 1.)));
}

int main() {
    int available = 0;
    double worstCutoff = 0., worstError = 0.;
    for (double rate : {44100., 48000., 96000., 192000.}) {
        for (auto type : {d::Type::lowPass, d::Type::highPass}) {
            for (double q : {.3, .7071067811865476, 10.}) {
                for (double slope : {12., 42.5, 96.}) {
                    const auto design = d::design(rate, {type, 20., q, 0., 20000., slope});
                    if (!d::isValid(design)) {
                        std::cerr << "20 Hz design unavailable: rate " << rate << ", type "
                                  << int(type) << ", Q " << q << ", slope " << slope << '\n';
                        return 1;
                    }
                    ++available;
                    const double cutoffError =
                        std::abs(d::responseDb(design, rate, 20.) + 3.01029995664);
                    worstCutoff = std::max(worstCutoff, cutoffError);
                    worstError = std::max(worstError, design.maxErrorDb);
                    if (cutoffError > .2 || design.maxErrorDb > .5) {
                        std::cerr << "20 Hz design misses the normalized cutoff or fit tolerance\n";
                        return 2;
                    }
                }
            }
        }
        // A requested 19 Hz cutoff remains out of range for both public APIs.
        if (d::design(rate, {d::Type::lowPass, 19.}).valid ||
            analog_eq_2sos::design(d::Type::lowPass, rate, 19., .7).valid)
            return 3;

        for (double center : {20., 1000., 18000.}) {
            for (double q : {.3, .7071067811865476, 10.}) {
                const double top = center == 20. ? 20.001 : 20000.;
                const auto design = d::design(rate, {d::Type::notch, center, q, 0., top});
                if (!d::isValid(design) || !std::isfinite(design.maxErrorDb) ||
                    design.maxErrorDb > .02) {
                    std::cerr << "Notch boundary fit failed\n";
                    return 4;
                }
                // Compare actual coefficients to an independent analog
                // transfer at points on the notch shoulder and near center.
                for (double offset : {-.1, -.01, -.001, .000025, .001, .01, .1}) {
                    const double hz = center * (1. + offset);
                    if (hz < 20. || hz > top)
                        continue;
                    const double error =
                        std::abs(d::responseDb(design, rate, hz) - analogNotch(hz, center, q));
                    if (!std::isfinite(error) || error > design.maxErrorDb + .003) {
                        std::cerr << "Notch shoulder mismatch at " << hz << " Hz, error " << error
                                  << '\n';
                        return 5;
                    }
                }
            }
        }
    }
    std::cout << available << "/72 low-boundary designs available; worst cutoff deviation "
              << worstCutoff << " dB, worst sampled fit " << worstError << " dB\n";
}
