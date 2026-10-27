#include <DecrampEQ.hpp>
#include <array>
#include <cmath>
#include <iostream>

namespace d = decramp;

int main() {
    // Independently evaluated analog reference for the old worst case:
    // 192 kHz, 20 Hz low-pass, Q=.3, 42.5 dB/oct. The cascaded prototype is
    // normalized to -3.0102999566 dB at the requested cutoff.
    constexpr std::array<std::array<double, 2>, 10> reference{{{1., -.0104302942466},
                                                               {5., -.254371994482},
                                                               {10., -.946878469957},
                                                               {20., -3.01029995664},
                                                               {30., -5.23681011381},
                                                               {50., -16.6145699169},
                                                               {100., -54.7488391242},
                                                               {1000., -193.798176474},
                                                               {10000., -335.430019507},
                                                               {20000., -378.075670806}}};
    constexpr double rate = 192000.;
    const auto coefficients = d::design(rate, {d::Type::lowPass, 20., .3, 0., 20000., 42.5});
    if (!d::isValid(coefficients) || coefficients.maxErrorDb > .1) {
        std::cerr << "Low-cutoff fractional design failed its accuracy bound\n";
        return 1;
    }
    for (const auto &point : reference) {
        if (std::abs(d::responseDb(coefficients, rate, point[0]) - point[1]) > .08) {
            std::cerr << "Analog-reference mismatch at " << point[0] << " Hz\n";
            return 2;
        }
    }
    for (int i = 0; i <= 512; ++i) {
        const double hz = .1 * std::pow(200., i / 512.);
        if (d::responseDb(coefficients, rate, hz) > .01) {
            std::cerr << "Spurious subsonic gain at " << hz << " Hz\n";
            return 3;
        }
    }
    const auto defaultQ =
        d::design(rate, {d::Type::lowPass, 20., .7071067811865476, 0., 20000., 42.5});
    if (!defaultQ.valid || defaultQ.maxErrorDb > .15)
        return 3;
    for (int i = 0; i <= 512; ++i) {
        const double hz = .1 * std::pow(100., i / 512.);
        if (d::responseDb(defaultQ, rate, hz) > .05) {
            std::cerr << "Default-Q fractional stage has a subsonic peak\n";
            return 3;
        }
    }

    // Verify the actual streaming path, rather than only the plot helper.
    d::Processor<1, double> processor;
    processor.set(coefficients, true);
    double inputEnergy = 0., outputEnergy = 0.;
    for (int sample = 0; sample < 384000; ++sample) {
        const double input = .1 * std::sin(2. * 3.14159265358979323846 * 20. * sample / rate);
        const double output = processor.processSample(0, input);
        if (sample >= 192000) {
            inputEnergy += input * input;
            outputEnergy += output * output;
        }
    }
    const double measured = 10. * std::log10(outputEnergy / inputEnergy);
    if (std::abs(measured - reference[3][1]) > .03) {
        std::cerr << "Processed 20 Hz tone differs from the analog cutoff: " << measured << '\n';
        return 4;
    }
    std::cout << "Low-cutoff fractional error " << coefficients.maxErrorDb
              << " dB; processed 20 Hz gain " << measured << " dB\n";

    // A tiny extra order must approach the neighboring integer response.
    // This also catches the old narrow high-pass notch and subsonic LP peak.
    for (double fs : {44100., 48000., 96000., 192000.}) {
        for (auto type : {d::Type::lowPass, d::Type::highPass}) {
            const double cutoff = type == d::Type::highPass ? 20. : 40.;
            const double quality = type == d::Type::highPass ? .7071067811865476 : 10.;
            const double slope = type == d::Type::highPass ? 24. : 12.;
            const auto integer = d::design(fs, {type, cutoff, quality, 0., 20000., slope});
            const auto adjacent = d::design(fs, {type, cutoff, quality, 0., 20000., slope + .001});
            if (!integer.valid || !adjacent.valid) {
                std::cerr << "Integer-boundary design unavailable at " << fs << " Hz, type "
                          << int(type) << '\n';
                return 5;
            }
            const double first = type == d::Type::highPass ? 20. : .1;
            double difference = 0.;
            for (int i = 0; i <= 2048; ++i) {
                const double hz = first * std::pow(20000. / first, i / 2048.);
                difference = std::max(difference, std::abs(d::responseDb(adjacent, fs, hz) -
                                                           d::responseDb(integer, fs, hz)));
            }
            if (difference > .06) {
                std::cerr << "Fractional boundary differs by " << difference << " dB at " << fs
                          << " Hz, type " << int(type) << '\n';
                return 6;
            }
        }
    }
}
