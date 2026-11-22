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
