#include <DecrampEQ.hpp>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace {
std::atomic<bool> tracking{false};
std::atomic<std::size_t> allocations{0};
void record() noexcept {
    if (tracking.load(std::memory_order_relaxed))
        allocations.fetch_add(1, std::memory_order_relaxed);
}
void *allocate(std::size_t size) {
    record();
    if (void *p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void *allocateAligned(std::size_t size, std::size_t alignment) {
    record();
#ifdef _WIN32
    if (void *p = _aligned_malloc(size ? size : 1, alignment))
        return p;
#else
    void *p = nullptr;
    if (posix_memalign(&p, alignment, size ? size : 1) == 0)
        return p;
#endif
    throw std::bad_alloc();
}
void freeAligned(void *p) noexcept {
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}
} // namespace

void *operator new(std::size_t n) {
    return allocate(n);
}
void *operator new[](std::size_t n) {
    return allocate(n);
}
void operator delete(void *p) noexcept {
    std::free(p);
}
void operator delete[](void *p) noexcept {
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void *p, std::size_t) noexcept {
    std::free(p);
}
void *operator new(std::size_t n, std::align_val_t a) {
    return allocateAligned(n, static_cast<std::size_t>(a));
}
void *operator new[](std::size_t n, std::align_val_t a) {
    return allocateAligned(n, static_cast<std::size_t>(a));
}
void operator delete(void *p, std::align_val_t) noexcept {
    freeAligned(p);
}
void operator delete[](void *p, std::align_val_t) noexcept {
    freeAligned(p);
}
void operator delete(void *p, std::size_t, std::align_val_t) noexcept {
    freeAligned(p);
}
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept {
    freeAligned(p);
}

int main() {
    tracking.store(true);
    void *sanity = ::operator new(1);
    ::operator delete(sanity);
    tracking.store(false);
    if (allocations.load() != 1)
        return 1;
    allocations.store(0);

    namespace d = decramp;
    const std::array<d::Type, 8> types{{d::Type::bell, d::Type::lowShelf, d::Type::highShelf,
                                        d::Type::lowPass, d::Type::highPass, d::Type::notch,
                                        d::Type::tilt, d::Type::bandPass}};
    std::array<d::Design, 8> designs{};
    tracking.store(true);
    bool valid = true;
    for (std::size_t i = 0; i < types.size(); ++i) {
        designs[i] = d::design(48000., {types[i], 3000., .7071067811865476, 6., 20000., 42.5});
        valid &= d::isValid(designs[i]);
    }
    tracking.store(false);
    const auto designAllocations = allocations.load();
    allocations.store(0);

    d::Processor<2> direct;
    d::SmoothProcessor<2> smooth(128);
    d::DesignQueue<2> queue;
    std::array<float, 256> left{}, right{};
    bool finite = true;
    tracking.store(true);
    for (const auto &design : designs) {
        valid &= queue.push(design);
        d::Design received;
        valid &= queue.pop(received) && direct.set(received, true) && smooth.set(received);
        for (int block = 0; block < 16; ++block) {
            left.fill(.01f);
            right.fill(-.01f);
            direct.process({left.data(), right.data()}, left.size());
            smooth.process({left.data(), right.data()}, left.size());
            for (float value : left)
                finite &= std::isfinite(value);
        }
        smooth.reset();
        direct.reset();
    }
    tracking.store(false);
    const auto audioAllocations = allocations.load();
    std::cout << "C++ allocations: design=" << designAllocations
              << ", coefficient updates/audio/reset/queue=" << audioAllocations << '\n';
    return valid && finite && designAllocations == 0 && audioAllocations == 0 ? 0 : 2;
}
