#include <DecrampEQ.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

namespace d = decramp;

namespace {
int failures = 0;
void check(bool passed, const char *message) {
    if (!passed) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

d::Design gainDesign(double gain) {
    d::Design result;
    result.valid = true;
    result.count = 1;
    result.sections[0].b0 = gain;
    return result;
}

void rejectedDesign(const d::Design &invalid) {
    d::Processor<1> direct;
    direct.set(gainDesign(.5));
    check(!direct.set(invalid), "Processor rejects malformed designs");
    check(direct.processSample(0, 1.f) == .5f,
          "rejected design preserves Processor coefficients and state");

    d::SmoothProcessor<1> fresh;
    check(!fresh.set(invalid), "SmoothProcessor rejects malformed first design");
    d::SmoothProcessor<1> smooth(8);
    smooth.set(gainDesign(.5));
    check(!smooth.set(invalid), "SmoothProcessor rejects malformed update");
    std::array<float, 16> audio;
    audio.fill(1.f);
    smooth.process({audio.data()}, audio.size());
    check(std::all_of(audio.begin(), audio.end(), [](float x) { return x == .5f; }),
          "rejected design does not start a fade or change the output");

    d::DesignQueue<2> queue;
    check(!queue.push(invalid), "queue rejects malformed designs");
    d::Design received;
    check(!queue.pop(received), "rejected design leaves the queue empty");
}

void malformedDesigns() {
    d::Design invalid;
    rejectedDesign(invalid);
    invalid = gainDesign(1.);
    invalid.count = d::maxSections + 1;
    rejectedDesign(invalid);
    invalid = gainDesign(1.);
    invalid.maxErrorDb = -1.;
    rejectedDesign(invalid);

    for (double bad :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity()}) {
        for (auto member : {&d::SOS::b0, &d::SOS::b1, &d::SOS::b2, &d::SOS::a1, &d::SOS::a2}) {
            invalid = gainDesign(1.);
            invalid.sections[0].*member = bad;
            rejectedDesign(invalid);
        }
        invalid = gainDesign(1.);
        invalid.maxErrorDb = bad;
        rejectedDesign(invalid);
    }
    for (const auto &pole :
         std::array<std::array<double, 2>, 4>{{{2., 0.}, {0., 1.}, {1.5, .5}, {-1.5, .5}}}) {
        invalid = gainDesign(1.);
        invalid.sections[0].a1 = pole[0];
        invalid.sections[0].a2 = pole[1];
        rejectedDesign(invalid);
    }
}

void resetKeepsLatestTarget() {
    for (bool pending : {false, true}) {
        d::SmoothProcessor<1> smooth(256);
        smooth.set(gainDesign(.5));
        smooth.set(gainDesign(2.));
        if (pending)
            smooth.set(gainDesign(3.));
        smooth.reset();
        std::array<float, 16> audio;
        audio.fill(1.f);
        smooth.process({audio.data()}, audio.size());
        const float expected = pending ? 3.f : 2.f;
        check(
            std::all_of(audio.begin(), audio.end(), [expected](float x) { return x == expected; }),
            "reset clears state while retaining the most recent requested coefficients");
    }
}

void unityAndResponse() {
    d::Design unity;
    unity.valid = true;
    check(d::isValid(unity) && d::responseDb(unity, 48000., 1000.) == 0.,
          "a valid zero-section design is unity");
    auto custom = gainDesign(.5);
    custom.sections[1].b0 = std::numeric_limits<double>::quiet_NaN();
    check(d::isValid(custom), "inactive coefficient slots are not part of the design");
    check(std::abs(d::responseDb(custom, 48000., 1000.) + 6.020599913) < 1.e-8,
          "custom stable coefficients have the expected response");
    custom.sections[0].a1 = 2.;
    check(std::isnan(d::responseDb(custom, 48000., 1000.)),
          "responseDb reports an invalid design as NaN");
    for (double frequency : {-1., 24001., std::numeric_limits<double>::quiet_NaN()})
        check(std::isnan(d::responseDb(unity, 48000., frequency)),
              "responseDb rejects frequencies outside its domain");

    for (double gain : {1.e-100, 1.e100}) {
        auto cascade = gainDesign(gain);
        cascade.count = d::maxSections;
        for (auto &section : cascade.sections)
            section.b0 = gain;
        const double expected = d::maxSections * 20. * std::log10(gain);
        check(std::abs(d::responseDb(cascade, 48000., 1000.) - expected) < 1.e-8,
              "responseDb does not underflow or overflow a finite dB result");
    }
    auto mixed = gainDesign(1.);
    mixed.count = d::maxSections;
    for (std::size_t i = 0; i < d::maxSections; ++i)
        mixed.sections[i].b0 = i < d::maxSections / 2 ? 1.e100 : 1.e-100;
    check(std::abs(d::responseDb(mixed, 48000., 1000.)) < 1.e-8,
          "large opposite section gains cancel in dB without an intermediate infinity");
    auto huge = gainDesign(1.e308);
    huge.sections[0].b1 = 1.e308;
    check(std::abs(d::responseDb(huge, 48000., 0.) - (6160. + 20. * std::log10(2.))) < 1.e-8,
          "responseDb scales the numerator before complex arithmetic");
    check(std::abs(d::responseDb(gainDesign(.5), 1.e308, 4.e307) + 6.020599913) < 1.e-8,
          "responseDb computes angular frequency without overflowing its inputs");
    check(d::responseDb(gainDesign(0.), 48000., 1000.) == -std::numeric_limits<double>::infinity(),
          "zero transfer remains negative infinity");
}

void disconnectedChannel() {
    const auto before = d::design(48000., {d::Type::lowPass, 100.});
    const auto after = d::design(48000., {d::Type::lowPass, 200.});
    check(before.valid && after.valid, "channel-history test designs are available");
    d::SmoothProcessor<1> reference(256);
    d::SmoothProcessor<2> stereo(256);
    reference.set(before);
    stereo.set(before);
    std::array<float, 256> mono{}, left{}, right{};
    mono.fill(.25f);
    right.fill(.25f);
    reference.process({mono.data()}, mono.size());
    stereo.process({left.data(), right.data()}, right.size());

    // A null pointer freezes that channel, just as Processor::process does.
    // Advancing the other channel must not advance or overwrite its history.
    stereo.process({left.data(), nullptr}, 137);
    reference.set(after);
    stereo.set(after);
    mono.fill(.25f);
    right.fill(.25f);
    reference.process({mono.data()}, mono.size());
    stereo.process({left.data(), right.data()}, right.size());
    double worst = 0.;
    for (std::size_t i = 0; i < mono.size(); ++i)
        worst = std::max(worst, std::abs(double(mono[i]) - right[i]));
    std::cout << "Disconnected-channel deviation: " << worst << '\n';
    check(worst < 1.e-7, "reconnected channel retains its own input history");
}

void threadedQueue() {
    constexpr int updates = 20000;
    d::DesignQueue<8> queue;
    std::thread producer([&] {
        for (int sequence = 1; sequence <= updates; ++sequence) {
            auto value = gainDesign(sequence);
            value.maxErrorDb = sequence;
            value.sections[0].b1 = -sequence;
            while (!queue.push(value))
                std::this_thread::yield();
        }
    });
    bool intact = true;
    for (int sequence = 1; sequence <= updates; ++sequence) {
        d::Design received;
        while (!queue.pop(received))
            std::this_thread::yield();
        intact &= received.count == 1 && received.maxErrorDb == sequence &&
                  received.sections[0].b0 == sequence && received.sections[0].b1 == -sequence;
    }
    producer.join();
    check(intact, "SPSC queue transfers complete designs in order across threads");
}

void repeatedIdenticalDesign() {
    const auto coefficients = d::design(192000., {d::Type::lowPass, 20.});
    check(d::isValid(coefficients), "unchanged-design regression has valid coefficients");
    d::SmoothProcessor<1, double> smooth(64);
    d::Processor<1, double> reference;
    smooth.set(coefficients);
    reference.set(coefficients, true);
    std::array<double, 128> audio{}, expected{};
    for (int block = 0; block < 3000; ++block) {
        audio.fill(.25);
        expected.fill(.25);
        smooth.process({audio.data()}, audio.size());
        reference.process({expected.data()}, expected.size());
    }
    double worst = 0.;
    for (int block = 0; block < 64; ++block) {
        auto duplicate = coefficients;
        // Accuracy metadata and inactive slots do not change audio transfer.
        duplicate.maxErrorDb += block * .001;
        duplicate.sections[d::maxSections - 1].b0 = block;
        check(smooth.set(duplicate), "a repeated valid design is accepted");
        audio.fill(.25);
        expected.fill(.25);
        smooth.process({audio.data()}, audio.size());
        reference.process({expected.data()}, expected.size());
        for (std::size_t i = 0; i < audio.size(); ++i)
            worst = std::max(worst, std::abs(audio[i] - expected[i]));
    }
    std::cout << "Repeated-identical-design deviation: " << worst << '\n';
    check(worst < 1.e-10,
          "repeated coefficients do not replay truncated history or disturb a settled filter");
}

void newestDesignAtBlockBoundary() {
    d::SmoothProcessor<1> smooth(8);
    smooth.set(gainDesign(1.));
    smooth.set(gainDesign(2.));
    smooth.set(gainDesign(3.));
    std::array<float, 8> first{};
    first.fill(1.f);
    smooth.process({first.data()}, first.size());
    // The fade has finished exactly at the block end, while 3x is pending.
    // A newer 4x target arrives before the next process() call.
    smooth.set(gainDesign(4.));
    std::array<float, 32> audio{};
    audio.fill(1.f);
    smooth.process({audio.data()}, audio.size());
    check(audio.back() == 4.f, "new block-boundary target supersedes the older pending design");
}
} // namespace

int main() {
    malformedDesigns();
    disconnectedChannel();
    resetKeepsLatestTarget();
    unityAndResponse();
    threadedQueue();
    repeatedIdenticalDesign();
    newestDesignAtBlockBoundary();
    std::cout << failures << " contract failures\n";
    return failures == 0 ? 0 : 1;
}
