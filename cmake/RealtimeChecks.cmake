# Compile-time audio-thread check (FRO453). A function marked AS_NONBLOCKING (after its noexcept) must not
# allocate, lock, do I/O or call anything unmarked that the compiler cannot see into. Clang 20+ (Apple clang 17+)
# checks the claim with -Wfunction-effects and, as an error in the Core target, names the offending line. Other
# compilers (gcc, MSVC, older clang) define AS_NONBLOCKING as nothing, so the marked headers still build and the
# check simply does not run there. The macro is a compile definition, not a header, so the leaf kernels that use
# it do not all gain one more include (scripts/check-header-reach.sh).
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-Werror=function-effects" AGENTSYNTH_HAS_FUNCTION_EFFECTS)
if(AGENTSYNTH_HAS_FUNCTION_EFFECTS)
    target_compile_definitions(Core PUBLIC "AS_NONBLOCKING=__attribute__((nonblocking))")
    target_compile_options(Core PRIVATE -Wfunction-effects -Werror=function-effects)
else()
    target_compile_definitions(Core PUBLIC "AS_NONBLOCKING=")
endif()

# Run-time audio-thread check (FRO2187). With -DAGENTSYNTH_RTSAN=ON the binaries are built with clang's
# RealtimeSanitizer (clang 20+, runtime in libclang-rt), and synth::RealtimeScope (AudioEngine::renderNextBlock,
# the one funnel for the device callback and the hosted processBlock) makes any allocation, lock or I/O on the
# render thread abort with its call stack. Off by default; only the label-gated run-rtsan CI job turns it on.
option(AGENTSYNTH_RTSAN "Build with clang RealtimeSanitizer (clang 20+) and arm it in AudioEngine::renderNextBlock" OFF)
if(AGENTSYNTH_RTSAN)
    include(CMakePushCheckState)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=realtime")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=realtime")
    check_cxx_compiler_flag("-fsanitize=realtime" AGENTSYNTH_HAS_RTSAN)
    cmake_pop_check_state()
    if(NOT AGENTSYNTH_HAS_RTSAN)
        message(FATAL_ERROR "AGENTSYNTH_RTSAN=ON needs clang 20+ with the RealtimeSanitizer runtime (-fsanitize=realtime)")
    endif()
    target_compile_definitions(Core PUBLIC AGENTSYNTH_RTSAN=1)
    target_compile_options(Core PUBLIC -fsanitize=realtime -fno-omit-frame-pointer)
    target_link_options(Core PUBLIC -fsanitize=realtime)
endif()
