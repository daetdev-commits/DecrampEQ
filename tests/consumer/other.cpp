#include <DecrampEQ.hpp>

decramp::Design designInOtherTranslationUnit() {
    return decramp::design(48000., {decramp::Type::bell, 10000., .7071067811865476, 12.});
}
