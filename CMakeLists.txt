cmake_minimum_required(VERSION 3.16)
project(DecrampEQ VERSION 0.2.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(DecrampEQ INTERFACE)
add_library(DecrampEQ::DecrampEQ ALIAS DecrampEQ)
target_include_directories(DecrampEQ INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(DecrampEQ INTERFACE cxx_std_17)

option(DECRAMPEQ_BUILD_EXAMPLE "Build small command-line example" OFF)
option(DECRAMPEQ_BUILD_TESTS "Build validation test" OFF)
option(DECRAMPEQ_BUILD_BENCHMARKS "Build standalone timing benchmark" OFF)
option(DECRAMPEQ_WARNINGS_AS_ERRORS "Treat project executable warnings as errors" OFF)
option(DECRAMPEQ_ENABLE_SANITIZERS "Instrument executables (MSVC: ASan; GCC/Clang: ASan+UBSan)" OFF)

# Private build checks must not leak into consumers of the INTERFACE target.
function(decramp_configure_executable target)
    target_link_libraries(${target} PRIVATE DecrampEQ::DecrampEQ)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(DECRAMPEQ_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        if(DECRAMPEQ_ENABLE_SANITIZERS)
            target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
            target_link_options(${target} PRIVATE /INCREMENTAL:NO /DEBUG)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(DECRAMPEQ_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(DECRAMPEQ_ENABLE_SANITIZERS)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    elseif(DECRAMPEQ_ENABLE_SANITIZERS)
        message(FATAL_ERROR "Sanitizers require MSVC, GCC, or Clang")
    endif()
endfunction()

if(MSVC AND DECRAMPEQ_ENABLE_SANITIZERS)
    string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

if(DECRAMPEQ_BUILD_BENCHMARKS)
    add_executable(decramp_benchmark benchmarks/benchmark.cpp)
    decramp_configure_executable(decramp_benchmark)
endif()

if(DECRAMPEQ_BUILD_EXAMPLE)
    add_executable(decramp_example examples/basic.cpp)
    decramp_configure_executable(decramp_example)
    add_executable(decramp_realtime_example examples/realtime.cpp)
    decramp_configure_executable(decramp_realtime_example)
endif()

if(DECRAMPEQ_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    foreach(name IN ITEMS eq contracts design_grid fractional boundaries realtime)
        add_executable(decramp_test_${name} tests/test_${name}.cpp)
        decramp_configure_executable(decramp_test_${name})
        add_test(NAME decramp_${name} COMMAND decramp_test_${name})
        set_tests_properties(decramp_${name} PROPERTIES TIMEOUT 60)
    endforeach()
    target_link_libraries(decramp_test_contracts PRIVATE Threads::Threads)
    if(DECRAMPEQ_BUILD_EXAMPLE)
        add_test(NAME decramp_basic_example COMMAND decramp_example)
        add_test(NAME decramp_realtime_example COMMAND decramp_realtime_example)
    endif()
    add_test(NAME decramp_package_consumer
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            "-DGENERATOR=${CMAKE_GENERATOR}"
            "-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DTOOLSET=${CMAKE_GENERATOR_TOOLSET}"
            "-DCOMPILER=${CMAKE_CXX_COMPILER}"
            "-DCOMPILER_ARG1=${CMAKE_CXX_COMPILER_ARG1}"
            "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            "-DCONFIG=$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/check_consumer.cmake")
    set_tests_properties(decramp_package_consumer PROPERTIES TIMEOUT 180)
endif()

set(DECRAMPEQ_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/DecrampEQ")
configure_package_config_file(cmake/DecrampEQConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/DecrampEQConfig.cmake"
    INSTALL_DESTINATION "${DECRAMPEQ_CMAKE_INSTALL_DIR}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/DecrampEQConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion ARCH_INDEPENDENT)
install(FILES include/DecrampEQ.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(TARGETS DecrampEQ EXPORT DecrampEQTargets)
install(EXPORT DecrampEQTargets NAMESPACE DecrampEQ::
        DESTINATION "${DECRAMPEQ_CMAKE_INSTALL_DIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/DecrampEQConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/DecrampEQConfigVersion.cmake"
    DESTINATION "${DECRAMPEQ_CMAKE_INSTALL_DIR}")
install(FILES LICENSE README.md CHANGELOG.md THIRD_PARTY.md DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(FILES README.ja.md CONTRIBUTING.md DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(DIRECTORY docs/ DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs")
install(DIRECTORY examples/ DESTINATION "${CMAKE_INSTALL_DOCDIR}/examples")
