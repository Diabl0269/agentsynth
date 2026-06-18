cmake_minimum_required(VERSION 3.22)

project(Gravisynth VERSION 0.13.2)

# Enable ccache if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch JUCE
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.3 # Using a specific tag for stability
)
FetchContent_MakeAvailable(juce)

# Coverage Option
option(ENABLE_COVERAGE "Enable code coverage testing" OFF)
option(ENABLE_TESTS "Enable building test suite" OFF)

if(ENABLE_COVERAGE)
    if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
        add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
        add_link_options(-fprofile-instr-generate -fcoverage-mapping)
    elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
        add_compile_options(--coverage)
        add_link_options(--coverage)
    endif()
endif()

# Create the core library
add_library(GravisynthCore STATIC
    Source/AudioEngine.cpp
    Source/AudioEngine.h
    Source/GravisynthUndoManager.cpp
    Source/GravisynthUndoManager.h
    Source/PresetManager.cpp
    Source/PresetManager.h
    # Modules
    Source/Modules/ModuleBase.h
    Source/Modules/OscillatorModule.h
    Source/Modules/ADSRModule.h
    Source/Modules/FilterModule.h
    Source/Modules/VCAModule.h
    Source/Modules/VoiceMixerModule.h
    Source/Modules/SequencerModule.h
    Source/Modules/MidiKeyboardModule.h
    Source/Modules/LFOModule.h
    Source/Modules/AttenuverterModule.h
    Source/Modules/VisualBuffer.h
    Source/Modules/FX/DelayModule.h
    Source/Modules/FX/DistortionModule.h
    Source/Modules/FX/ReverbModule.h
    Source/Modules/FX/ChorusModule.h
    Source/Modules/FX/PhaserModule.h
    Source/Modules/FX/CompressorModule.h
    Source/Modules/FX/FlangerModule.h
    Source/Modules/FX/LimiterModule.h
    # AI
    Source/AI/AIProvider.h
    Source/AI/OllamaProvider.h
    Source/AI/AIIntegrationService.cpp
    Source/AI/OllamaProvider.cpp
    Source/AI/AIIntegrationService.h
    Source/AI/AIStateMapper.cpp
    Source/AI/AIStateMapper.h
    # UI utilities
    Source/UI/LayoutUtil.h
    Source/UI/LayoutUtil.cpp
    Source/UI/StatusBarComponent.h
    Source/UI/StatusBarComponent.cpp
    Source/UI/ToolbarComponent.h
    Source/UI/ToolbarComponent.cpp
    # Theme system
    Source/UI/Theme/Theme.h
    Source/UI/Theme/ThemeManager.h
    Source/UI/Theme/ThemeManager.cpp
    Source/UI/Theme/ThemeLoader.h
    Source/UI/Theme/ThemeLoader.cpp
    Source/UI/Theme/BuiltInThemes.h
    Source/UI/Theme/BuiltInThemes.cpp
    Source/UI/Theme/IconLibrary.h
    Source/UI/Theme/IconLibrary.cpp
    Source/UI/Theme/GravisynthLookAndFeel.h
    Source/UI/Theme/GravisynthLookAndFeel.cpp
)

# Link Core against JUCE
target_link_libraries(GravisynthCore PUBLIC
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
    juce::juce_data_structures
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_dsp
)

target_compile_definitions(GravisynthCore PUBLIC JUCE_WEB_BROWSER=0)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

    find_package(CURL REQUIRED)

    # Link to Core so JUCE modules compiled within it find the headers
    target_link_libraries(GravisynthCore PUBLIC ${GTK3_LIBRARIES} CURL::libcurl)
    target_include_directories(GravisynthCore PUBLIC ${GTK3_INCLUDE_DIRS})
endif()

target_include_directories(GravisynthCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

if(MSVC)
    target_compile_options(GravisynthCore PRIVATE /W4)
else()
    target_compile_options(GravisynthCore PRIVATE -Wall -Wextra)
endif()

# Create the application target
juce_add_gui_app(Gravisynth
    PRODUCT_NAME "Gravisynth"
    VERSION "0.13.2"
    BUNDLE_ID "com.gravisynth.app"
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "We need Bluetooth access to connect to your external MIDI devices."
)

juce_generate_juce_header(Gravisynth)

# Embed font files as binary data (SIL OFL licensed — see assets/fonts/LICENSES.md).
# GravisynthLookAndFeel (compiled into GravisynthCore) loads these typefaces from BinaryData,
# so the asset library + GRAVISYNTH_HAS_FONT_ASSETS define are attached to Core (PUBLIC) below;
# the app and the test target both link Core and therefore resolve the BinaryData symbols.
# Guard: icon filenames must use hyphens only (JUCE strips hyphens entirely, so 'a-b.svg' becomes
# BinaryData::ab_svg; underscores in filenames would collide with the separator).
file(GLOB ICON_FILES "${CMAKE_SOURCE_DIR}/assets/icons/*.svg")
foreach(f ${ICON_FILES})
    get_filename_component(fname "${f}" NAME_WE)
    string(FIND "${fname}" "_" ul_pos)
    if(NOT ${ul_pos} EQUAL -1)
        message(FATAL_ERROR "Icon filename '${fname}' contains underscore — use hyphens only.")
    endif()
endforeach()

juce_add_binary_data(GravisynthAssets SOURCES
    assets/fonts/Inter-Regular.ttf
    assets/fonts/Inter-Medium.ttf
    assets/fonts/Inter-SemiBold.ttf
    assets/fonts/Inter-Bold.ttf
    assets/fonts/JetBrainsMono-Regular.ttf
    assets/fonts/JetBrainsMono-Medium.ttf
    assets/fonts/Manrope-Regular.ttf
    assets/fonts/Manrope-Medium.ttf
    assets/fonts/Manrope-SemiBold.ttf
    assets/fonts/Manrope-Bold.ttf
    assets/fonts/SpaceMono-Regular.ttf
    assets/fonts/SpaceMono-Bold.ttf
    assets/fonts/IBMPlexSans-Regular.ttf
    assets/fonts/IBMPlexSans-Medium.ttf
    assets/fonts/IBMPlexSans-SemiBold.ttf
    assets/fonts/IBMPlexMono-Regular.ttf
    assets/fonts/IBMPlexMono-Medium.ttf
    # Icons (22 SVGs, hyphen-named; BinaryData symbols: e.g. transport_play_svg)
    assets/icons/transport-play.svg
    assets/icons/transport-stop.svg
    assets/icons/action-undo.svg
    assets/icons/action-redo.svg
    assets/icons/action-save.svg
    assets/icons/action-load.svg
    assets/icons/action-settings.svg
    assets/icons/action-auto-arrange.svg
    assets/icons/toggle-ai.svg
    assets/icons/toggle-matrix.svg
    assets/icons/toggle-library.svg
    assets/icons/module-bypass.svg
    assets/icons/module-mute.svg
    assets/icons/module-delete.svg
    assets/icons/cat-sources.svg
    assets/icons/cat-sequencing.svg
    assets/icons/cat-envelopes.svg
    assets/icons/cat-filters.svg
    assets/icons/cat-modulation-fx.svg
    assets/icons/cat-time-fx.svg
    assets/icons/cat-dynamics.svg
    assets/icons/cat-utility.svg
    # Waveform glyphs (Phase 4 — 4 SVGs, BinaryData symbols: waveformsine_svg etc.)
    assets/icons/waveform-sine.svg
    assets/icons/waveform-saw.svg
    assets/icons/waveform-square.svg
    assets/icons/waveform-triangle.svg
)

# The themed LookAndFeel lives in GravisynthCore and pulls typefaces from BinaryData;
# attach the asset lib + feature define to Core (PUBLIC) so the app AND the test target
# (both link Core) compile the font path and resolve the BinaryData symbols.
target_link_libraries(GravisynthCore PUBLIC GravisynthAssets)
target_compile_definitions(GravisynthCore PUBLIC GRAVISYNTH_HAS_FONT_ASSETS=1)

# Add source files
target_sources(Gravisynth PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MainComponent.h
    # UI (UI depends on Core, but technically could be in App for now)
    Source/UI/AIChatComponent.cpp
    Source/UI/GraphEditor.cpp
    Source/UI/GraphEditor.h
    Source/UI/ModuleComponent.cpp
    Source/UI/ModuleComponent.h
    Source/UI/FrequencyResponseComponent.h
    Source/UI/ModMatrixComponent.cpp
    Source/UI/ModMatrixComponent.h
    Source/UI/SettingsWindow.cpp
    Source/UI/SettingsWindow.h
    Source/UI/AppearanceSettingsTab.cpp
    Source/UI/AppearanceSettingsTab.h
    Source/ShortcutManager.h
)

# Link against JUCE modules
target_link_libraries(Gravisynth PRIVATE
    GravisynthCore # Link to our core library
    GravisynthAssets # Embedded font binary data
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
    juce::juce_data_structures
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
)

target_compile_definitions(Gravisynth PRIVATE
    JUCE_WEB_BROWSER=0
    GRAVISYNTH_HAS_FONT_ASSETS=1
)

if(UNIX AND NOT APPLE)
    target_link_libraries(Gravisynth PRIVATE ${GTK3_LIBRARIES})
    target_include_directories(Gravisynth PRIVATE ${GTK3_INCLUDE_DIRS})
endif()

# Enable strict warnings to catch issues early
if(MSVC)
    target_compile_options(Gravisynth PRIVATE /W4)
else()
    target_compile_options(Gravisynth PRIVATE -Wall -Wextra)
endif()

if(ENABLE_TESTS)
    add_subdirectory(Tests)
endif()

