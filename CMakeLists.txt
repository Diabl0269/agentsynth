cmake_minimum_required(VERSION 3.22)

project(AgentSynth VERSION 0.13.2)

# Enable ccache if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependency pins live in one file so CI can key its build/_deps cache on that file alone —
# adding a source file below must not invalidate a ~350 MB dependency cache. See the header of
# cmake/DependencyVersions.cmake for the full rationale.
include(${CMAKE_CURRENT_LIST_DIR}/cmake/DependencyVersions.cmake)

# Fetch JUCE
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY ${SYNTH_JUCE_GIT_REPOSITORY}
    GIT_TAG        ${SYNTH_JUCE_GIT_TAG}
)
FetchContent_MakeAvailable(juce)

# Coverage Option
option(ENABLE_COVERAGE "Enable code coverage testing" OFF)
option(ENABLE_TESTS "Enable building test suite" OFF)
option(ENABLE_AI_HARNESS "Build the AI patch validity/quality measurement harnesses (needs a live Ollama)" OFF)
# ON by default: the plugin ships in every release alongside the standalone app, and building it
# in PR CI is what stops the shared UI/engine code from silently breaking the plugin wrapper.
# Turn OFF for a faster local iteration loop when you're only working on the standalone app.
option(ENABLE_PLUGIN "Build the VST3 (+ AU on macOS) audio-plugin targets" ON)

# Product identity — the single place to override branding at configure time
# (e.g. `cmake -S . -B build -DSYNTH_PRODUCT_NAME=MySynth`). Mirrored in C++ at
# Source/Branding.h, which JUCE's PRODUCT_NAME/COMPANY_NAME/BUNDLE_ID args can't read
# directly since they're consumed at configure time, not compile time.
set(SYNTH_PRODUCT_NAME "Agent Synth" CACHE STRING "Display name: window title, About box, installer")
set(SYNTH_COMPANY_NAME "yourcompany" CACHE STRING "JUCE COMPANY_NAME / macOS bundle vendor")
set(SYNTH_BUNDLE_ID "com.agentsynth.app" CACHE STRING "macOS/iOS CFBundleIdentifier")

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
add_library(Core STATIC
    Source/AudioEngine.cpp
    Source/AudioEngine.h
    Source/AppUndoManager.cpp
    Source/AppUndoManager.h
    Source/Branding.h
    Source/PresetManager.cpp
    Source/PresetManager.h
    Source/SnippetManager.cpp
    Source/SnippetManager.h
    Source/SettingsMigration.cpp
    Source/SettingsMigration.h
    # Modules
    Source/Modules/ModuleBase.h
    Source/Modules/OscillatorModule.h
    Source/Modules/ADSRModule.h
    Source/Modules/FilterModule.h
    Source/Modules/VCAModule.h
    Source/Modules/VoiceMixerModule.h
    Source/Modules/NoiseModule.h
    Source/Modules/MathModule.h
    Source/Modules/SamplerModule.h
    Source/Modules/WavetableOscillatorModule.h
    Source/Modules/SequencerModule.h
    Source/Modules/MidiKeyboardModule.h
    Source/Modules/LFOModule.h
    Source/Modules/SampleHoldModule.h
    Source/Modules/AttenuverterModule.h
    Source/Modules/EnvelopeFollowerModule.h
    Source/Modules/MacroControlModule.h
    Source/Modules/VisualBuffer.h
    Source/Modules/FX/DelayModule.h
    Source/Modules/FX/DistortionModule.h
    Source/Modules/FX/ReverbModule.h
    Source/Modules/FX/ChorusModule.h
    Source/Modules/FX/PhaserModule.h
    Source/Modules/FX/CompressorModule.h
    Source/Modules/FX/FlangerModule.h
    Source/Modules/FX/LimiterModule.h
    Source/Modules/FX/BitcrusherModule.h
    Source/Modules/FX/PitchShifterModule.h
    Source/Modules/FX/ParametricEQModule.h
    Source/Modules/NoiseModule.h
    # AI
    Source/AI/AIProvider.h
    Source/AI/OllamaProvider.h
    Source/AI/RemoteProvider.h
    Source/AI/AIIntegrationService.cpp
    Source/AI/OllamaProvider.cpp
    Source/AI/RemoteProvider.cpp
    Source/AI/AIIntegrationService.h
    Source/AI/AIStateMapper.cpp
    Source/AI/AIStateMapper.h
    Source/AI/AIProviderRegistry.h
    Source/AI/AIProviderRegistry.cpp
    Source/AI/PatchEval.h
    Source/AI/PatchEval.cpp
    Source/AI/AuthClient.h
    Source/AI/AuthClient.cpp
    Source/AI/AccountService.h
    Source/AI/AccountService.cpp
    # Auth (device-code sign-in: token storage, no UI)
    Source/Auth/TokenStore.h
    Source/Auth/InMemoryTokenStore.h
    Source/Auth/KeychainTokenStore.h
    Source/Auth/KeychainTokenStore.cpp
    Source/Auth/DeviceIdStore.h
    Source/Auth/DeviceIdStore.cpp
    # UI utilities
    Source/UI/LayoutUtil.h
    Source/UI/LayoutUtil.cpp
    Source/UI/SelectionModel.h
    Source/UI/StatusBarComponent.h
    Source/UI/StatusBarComponent.cpp
    Source/UI/ToolbarComponent.h
    Source/UI/ToolbarComponent.cpp
    Source/UI/MinimapComponent.h
    Source/UI/MinimapComponent.cpp
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
    Source/UI/Theme/AppLookAndFeel.h
    Source/UI/Theme/AppLookAndFeel.cpp
)

# Link Core against JUCE
target_link_libraries(Core PUBLIC
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
    juce::juce_animation
    juce::juce_dsp
)

target_compile_definitions(Core PUBLIC JUCE_WEB_BROWSER=0)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

    # Link to Core so JUCE modules compiled within it find the headers
    target_link_libraries(Core PUBLIC ${GTK3_LIBRARIES})
    target_include_directories(Core PUBLIC ${GTK3_INCLUDE_DIRS})
endif()

# RemoteProvider (Source/AI/RemoteProvider.cpp) needs libcurl. Required on macOS and Linux (the
# macOS SDK ships a curl.tbd stub, so find_package(CURL) succeeds there with no Homebrew
# dependency) but deliberately NOT on WIN32 — the windows-latest CI job
# (.github/workflows/ci.yml) has no libcurl setup step, and RemoteProvider's Windows branch is a
# stub that never calls into curl.
if(NOT WIN32)
    find_package(CURL REQUIRED)
    target_link_libraries(Core PUBLIC CURL::libcurl)
endif()

# KeychainTokenStore (Source/Auth/KeychainTokenStore.cpp) needs Security.framework's SecItem* C
# API on macOS; every other platform delegates to an in-memory fallback and needs nothing linked.
if(APPLE)
    target_link_libraries(Core PUBLIC "-framework Security")
endif()

target_include_directories(Core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

if(MSVC)
    target_compile_options(Core PRIVATE /W4)
else()
    target_compile_options(Core PRIVATE -Wall -Wextra)
endif()

# Create the application target
juce_add_gui_app(AgentSynth
    PRODUCT_NAME "${SYNTH_PRODUCT_NAME}"
    VERSION "0.13.2"
    COMPANY_NAME "${SYNTH_COMPANY_NAME}"
    BUNDLE_ID "${SYNTH_BUNDLE_ID}"
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "We need Bluetooth access to connect to your external MIDI devices."
)

juce_generate_juce_header(AgentSynth)

# Embed font files as binary data (SIL OFL licensed — see assets/fonts/LICENSES.md).
# AppLookAndFeel (compiled into Core) loads these typefaces from BinaryData,
# so the asset library + HAS_FONT_ASSETS define are attached to Core (PUBLIC) below;
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

juce_add_binary_data(Assets SOURCES
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
    # Icons (23 SVGs, hyphen-named; BinaryData symbols: e.g. transport_play_svg)
    assets/icons/transport-play.svg
    assets/icons/transport-stop.svg
    assets/icons/action-undo.svg
    assets/icons/action-redo.svg
    assets/icons/action-new.svg
    assets/icons/action-save.svg
    assets/icons/action-load.svg
    assets/icons/action-settings.svg
    assets/icons/action-auto-arrange.svg
    assets/icons/toggle-ai.svg
    assets/icons/toggle-matrix.svg
    assets/icons/toggle-library.svg
    assets/icons/theme-toggle.svg
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
    # Minimap toggle (issue #159).
    assets/icons/toggle-minimap.svg
)

# The themed LookAndFeel lives in Core and pulls typefaces from BinaryData;
# attach the asset lib + feature define to Core (PUBLIC) so the app AND the test target
# (both link Core) compile the font path and resolve the BinaryData symbols.
target_link_libraries(Core PUBLIC Assets)
target_compile_definitions(Core PUBLIC HAS_FONT_ASSETS=1)

# The full editor UI (MainComponent + everything under Source/UI that isn't already in Core).
# Split out of the AgentSynth app target so the standalone app AND the audio-plugin wrapper build
# ONE copy of this code from ONE source list — a plugin whose UI drifts from the app's is the
# exact failure mode this library exists to prevent.
# Deliberately NOT folded into Core: Core is the headless-testable layer, and the Tests target
# still compiles these sources itself with JUCE_MODAL_LOOPS_PERMITTED=1, which this library is
# not built with.
# Main.cpp stays out — it is the standalone entry point (JUCEApplication + the generated
# JuceHeader.h) and has no meaning inside a plugin.
add_library(AppUI STATIC
    Source/MainComponent.cpp
    Source/MainComponent.h
    Source/UI/AIChatComponent.cpp
    Source/UI/AccountRow.cpp
    Source/UI/AccountRow.h
    Source/UI/SignInDialog.cpp
    Source/UI/SignInDialog.h
    Source/UI/GraphEditor.cpp
    Source/UI/GraphEditor.h
    Source/UI/ModuleComponent.cpp
    Source/UI/ModuleComponent.h
    Source/UI/FrequencyResponseComponent.h
    Source/UI/FrequencyGrid.h
    Source/UI/EQCurveComponent.h
    Source/UI/EQWindow.h
    Source/UI/SampleWaveformComponent.h
    Source/UI/WavetableDisplayComponent.h
    Source/UI/TriggerMeterComponent.h
    Source/UI/ModMatrixComponent.cpp
    Source/UI/ModMatrixComponent.h
    Source/UI/SettingsWindow.cpp
    Source/UI/SettingsWindow.h
    Source/UI/AppearanceSettingsTab.cpp
    Source/UI/AppearanceSettingsTab.h
    Source/UI/ShortcutsSettingsTab.cpp
    Source/ShortcutManager.h
)

# PUBLIC so consumers (the app and the plugin) inherit Core's JUCE modules, include dirs,
# the Assets binary-data lib and HAS_FONT_ASSETS.
target_link_libraries(AppUI PUBLIC Core)

# The VST3 is a shared object on Linux, and every object linked into a .so must be
# position-independent. Core/AppUI/Assets are static libraries that were previously only ever
# linked into an executable, where non-PIC is fine — linking them into the plugin without this
# fails at link time with "failed to set dynamic section sizes: bad value" from ld. macOS and
# Windows don't hit it (Mach-O is PIC by default, and PE/DLLs have no equivalent constraint),
# which is exactly why this only broke the Ubuntu CI job.
# Set on the targets rather than globally so it travels with the libraries the plugin consumes.
set_target_properties(Core AppUI Assets PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(AppUI PRIVATE /W4)
else()
    target_compile_options(AppUI PRIVATE -Wall -Wextra)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(AppUI PUBLIC ${GTK3_LIBRARIES})
    target_include_directories(AppUI PUBLIC ${GTK3_INCLUDE_DIRS})
endif()

# The standalone app is now just its entry point on top of AppUI.
target_sources(AgentSynth PRIVATE
    Source/Main.cpp
)

# Link against JUCE modules
target_link_libraries(AgentSynth PRIVATE
    AppUI # Editor UI, shared with the plugin target
    Core # Link to our core library
    Assets # Embedded font binary data
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
    juce::juce_data_structures
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_animation
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
)

target_compile_definitions(AgentSynth PRIVATE
    JUCE_WEB_BROWSER=0
    HAS_FONT_ASSETS=1
)

if(UNIX AND NOT APPLE)
    target_link_libraries(AgentSynth PRIVATE ${GTK3_LIBRARIES})
    target_include_directories(AgentSynth PRIVATE ${GTK3_INCLUDE_DIRS})
endif()

# Enable strict warnings to catch issues early
if(MSVC)
    target_compile_options(AgentSynth PRIVATE /W4)
else()
    target_compile_options(AgentSynth PRIVATE -Wall -Wextra)
endif()

# ----------------------------------------------------------------------------------------------
# Audio plugin (VST3 everywhere, + AU on macOS)
#
# Wraps the SAME AudioEngine and MainComponent the standalone app runs (via the AppUI library
# above) in a juce::AudioProcessor. The engine is constructed in HostMode::Hosted so it never
# opens an audio device or MIDI input of its own — the host drives it. See Source/Plugin/ and
# docs/architecture.md.
#
# Standalone is deliberately NOT in FORMATS: JUCE would emit a second "Agent Synth.app" that
# collides with the AgentSynth app target's own artifact in release packaging, for a build that
# does strictly less than the real app.
# ----------------------------------------------------------------------------------------------
if(ENABLE_PLUGIN)
    set(SYNTH_PLUGIN_FORMATS VST3)
    if(APPLE)
        list(APPEND SYNTH_PLUGIN_FORMATS AU)
    endif()

    juce_add_plugin(AgentSynthPlugin
        PRODUCT_NAME "${SYNTH_PRODUCT_NAME}"
        VERSION "${PROJECT_VERSION}"
        COMPANY_NAME "${SYNTH_COMPANY_NAME}"
        # Must differ from the app's bundle ID — macOS refuses to register two bundles that
        # claim the same CFBundleIdentifier, and a collision silently breaks AU validation.
        BUNDLE_ID "${SYNTH_BUNDLE_ID}.plugin"
        # 4-char codes. The manufacturer code MUST contain at least one uppercase character or
        # Audio Unit registration rejects the component.
        PLUGIN_MANUFACTURER_CODE Agsy
        PLUGIN_CODE Agsn
        FORMATS ${SYNTH_PLUGIN_FORMATS}
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        # The graph editor is keyboard-driven (see docs/shortcuts.md); without this the host
        # swallows every keystroke before the editor sees it.
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        # Never install into the user's system plugin folders as a build side effect — CI
        # packages the artefacts explicitly, and a local build shouldn't mutate ~/Library.
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument Synth
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
    )

    target_sources(AgentSynthPlugin PRIVATE
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PluginProcessor.h
        Source/Plugin/PluginEditor.cpp
        Source/Plugin/PluginEditor.h
    )

    target_link_libraries(AgentSynthPlugin PRIVATE
        AppUI # Editor UI, shared with the standalone app
        Core
        Assets # Embedded font binary data
        juce::juce_audio_utils
    )

    target_compile_definitions(AgentSynthPlugin PUBLIC
        JUCE_WEB_BROWSER=0
        HAS_FONT_ASSETS=1
        # JUCE 8 has no VST2 support; be explicit so the VST3 wrapper doesn't try to masquerade
        # as a VST2 replacement (which would require the long-gone VST2 SDK).
        JUCE_VST3_CAN_REPLACE_VST2=0
    )

    if(UNIX AND NOT APPLE)
        target_link_libraries(AgentSynthPlugin PRIVATE ${GTK3_LIBRARIES})
        target_include_directories(AgentSynthPlugin PRIVATE ${GTK3_INCLUDE_DIRS})
    endif()

    if(MSVC)
        target_compile_options(AgentSynthPlugin PRIVATE /W4)
    else()
        target_compile_options(AgentSynthPlugin PRIVATE -Wall -Wextra)
    endif()
endif()

if(ENABLE_TESTS)
    add_subdirectory(Tests)
endif()

# Measurement harnesses for AI patch validity and quality. Both need a live Ollama, so neither
# is ever part of the default build or CI — see their READMEs under Tools/.
if(ENABLE_AI_HARNESS)
    add_subdirectory(Tools/AIPatchHarness)
    add_subdirectory(Tools/AIEvalHarness)
endif()

