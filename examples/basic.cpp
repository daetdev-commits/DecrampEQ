#include <DecrampEQ.hpp>
#include <array>
#include <iostream>

int main() {
    constexpr double fs = 48000.;
    decramp::Band band{decramp::Type::bell, 10000., 0.70710678, 12.};
    auto coefficients = decramp::design(fs, band);
    if (!coefficients.valid)
        return 1;

    // For a plug-in, run design() on the control thread. The queue transfers
    // the completed Design to the audio thread without locking or allocation.
    decramp::DesignQueue<8> updates;
    if (!updates.push(coefficients))
        return 2;
    decramp::SmoothProcessor<2> eq(480); // 10 ms at 48 kHz
    decramp::Design incoming;
    if (updates.pop(incoming))
        eq.set(incoming);

    std::array<float, 256> left{}, right{};
    left[0] = right[0] = 1.f; // Stereo impulse, purely for this example.
    eq.process({left.data(), right.data()}, left.size());
    std::cout << "Peak design error through 20 kHz: " << coefficients.maxErrorDb << " dB\n";
    std::cout << "Gain at 20 kHz: " << decramp::responseDb(coefficients, fs, 20000.) << " dB\n";
    std::cout << "First output sample: " << left[0] << '\n';
}
