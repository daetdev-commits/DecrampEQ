#include <DecrampEQ.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>

namespace d = decramp;
using Clock = std::chrono::steady_clock;
constexpr std::size_t trials = 41;
volatile double checksum = 0.;

void report(const char *name, std::array<double, trials> times, double error, int valid) {
    double mean = 0.;
    for (double time : times)
        mean += time / trials;
    std::sort(times.begin(), times.end());
    std::cout << name << ',' << times[trials / 2] << ',' << mean << ',' << error << ',' << valid
              << '\n';
}

int main() {
    struct Case {
        const char *name;
        double rate;
        d::Band band;
    };
    const std::array<Case, 9> cases{
        {{"bell_10k", 48000., {d::Type::bell, 10000., .7071067811865476, 12.}},
         {"low_shelf_10k", 48000., {d::Type::lowShelf, 10000., .7071067811865476, 12.}},
         {"high_shelf_10k", 48000., {d::Type::highShelf, 10000., .7071067811865476, 12.}},
         {"low_pass_96db", 48000., {d::Type::lowPass, 3000., .7071067811865476, 0., 20000., 96.}},
         {"high_pass_42_5db",
          48000.,
          {d::Type::highPass, 3000., .7071067811865476, 0., 20000., 42.5}},
         {"notch_18k", 48000., {d::Type::notch, 18000., 3.}},
         {"band_pass_18k", 48000., {d::Type::bandPass, 18000., 1.}},
         {"tilt", 48000., {d::Type::tilt, 1000., .7071067811865476, 12.}},
         {"low_pass_20hz_fractional", 192000., {d::Type::lowPass, 20., .3, 0., 20000., 42.5}}}};
    std::cout << std::fixed << std::setprecision(6)
              << "case,median_us,mean_us,max_error_db,valid_trials\n";
    for (const auto &item : cases) {
        for (int i = 0; i < 3; ++i)
            checksum += d::design(item.rate, item.band).maxErrorDb;
        std::array<double, trials> times{};
        double error = 0.;
        int valid = 0;
        for (std::size_t i = 0; i < trials; ++i) {
            auto band = item.band;
            band.frequencyHz *= 1. + double(i) * .00001;
            const auto start = Clock::now();
            const auto result = d::design(item.rate, band);
            times[i] = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
            checksum += result.sections[0].b0;
            error = std::max(error, result.maxErrorDb);
            valid += result.valid ? 1 : 0;
        }
        report(item.name, times, error, valid);
        if (valid != static_cast<int>(trials))
            return 1;
    }

    const auto coefficients =
        d::design(48000., {d::Type::lowPass, 3000., .7071067811865476, 0., 20000., 96.});
    d::SmoothProcessor<2> eq(64);
    eq.set(coefficients);
    std::array<float, 128> left{}, right{};
    for (int block = 0; block < 32; ++block) {
        left.fill(.1f);
        right.fill(.1f);
        eq.process({left.data(), right.data()}, left.size());
    }
    std::array<double, trials> times{};
    for (std::size_t i = 0; i < trials; ++i) {
        left.fill(.1f);
        right.fill(.1f);
        const auto start = Clock::now();
        eq.set(coefficients);
        eq.process({left.data(), right.data()}, left.size());
        times[i] = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
        checksum += left.back();
    }
    report("repeat_same_update_96db", times, coefficients.maxErrorDb, int(trials));
}
