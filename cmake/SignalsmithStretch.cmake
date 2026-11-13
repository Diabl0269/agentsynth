# Signalsmith Stretch and Linear, the header-only libraries behind the sound browser's loop stretching. Included from
# SoundBrowserSources.cmake, after Core exists; the pins are in DependencyVersions.cmake.

# Signalsmith Stretch + Linear: header-only. SOURCE_SUBDIR points at a folder that does not exist so
# FetchContent_MakeAvailable only downloads them and never runs their CMake projects (Stretch would fetch a second,
# unpinned copy of Linear); Core gets their include directories below.
FetchContent_Declare(
    signalsmith_stretch
    GIT_REPOSITORY ${SYNTH_SIGNALSMITH_STRETCH_GIT_REPOSITORY}
    GIT_TAG        ${SYNTH_SIGNALSMITH_STRETCH_GIT_TAG}
    GIT_SHALLOW    FALSE
    SOURCE_SUBDIR  no-cmake-project
)
FetchContent_Declare(
    signalsmith_linear
    GIT_REPOSITORY ${SYNTH_SIGNALSMITH_LINEAR_GIT_REPOSITORY}
    GIT_TAG        ${SYNTH_SIGNALSMITH_LINEAR_GIT_TAG}
    GIT_SHALLOW    FALSE
    SOURCE_SUBDIR  no-cmake-project
)
FetchContent_MakeAvailable(signalsmith_stretch signalsmith_linear)

# Signalsmith Stretch's headers (they include "signalsmith-linear/stft.h" from Linear's include directory).
target_include_directories(Core SYSTEM PUBLIC
    ${signalsmith_stretch_SOURCE_DIR}/include
    ${signalsmith_linear_SOURCE_DIR}/include
)
