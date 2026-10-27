#include <DecrampEQ.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iomanip>
#include <iostream>
#include <limits>

namespace d = decramp;

namespace {
bool independentlyStable(const d::SOS &section) {
    // Find the denominator roots directly, independently of isValid's test.
    const auto radical =
        std::sqrt(std::complex<double>(section.a1 * section.a1 - 4. * section.a2, 0.));
    return std::abs((-section.a1 + radical) * .5) < 1. &&
           std::abs((-section.a1 - radical) * .5) < 1.;
}
} // namespace

int main() {
    int attempted = 0, available = 0;
    double worstReportedError = 0.;
    d::Band worstBand;
    double worstRate = 0.;
    for (double rate : {44100., 48000., 96000., 192000.}) {
        for (auto type : {d::Type::bell, d::Type::lowShelf, d::Type::highShelf, d::Type::lowPass,
                          d::Type::highPass, d::Type::notch, d::Type::tilt, d::Type::bandPass}) {
            for (double frequency : {20., 1000., rate * .45, std::nextafter(rate * .5, 0.)}) {
                for (double q : {.3, .7071067811865476, 10.}) {
                    for (double setting : {-24., 0., 24.}) {
                        d::Band band{type, frequency, q, setting};
                        if (type == d::Type::lowPass || type == d::Type::highPass) {
                            band.gainDb = 0.;
                            band.slopeDbPerOct = setting < 0. ? 12. : setting == 0. ? 42.5 : 96.;
                        }
                        const auto design = d::design(rate, band);
                        ++attempted;
                        if (!design.valid)
                            continue;
                        ++available;
                        if (!d::isValid(design)) {
                            std::cerr << "Designer returned an unusable Design\n";
                            return 1;
                        }
                        for (std::size_t i = 0; i < design.count; ++i) {
                            if (!independentlyStable(design.sections[i])) {
                                const auto &s = design.sections[i];
                                std::cerr << std::setprecision(17) << "Unstable poles at rate "
                                          << rate << ", frequency " << frequency << ", type "
                                          << int(type) << ", q " << q << ", setting " << setting
                                          << ", section " << i << ", a1 " << s.a1 << ", a2 " << s.a2
                                          << '\n';
                                return 1;
                            }
                        }
                        for (double hz : {0., 20., 1000., rate * .5}) {
                            if (std::isnan(d::responseDb(design, rate, hz))) {
                                std::cerr << "Valid design has undefined response\n";
                                return 1;
                            }
                        }
                        d::Processor<1, double> audio;
                        audio.set(design, true);
                        for (int sample = 0; sample < 512; ++sample) {
                            const double y = audio.processSample(0, sample == 0 ? 1. : 0.);
                            if (!std::isfinite(y)) {
                                std::cerr << "Valid design has non-finite impulse output\n";
                                return 1;
                            }
                        }
                        if (design.maxErrorDb > worstReportedError) {
                            worstReportedError = design.maxErrorDb;
                            worstBand = band;
                            worstRate = rate;
                        }
                    }
                }
            }
        }
    }

    for (double bad :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity()}) {
        for (auto member : {&d::Band::frequencyHz, &d::Band::q, &d::Band::gainDb,
                            &d::Band::fitTopHz, &d::Band::slopeDbPerOct}) {
            d::Band band;
            band.*member = bad;
            if (d::design(48000., band).valid)
                return 2;
        }
        if (d::design(bad, {}).valid)
            return 2;
    }
    for (auto type :
         {static_cast<d::Type>(-1), static_cast<d::Type>(999), d::Type::partialHighPass})
        if (d::design(48000., {type}).valid)
            return 2;

    std::cout << available << '/' << attempted << " parameter-grid designs available; "
              << "all returned coefficients have stable poles and finite impulse output.\n"
              << "Worst sampled fit error in this extreme grid: " << worstReportedError
              << " dB (availability is not an accuracy guarantee).\n"
              << "Worst case: rate " << worstRate << ", type " << int(worstBand.type)
              << ", frequency " << worstBand.frequencyHz << ", Q " << worstBand.q << ", gain "
              << worstBand.gainDb << ", slope " << worstBand.slopeDbPerOct << '\n';
    return available > 0 ? 0 : 3;
}
