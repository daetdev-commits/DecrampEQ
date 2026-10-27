#include <DecrampEQ.hpp>
#include <cmath>
#include <type_traits>

decramp::Design designInOtherTranslationUnit();

int main() {
    static_assert(std::is_trivially_copyable_v<decramp::Design>,
                  "Design must be transferable by value");
    const auto result = designInOtherTranslationUnit();
    if (!decramp::isValid(result))
        return 1;
    decramp::Processor<1> eq;
    if (!eq.set(result, true))
        return 2;
    const auto output = eq.processSample(0, .25f);
    return std::isfinite(output) && decramp::responseDb(result, 48000., 10000.) > 11.9 ? 0 : 3;
}
