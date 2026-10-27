#include <DecrampEQ.hpp>
#include <array>
#include <cmath>
#include <iostream>

class RealtimeBand {
  public:
    // Worker/control thread only. Acceptance and delivery are separate:
    // a full queue retains the newest accepted design for the next flush.
    bool request(double rate, const decramp::Band &band) {
        const auto result = decramp::design(rate, band);
        if (!decramp::isValid(result) || result.maxErrorDb > .1)
            return false;
        pending_ = result;
        hasPending_ = true;
        flush();
        return true;
    }
    void flush() {
        if (hasPending_ && updates_.push(pending_))
            hasPending_ = false;
    }

    // Audio thread only. Keep this object alive across blocks.
    void process(float *left, float *right, std::size_t samples) noexcept {
        decramp::Design received, latest;
        bool found = false;
        while (updates_.pop(received)) {
            latest = received;
            found = true;
        }
        if (found)
            eq_.set(latest);
        eq_.process({left, right}, samples);
    }

  private:
    decramp::DesignQueue<2> updates_;
    decramp::SmoothProcessor<2> eq_{480}; // 10 ms at the example's 48 kHz rate.
    decramp::Design pending_{};           // Control-thread state only.
    bool hasPending_ = false;
};

int main() {
    // Drive the two roles sequentially for this device-independent example.
    // In a host, request()/flush() belong to one control thread and process()
    // belongs to its single audio consumer.
    RealtimeBand eq;
    for (double gain : {2., 4., 6.})
        if (!eq.request(48000., {decramp::Type::bell, 3000., .7071067811865476, gain}))
            return 1;
    std::array<float, 128> left{}, right{};
    for (int block = 0; block < 32; ++block) {
        eq.flush();
        left.fill(.01f);
        right.fill(-.01f);
        eq.process(left.data(), right.data(), left.size());
        for (float sample : left)
            if (!std::isfinite(sample))
                return 2;
    }
    std::cout << "Worker retry and newest-update audio processing completed\n";
}
