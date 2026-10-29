# Fetches and adds JUCE (pin and patches: cmake/DependencyVersions.cmake). Included by the root CMakeLists.txt.

include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY ${SYNTH_JUCE_GIT_REPOSITORY}
    GIT_TAG        ${SYNTH_JUCE_GIT_TAG}
    GIT_SHALLOW    TRUE
    PATCH_COMMAND  ${SYNTH_JUCE_PATCH_COMMAND}
)
# JUCE passes the C and C++ launchers on to its helper-tool configure as one unquoted -D argument,
# which splits a launcher list (cmake/Pch.cmake's `cmake -E env ... ccache`) into stray arguments and
# fails the configure. JUCE therefore sees the plain ccache path while it is added, and our own
# targets (created after this) get the full launcher back.
set(_synth_launcher_c "${CMAKE_C_COMPILER_LAUNCHER}")
set(_synth_launcher_cxx "${CMAKE_CXX_COMPILER_LAUNCHER}")
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()
FetchContent_MakeAvailable(juce)
set(CMAKE_C_COMPILER_LAUNCHER "${_synth_launcher_c}")
set(CMAKE_CXX_COMPILER_LAUNCHER "${_synth_launcher_cxx}")
