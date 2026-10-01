# Precompiled JUCE headers for the three targets that hold almost every translation unit: Core,
# AppUI and Tests. A source file here spends most of its compile time parsing the JUCE module
# headers; a precompiled header parses them once per target instead of once per file.
# Measurements and the history of the earlier attempts: docs/development/ci-pipeline.md.
#
# Included right after the ccache launcher block in the root CMakeLists.txt, BEFORE any target
# exists, because a target copies CMAKE_<LANG>_COMPILER_LAUNCHER when it is created.
#
# OFF by default. The macOS and Linux CI jobs turn it on. It stays off for local builds because
# clang writes the build directory's absolute path into the header, so the file is only valid at
# the path that built it, and the local ccache is shared between checkouts
# (docs/development/local-ci.md). It stays off for the Windows job because files that use the
# header never hit ccache there (measured; see docs/development/ci-pipeline.md).
option(AGENTSYNTH_PCH "Precompile the JUCE module headers for Core, AppUI and Tests" OFF)

# A ccache base_dir makes two checkouts hash the header's compile identically, so the second one
# is handed the first one's file and every compile that uses it then fails with "malformed or
# corrupted precompiled file: could not find file <the other checkout's path>". Refuse the
# combination at configure time instead of failing a thousand compiles later.
if(AGENTSYNTH_PCH AND CCACHE_PROGRAM)
    execute_process(COMMAND "${CCACHE_PROGRAM}" --get-config base_dir
                    OUTPUT_VARIABLE _synth_ccache_base_dir OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_synth_ccache_base_dir)
        message(FATAL_ERROR
            "AGENTSYNTH_PCH=ON cannot be combined with a ccache base_dir "
            "(currently '${_synth_ccache_base_dir}'): a precompiled header is only valid at the "
            "path that built it, and base_dir lets another checkout's copy be served from the "
            "cache. Configure with -DAGENTSYNTH_PCH=OFF, or clear base_dir "
            "(see cmake/Pch.cmake).")
    endif()
endif()

# ccache refuses to cache a compile that uses a precompiled header unless told that the header's
# own #defines and the time macros may be ignored. Set through the launcher's environment so every
# checkout, worktree and CI job gets it from the build itself: a ccache.conf that lacks it would
# not fail, it would silently stop hitting. The two include_file_* entries repeat what the CI jobs
# already configure, since the environment variable replaces the configured list.
if(AGENTSYNTH_PCH AND CCACHE_PROGRAM)
    set(_synth_pch_launcher
        "${CMAKE_COMMAND}" -E env
        CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime
        "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER ${_synth_pch_launcher})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${_synth_pch_launcher})
    if(APPLE)
        set(CMAKE_OBJC_COMPILER_LAUNCHER ${_synth_pch_launcher})
        set(CMAKE_OBJCXX_COMPILER_LAUNCHER ${_synth_pch_launcher})
    endif()
endif()

# The modules the sources include by name (cmake/JuceModules.cmake links them), except juce_dsp:
# its jmin/jmax overloads for SIMDRegister turn `juce::jmin<juce::int64> (a, b)` into a hard error
# on Linux, where int64 is `long long` and SIMDNativeOps has no specialisation for it. Only the
# files that use juce_dsp include it, as before.
set(SYNTH_PCH_JUCE_HEADERS
    juce_core/juce_core.h
    juce_events/juce_events.h
    juce_graphics/juce_graphics.h
    juce_data_structures/juce_data_structures.h
    juce_audio_basics/juce_audio_basics.h
    juce_audio_devices/juce_audio_devices.h
    juce_audio_formats/juce_audio_formats.h
    juce_audio_processors/juce_audio_processors.h
    juce_audio_utils/juce_audio_utils.h
    juce_gui_basics/juce_gui_basics.h
    juce_gui_extra/juce_gui_extra.h
    juce_animation/juce_animation.h
)

# synth_enable_pch(<target> [extra angle-bracket headers...])
#
# C++ only: an Objective-C++ file would need a second, separately built header, and the few .mm
# files in these targets are compiled with -fobjc-arc, which clang refuses to mix with a header
# precompiled without it. The app and plugin targets are left out for the same reason and because
# each holds a handful of files, fewer than a precompiled header costs to build and cache.
function(synth_enable_pch target)
    if(NOT AGENTSYNTH_PCH)
        return()
    endif()
    foreach(header IN LISTS SYNTH_PCH_JUCE_HEADERS ARGN)
        target_precompile_headers(${target} PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:<${header}$<ANGLE-R>>")
    endforeach()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Without this clang stamps each included file's modification time into the header, so a
        # fresh checkout of identical sources produces a different file and ccache never hits.
        target_compile_options(${target} PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-Xclang -fno-pch-timestamp>")
    endif()
endfunction()
