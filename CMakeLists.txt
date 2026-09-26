cmake_minimum_required(VERSION 3.22)

project(AgentSynth VERSION 0.202.0)

# Sparkle's update bridge (Source/Update/SparkleUpdateManager.mm) is Objective-C++. JUCE's own
# project() only declares C/CXX, so this must be enabled explicitly before any .mm source is added.
if(APPLE)
    enable_language(OBJCXX)
endif()

# Enable ccache if available. The launcher is PER LANGUAGE (Objective-C++ is its own CMake
# language) — see docs/development/ci-caching.md (rule 5) and scripts/ci-cache-check.sh.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    if(APPLE)
        set(CMAKE_OBJC_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        set(CMAKE_OBJCXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    endif()
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

# Fetch Sparkle (macOS auto-update). Prebuilt .xcframework distribution, not a CMake project —
# FetchContent_MakeAvailable populates it and, finding no CMakeLists.txt at its root, does not try
# to add_subdirectory it.
if(APPLE)
    FetchContent_Declare(
        sparkle
        URL      ${SYNTH_SPARKLE_URL}
        URL_HASH SHA256=${SYNTH_SPARKLE_SHA256}
    )
    FetchContent_MakeAvailable(sparkle)
endif()

# Fetch WinSparkle (Windows auto-update, P5-6). Same "prebuilt binary, no CMakeLists.txt at its
# root" shape as Sparkle above — headers + import lib + DLL + its own signing CLI
# (bin/winsparkle-tool.exe), not a CMake project.
if(WIN32)
    FetchContent_Declare(
        winsparkle
        URL      ${SYNTH_WINSPARKLE_URL}
        URL_HASH SHA256=${SYNTH_WINSPARKLE_SHA256}
    )
    FetchContent_MakeAvailable(winsparkle)
    # Unlike Sparkle's .tar.xz (which extracts flat), WinSparkle's release zip wraps every file in
    # a single "WinSparkle-X.Y.Z/" top-level directory — FetchContent's zip extraction preserves
    # that nesting rather than stripping it. Centralized here (one place to bump on a version
    # change) rather than hardcoded at every ${SYNTH_WINSPARKLE_DIR} use below.
    set(SYNTH_WINSPARKLE_DIR "${winsparkle_SOURCE_DIR}/WinSparkle-0.9.4")
endif()

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

# Auto-update (Sparkle on macOS, WinSparkle on Windows — see docs/development/auto-update.md).
# The public keys are deliberately empty by default: both update managers stay inert without one,
# so a dev/local build never shows an "app is misconfigured" alert. CI supplies the real keys via
# -DSYNTH_SPARKLE_PUBLIC_KEY / -DSYNTH_WINSPARKLE_PUBLIC_KEY (separate, independent EdDSA pairs).
#
# One cache var for the feed URL, platform-conditional default: mac and Windows publish separate
# appcast files under the same agentsynth.app/updates/ path.
if(APPLE)
    set(SYNTH_UPDATE_FEED_URL_DEFAULT "https://agentsynth.app/updates/appcast.xml")
elseif(WIN32)
    set(SYNTH_UPDATE_FEED_URL_DEFAULT "https://agentsynth.app/updates/appcast-windows.xml")
else()
    set(SYNTH_UPDATE_FEED_URL_DEFAULT "")
endif()
set(SYNTH_UPDATE_FEED_URL "${SYNTH_UPDATE_FEED_URL_DEFAULT}"
    CACHE STRING "Sparkle/WinSparkle feed URL — the appcast this build checks for updates")
set(SYNTH_SPARKLE_PUBLIC_KEY "" CACHE STRING "Sparkle SUPublicEDKey (base64, public half of the EdDSA signing key)")
set(SYNTH_WINSPARKLE_PUBLIC_KEY "" CACHE STRING "WinSparkle EdDSA public key (base64, WinSparkle's own key pair, separate from Sparkle's)")

# Marketing version — the human-facing version users see (CFBundleShortVersionString, the plugin's
# own VERSION arg, the installer's Add/Remove Programs entry). Defaults to PROJECT_VERSION so a
# local build behaves unchanged; CI overrides it via -DSYNTH_MARKETING_VERSION to match the
# computed release tag exactly, with nobody hand-bumping project(AgentSynth VERSION ...).
set(SYNTH_MARKETING_VERSION "${PROJECT_VERSION}" CACHE STRING
    "Marketing version embedded in the build (CFBundleShortVersionString / plugin VERSION / installer version); CI sets this to the computed release tag, local builds default to PROJECT_VERSION")

# CFBundleVersion (Sparkle's actual update-comparison key) is deliberately NOT
# SYNTH_MARKETING_VERSION above. SYNTH_BUILD_NUMBER is a separate, always-available counter:
# GITHUB_RUN_NUMBER, known before the build starts, passed via -DSYNTH_BUILD_NUMBER. Local builds
# default to "0" — never distributed, so Sparkle comparison doesn't matter for them.
set(SYNTH_BUILD_NUMBER "0" CACHE STRING "CFBundleVersion — monotonic build number, CI sets this to the workflow run number")

# ---- "What's New": build-time, no-network release notes ----
# See docs/development/distribution.md's "What's New" section for the full rationale (why
# configure-time not build-time, the commit-subject-lines-only sensitive-info boundary, rendering).
execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE SYNTH_WHATSNEW_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE SYNTH_WHATSNEW_TAG_RESULT
)
if(NOT SYNTH_WHATSNEW_TAG_RESULT EQUAL 0 OR SYNTH_WHATSNEW_TAG STREQUAL "")
    # A shallow clone or a repo with no tags yet must not break the configure.
    set(SYNTH_WHATSNEW_TAG "development build")
endif()

execute_process(
    COMMAND git log --pretty=format:%s -n 15
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE SYNTH_WHATSNEW_LOG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE SYNTH_WHATSNEW_LOG_RESULT
)
if(NOT SYNTH_WHATSNEW_LOG_RESULT EQUAL 0 OR SYNTH_WHATSNEW_LOG STREQUAL "")
    # A source tarball (no .git directory at all) must not break the configure either.
    set(SYNTH_WHATSNEW_LOG "No release history available.")
endif()

# Sanitize BOTH captures before they reach a C string literal (juce::String's `const char*` ctor
# decodes as Latin-1, so a stray UTF-8 byte would mojibake). Escape backslash/quote FIRST, so the
# ASCII-range replacement below can't corrupt an escape sequence it just introduced.
foreach(_synth_whatsnew_var SYNTH_WHATSNEW_TAG SYNTH_WHATSNEW_LOG)
    string(REPLACE "\\" "\\\\" ${_synth_whatsnew_var} "${${_synth_whatsnew_var}}")
    string(REPLACE "\"" "\\\"" ${_synth_whatsnew_var} "${${_synth_whatsnew_var}}")
    # CMake's regex engine has no \xNN hex-escape syntax (that error's caught at configure time,
    # not silently misparsed) — the bracket expression below spells the same printable-ASCII range
    # (0x20 space .. 0x7E tilde) literally instead, plus \n so a real newline survives untouched.
    string(REGEX REPLACE "[^ -~\n]" "?" ${_synth_whatsnew_var} "${${_synth_whatsnew_var}}")
endforeach()

# One line per commit subject, `;`-separated as a CMake list — safe because `;` cannot appear
# inside a single-line git log subject (git itself uses it as the format-field separator).
string(REPLACE "\n" ";" SYNTH_WHATSNEW_HIGHLIGHTS "${SYNTH_WHATSNEW_LOG}")
list(LENGTH SYNTH_WHATSNEW_HIGHLIGHTS SYNTH_WHATSNEW_HIGHLIGHTS_COUNT)

set(SYNTH_WHATSNEW_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${SYNTH_WHATSNEW_GENERATED_DIR}")

set(SYNTH_WHATSNEW_HEADER_CONTENT
"#pragma once

// GENERATED at CMake configure time — see the \"What's New\" block in the root CMakeLists.txt.
// Do not edit by hand; edit the generator instead.
namespace synth::whatsnew {
inline constexpr const char* kReleaseTag = \"${SYNTH_WHATSNEW_TAG}\";
inline constexpr const char* kHighlights[] = {
")
foreach(_synth_whatsnew_line ${SYNTH_WHATSNEW_HIGHLIGHTS})
    string(APPEND SYNTH_WHATSNEW_HEADER_CONTENT "    \"${_synth_whatsnew_line}\",\n")
endforeach()
string(APPEND SYNTH_WHATSNEW_HEADER_CONTENT
"};
inline constexpr int kHighlightsCount = ${SYNTH_WHATSNEW_HIGHLIGHTS_COUNT};
} // namespace synth::whatsnew
")
file(WRITE "${SYNTH_WHATSNEW_GENERATED_DIR}/WhatsNewData.h" "${SYNTH_WHATSNEW_HEADER_CONTENT}")

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
    Source/AudioEngine/AudioEngine.h
    Source/AudioEngine/ModulationRoutingTypes.h
    Source/AudioEngine/AudioEngineBounceSuspend.cpp
    Source/AudioEngine/AudioEngineDeviceLifecycle.cpp
    Source/AudioEngine/AudioEngineHostMode.cpp
    Source/AudioEngine/AudioEngineMidi.cpp
    Source/AudioEngine/AudioEngineModRouting.cpp
    Source/AudioEngine/AudioEngineRenderPass.cpp
    Source/AudioEngine/AudioEngineRuntimeState.cpp
    Source/AudioEngine/AudioEngineTimelinePublish.cpp
    Source/AppUndoManager.cpp
    Source/AppUndoManager.h
    Source/AutomationLaunch.cpp
    Source/AutomationLaunch.h
    Source/Branding.h
    Source/GeneralFeedbackStore.cpp
    Source/GeneralFeedbackStore.h
    Source/MacroSet.cpp
    Source/MacroSet.h
    Source/PresetManager.cpp
    Source/PresetManager.h
    Source/PatchDocument.cpp
    Source/PatchDocument.h
    Source/ProjectBundle.cpp
    Source/ProjectBundle.h
    Source/MidiRemote/RemoteEngine/RemoteEngine.h
    Source/MidiRemote/RemoteEngine/RemoteEngine.cpp
    Source/MidiRemote/RemoteEngine/RemoteEngineApply.cpp
    Source/MidiRemote/RemoteEngine/RemoteEngineDecode.cpp
    Source/MidiRemote/RemoteEngine/RemoteEngineFeedback.cpp
    Source/MidiRemote/RemoteEngine/RemoteEngineInternal.h
    Source/MidiRemote/RemoteEngine/RemoteEngineLearn.cpp
    Source/MidiRemote/RemoteEngine/RemoteEngineReconcile.cpp
    Source/MidiRemote/RemoteEngine/RemoteEvent.h
    Source/MidiRemote/RemoteEngine/RemoteFeedbackSink.h
    Source/MidiRemote/RemoteEngine/RemoteMappingSnapshot.h
    Source/MidiRemote/RemoteEngine/RemoteMessageSink.h
    Source/MidiRemote/RemoteModel.h
    Source/MidiRemote/RemoteModelJson.cpp
    Source/MidiRemote/ContinuousTarget.h
    Source/MidiRemote/ContinuousTarget.cpp
    Source/MidiRemote/MidiRemoteLearnBinder.h
    Source/MidiRemote/MidiRemoteLearnBinder.cpp
    Source/MidiRemote/MidiRemoteMapping.h
    Source/MidiRemote/MidiRemoteMapping.cpp
    Source/MidiRemote/PickTarget.h
    Source/MidiRemote/ControllerDetect.h
    Source/MidiRemote/ControllerDetect.cpp
    Source/MidiRemote/EncoderAutoDetect.h
    Source/MidiRemote/EncoderAutoDetect.cpp
    Source/MidiRemote/ControllerTemplates.h
    Source/MidiRemote/ControllerTemplates.cpp
    Source/RecentProjects.cpp
    Source/RecentProjects.h
    Source/SnippetManager.cpp
    Source/SnippetManager.h
    Source/SettingsMigration.cpp
    Source/SettingsMigration.h
    # Mixer
    Source/Mixer/ChannelFlows/ChannelFlows.h
    Source/Mixer/ChannelFlows/ChannelFlowsAutoChannel.cpp
    Source/Mixer/ChannelFlows/ChannelFlowsDefaultChannel.cpp
    Source/Mixer/ChannelFlows/ChannelFlowsInternal.h
    Source/Mixer/ChannelFlows/ChannelFlowsMakeChannel.cpp
    Source/Mixer/ChannelFlows/ChannelFlowsTrackChannelLink.cpp
    Source/Mixer/ChannelFlows/ChannelFlowsTrackPreset.cpp
    Source/Mixer/MasterSplice.cpp
    Source/Mixer/MasterSplice.h
    Source/Mixer/TrackChannelLink.cpp
    Source/Mixer/TrackChannelLink.h
    Source/Mixer/TrackPresetManager.cpp
    Source/Mixer/TrackPresetManager.h
    Source/Mixer/SoloAudibleSet.h
    Source/Mixer/SoloAudibleSet.cpp
    Source/Mixer/PeakMeterLatch.h
    Source/Mixer/MixerModel/MixerModel.h
    Source/Mixer/MixerModel/MixerModelInternal.h
    Source/Mixer/MixerModel/MixerModelColumns.cpp
    Source/Mixer/MixerModel/MixerModelInserts.cpp
    Source/Mixer/MixerModel/MixerModelSends.cpp
    Source/Mixer/MixerSends/MixerSends.h
    Source/Mixer/MixerSends/MixerSends.cpp
    # Transport
    Source/Transport/BlockTimeInfo.h
    Source/Transport/BounceExporter.cpp
    Source/Transport/BounceExporter.h
    Source/Transport/BounceGuards.h
    Source/Transport/BounceRunner.cpp
    Source/Transport/BounceRunner.h
    Source/Transport/BounceSession.cpp
    Source/Transport/BounceSession.h
    Source/Transport/Metronome.cpp
    Source/Transport/Metronome.h
    Source/Transport/OfflineTransportDriver.cpp
    Source/Transport/OfflineTransportDriver.h
    Source/Transport/StemExporter.cpp
    Source/Transport/StemExporter.h
    Source/Transport/StemRunner.cpp
    Source/Transport/StemRunner.h
    Source/Transport/StemSession.cpp
    Source/Transport/StemSession.h
    Source/Transport/TempoMap.h
    Source/Transport/TransportService.cpp
    Source/Transport/MarkerJump.h
    Source/Transport/TransportNudge.h
    Source/Transport/TransportService.h
    # Timeline
    Source/Timeline/ArrangementContext.cpp
    Source/Timeline/ArrangementContext.h
    Source/Timeline/AssetManager.cpp
    Source/Timeline/AssetManager.h
    Source/Timeline/AudioClipStreamer.cpp
    Source/Timeline/AudioClipStreamer.h
    Source/Timeline/AutomationApplier.cpp
    Source/Timeline/AutomationApplier.h
    Source/Timeline/AutomationBinding.cpp
    Source/Timeline/AutomationBinding.h
    Source/Timeline/AutomationKernel.h
    Source/Timeline/AutomationRecorder.cpp
    Source/Timeline/AutomationRecorder.h
    Source/Timeline/EpochExchange.h
    Source/Timeline/MidiClipFile.cpp
    Source/Timeline/MidiClipFile.h
    Source/Timeline/MidiRecorder.cpp
    Source/Timeline/MidiRecorder.h
    Source/Timeline/PeaksFile.cpp
    Source/Timeline/PeaksFile.h
    Source/Timeline/TakePlacement.cpp
    Source/Timeline/TakePlacement.h
    Source/Timeline/TimelineDoc/TimelineDoc.h
    Source/Timeline/TimelineDoc/TimelineDocAutomation.cpp
    Source/Timeline/TimelineDoc/TimelineDocClips.cpp
    Source/Timeline/TimelineDoc/TimelineDocCore.cpp
    Source/Timeline/TimelineDoc/TimelineDocInternal.h
    Source/Timeline/TimelineDoc/TimelineDocMarkers.cpp
    Source/Timeline/TimelineDoc/TimelineDocNotes.cpp
    Source/Timeline/TimelineDoc/TimelineDocSerialization.cpp
    Source/Timeline/TimelineDoc/TimelineDocTracks.cpp
    Source/Timeline/TimelineOps.cpp
    Source/Timeline/TimelineOps.h
    Source/Timeline/TimelineReconciler.cpp
    Source/Timeline/TimelineReconciler.h
    Source/Timeline/TimelineSnapshot.cpp
    Source/Timeline/TimelineSnapshot.h
    Source/Timeline/TimelineSnapshotExchange.h
    Source/Timeline/TimelineValidator.cpp
    Source/Timeline/TimelineValidator.h
    # Modules
    Source/Modules/ModuleBase.h
    Source/Modules/PortRole.h
    Source/Modules/AudioInputModule.h
    Source/Modules/OscillatorModule.h
    Source/Modules/ADSRModule.h
    Source/Modules/FilterModule.h
    Source/Modules/VCAModule.h
    Source/Modules/VoiceMixerModule.h
    Source/Modules/NoiseModule.h
    Source/Modules/MathModule.h
    Source/Modules/SamplerModule.h
    Source/Modules/WavetableOscillatorModule/WavetableOscillatorModule.h
    Source/Modules/WavetableOscillatorModule/WavetableOscillatorModule.cpp
    Source/Modules/WavetableOscillatorModule/WavetableTableBuilder.h
    Source/Modules/WavetableOscillatorModule/WavetableTableBuilder.cpp
    Source/Modules/SequencerModule.h
    Source/Modules/MidiKeyboardModule.h
    Source/Modules/LFOModule.h
    Source/Modules/SampleHoldModule.h
    Source/Modules/SchmittTrigger.h
    Source/Modules/ThresholdMeterSource.h
    Source/Modules/ComparatorModule.h
    Source/Modules/AttenuverterModule.h
    Source/Modules/EnvelopeFollowerModule.h
    Source/Modules/CardLayout.cpp
    Source/Modules/CardLayout.h
    Source/Modules/MacroControlModule.h
    Source/Modules/RecordTapModule.cpp
    Source/Modules/RecordTapModule.h
    Source/Modules/TimelineAudioSourceModule.h
    Source/Modules/TimelineMidiSourceModule.h
    Source/Modules/MacroInletModule.h
    Source/Modules/MacroOutletModule.h
    Source/Modules/MacroMidiInletModule.h
    Source/Modules/MacroMidiOutletModule.h
    Source/Modules/ChannelStripModule.h
    Source/Modules/MasterModule.h
    Source/Modules/VisualBuffer.h
    Source/Modules/FX/DelayModule.h
    Source/Modules/FX/DistortionModule.h
    Source/Modules/FX/ReverbModule.h
    Source/Modules/FX/ChorusModule.h
    Source/Modules/FX/PhaserModule.h
    Source/Modules/FX/CompressorModule.h
    Source/Modules/FX/FlangerModule.h
    Source/Modules/FX/LimiterModule.h
    Source/Modules/FX/GateModule.h
    Source/Modules/FX/BitcrusherModule.h
    Source/Modules/FX/PitchShifterModule.h
    Source/Modules/FX/RingModulatorModule.h
    Source/Modules/FX/ParametricEQModule.h
    Source/Modules/NoiseModule.h
    # Plugin hosting (TL7-2) — third-party VST3/AU plugins as graph modules. In Core, not in the
    # AgentSynthPlugin target: this is the app HOSTING plugins, which the headless Tests target
    # exercises against a stub backend (Tests/StubPluginInstance.h).
    Source/Plugin/Hosting/HostedPluginBackend.cpp
    Source/Plugin/Hosting/HostedPluginBackend.h
    Source/Plugin/Hosting/HostedPluginModule.cpp
    Source/Plugin/Hosting/HostedPluginModule.h
    Source/Plugin/Hosting/HostedPluginModuleCardLayout.cpp
    Source/Plugin/Hosting/PluginScanService.cpp
    Source/Plugin/Hosting/PluginScanService.h
    Source/Plugin/Hosting/PluginScanCrashGuard.cpp
    Source/Plugin/Hosting/PluginScanCrashGuard.h
    # AI
    Source/AI/AIProvider.h
    Source/AI/OllamaProvider.h
    Source/AI/RemoteProvider.h
    Source/AI/AIIntegrationService/AIIntegrationServiceProviderConfig.cpp
    Source/AI/AIIntegrationService/AIIntegrationServiceRequestSending.cpp
    Source/AI/AIIntegrationService/AIIntegrationServiceSystemPrompt.cpp
    Source/AI/AIIntegrationService/AIIntegrationServicePatchApply.cpp
    Source/AI/AIIntegrationService/AIIntegrationServiceTimelineOps.cpp
    Source/AI/OllamaProvider.cpp
    Source/AI/RemoteProvider.cpp
    Source/AI/AIIntegrationService/AIIntegrationService.h
    Source/AI/AIStateMapper/AIStateMapper.cpp
    Source/AI/AIStateMapper/AIStateMapper.h
    Source/AI/AIStateMapper/AIStateMapperValidation.cpp
    Source/AI/AIStateMapper/AIStateMapperSnapshots.cpp
    Source/AI/AIStateMapper/AIStateMapperSchema.cpp
    Source/AI/AIProviderRegistry.h
    Source/AI/AIProviderRegistry.cpp
    Source/AI/PatchEval.h
    Source/AI/PatchEval.cpp
    Source/AI/PatchDiff.h
    Source/AI/PatchDiff.cpp
    Source/AI/AuthClient.h
    Source/AI/AuthClient.cpp
    Source/AI/AccountService.h
    Source/AI/AccountService.cpp
    Source/AI/PatchFeedbackStore.h
    Source/AI/PatchFeedbackStore.cpp
    Source/AI/LocalHistoryStore.h
    Source/AI/LocalHistoryStore.cpp
    Source/AI/ConversationHistorySource.h
    # Auth (device-code sign-in: token storage, no UI)
    Source/Auth/TokenStore.h
    Source/Auth/InMemoryTokenStore.h
    Source/Auth/KeychainTokenStore.h
    Source/Auth/KeychainTokenStore.cpp
    Source/Auth/DeviceIdStore.h
    Source/Auth/DeviceIdStore.cpp
    # UI utilities
    Source/UI/Layout/LayoutUtil.h
    Source/UI/Layout/LayoutUtil.cpp
    Source/UI/Graph/ModuleStepOrder.h
    Source/UI/Graph/SelectionModel.h
    Source/UI/Chrome/StatusBarComponent.h
    Source/UI/Chrome/StatusBarComponent.cpp
    Source/UI/Chrome/ToolbarComponent.h
    Source/UI/Chrome/ToolbarComponent.cpp
    Source/UI/Graph/MinimapComponent.h
    Source/UI/Graph/MinimapComponent.cpp
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
    # FRO147: MeterColourStops is pure model code (no juce::Component) that used to live only in
    # AppUI's source list, but AppLookAndFeel (Core, below) now caches an effective MeterColourStops
    # itself (the ONE cache every meter painter reads) -- so Core needs the definition too. Moved
    # here rather than compiled into BOTH Core and AppUI, which would duplicate-symbol any target
    # linking both (AgentSynth, Tests). AppUI's own MixerMeter.cpp/ChannelChipComponent.cpp (still
    # AppUI-only, still real juce::Components) reach it transitively via AppUI's PUBLIC link to Core.
    Source/UI/Mixer/MeterColourStops.h
    Source/UI/Mixer/MeterColourStops.cpp
    Source/UI/Theme/AppLookAndFeel/AppLookAndFeel.h
    Source/UI/Theme/AppLookAndFeel/AppLookAndFeelLifecycle.cpp
    Source/UI/Theme/AppLookAndFeel/AppLookAndFeelFonts.cpp
    Source/UI/Theme/AppLookAndFeel/AppLookAndFeelSliders.cpp
    Source/UI/Theme/AppLookAndFeel/AppLookAndFeelButtons.cpp
    Source/UI/Theme/AppLookAndFeel/AppLookAndFeelComboAndMenus.cpp
    Source/UI/Theme/AppLookAndFeel/AppLookAndFeelScrollbarsAndChrome.cpp
    Source/UI/Theme/AppLookAndFeel/AppLookAndFeelCanvasTreatments.cpp
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

# TL7-1/TL7-2: host third-party plugins. JUCE's built-in hosting only — no new dependency pins, and
# therefore no new licences to clear (the VST3 and AU host code ships inside juce_audio_processors,
# which we already link). CLAP is deliberately deferred: it would mean vendoring clap-juce-extensions
# or the CLAP headers, i.e. exactly the new dependency this decision avoids.
#
# PUBLIC, on Core, beside the other JUCE defines: HostedPluginBackend is compiled into Core, and
# every consumer (AppUI, Tests, the plugin wrapper) must agree on these values or the format list in
# DefaultHostedPluginBackend's constructor differs between translation units.
target_compile_definitions(Core PUBLIC JUCE_PLUGINHOST_VST3=1)
if(APPLE)
    target_compile_definitions(Core PUBLIC JUCE_PLUGINHOST_AU=1)
endif()

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

# Sparkle Info.plist keys, merged into JUCE's generated plist. Empty string on non-Apple
# platforms, where JUCE ignores the argument entirely.
set(SYNTH_SPARKLE_PLIST_TO_MERGE "")
if(APPLE)
    set(SYNTH_SPARKLE_PLIST_TO_MERGE
        "<?xml version='1.0' encoding='UTF-8'?><plist version='1.0'><dict><key>SUFeedURL</key><string>${SYNTH_UPDATE_FEED_URL}</string><key>SUEnableAutomaticChecks</key><true/><key>SUScheduledCheckInterval</key><integer>86400</integer><key>SUPublicEDKey</key><string>${SYNTH_SPARKLE_PUBLIC_KEY}</string></dict></plist>")
endif()

# Create the application target
juce_add_gui_app(AgentSynth
    PRODUCT_NAME "${SYNTH_PRODUCT_NAME}"
    VERSION "${SYNTH_MARKETING_VERSION}"
    BUILD_VERSION "${SYNTH_BUILD_NUMBER}"
    COMPANY_NAME "${SYNTH_COMPANY_NAME}"
    BUNDLE_ID "${SYNTH_BUNDLE_ID}"
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "We need Bluetooth access to connect to your external MIDI devices."
    # TL6-1: the Audio tab can now open an input device (opt-in). macOS kills any process that
    # touches an input stream without NSMicrophoneUsageDescription in its Info.plist, so this is
    # not optional once inputs are reachable. The plugin target needs no equivalent — the host
    # owns the device there, and its own entitlement covers it.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "We need audio input access so you can patch a microphone or instrument into the synth."
    PLIST_TO_MERGE "${SYNTH_SPARKLE_PLIST_TO_MERGE}"
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

# Asset list in cmake/Assets.cmake -- split out to keep this file under the 1,000-line cap.
include(cmake/Assets.cmake)

# The themed LookAndFeel lives in Core and pulls typefaces from BinaryData;
# attach the asset lib + feature define to Core (PUBLIC) so the app AND the test target
# (both link Core) compile the font path and resolve the BinaryData symbols.
target_link_libraries(Core PUBLIC Assets)
target_compile_definitions(Core PUBLIC HAS_FONT_ASSETS=1)

# The full editor UI (MainComponent + everything under Source/UI that isn't already in Core).
# Split out of the AgentSynth app target so the standalone app AND the audio-plugin wrapper build
# ONE copy of this code from ONE source list — a plugin whose UI drifts from the app's is the
# exact failure mode this library exists to prevent.
# Deliberately NOT folded into Core: Core is the headless-testable layer. Tests links this library
# (built without Tests' JUCE_MODAL_LOOPS_PERMITTED=1, like Core) rather than compiling its sources
# a second time.
# Main.cpp stays out — it is the standalone entry point (JUCEApplication + the generated
# JuceHeader.h) and has no meaning inside a plugin.
# Source list in cmake/AppUISources.cmake (APPUI_SOURCES) -- split out to keep this file
# under the repo's 1,000-line file-size cap (scripts/check-file-sizes.sh).
include(cmake/AppUISources.cmake)
add_library(AppUI STATIC ${APPUI_SOURCES})

# PUBLIC so consumers (the app and the plugin) inherit Core's JUCE modules, include dirs,
# the Assets binary-data lib and HAS_FONT_ASSETS.
target_link_libraries(AppUI PUBLIC Core)

# WhatsNewData.h (generated above, "What's New" block) — PRIVATE since only AppUI's
# Source/MainComponent/ units (MainComponent.cpp, MainComponentPanels.cpp) include it.
target_include_directories(AppUI PRIVATE "${SYNTH_WHATSNEW_GENERATED_DIR}")

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

if(APPLE)
    target_sources(AgentSynth PRIVATE Source/Update/SparkleUpdateManager.mm)
    # ARC explicitly, rather than relying on JUCE's project-wide ObjC++ default (which is
    # non-ARC — JUCE's own .mm files manage retain/release by hand), so this one bridge file can
    # use SPUStandardUpdaterController without hand-written retain/release bookkeeping.
    set_source_files_properties(Source/Update/SparkleUpdateManager.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
endif()

if(WIN32)
    target_sources(AgentSynth PRIVATE Source/Update/WinSparkleUpdateManager.cpp)
endif()

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

if(APPLE)
    # `-F` must be a COMPILE flag too, not just a link flag — target_link_libraries only puts it on
    # the link line, but #import <Sparkle/Sparkle.h> in SparkleUpdateManager.mm needs the compiler
    # to see it as well.
    target_compile_options(AgentSynth PRIVATE "-F${sparkle_SOURCE_DIR}")
    target_link_libraries(AgentSynth PRIVATE "-F${sparkle_SOURCE_DIR}" "-framework Sparkle")

    set_target_properties(AgentSynth PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@executable_path/../Frameworks"
    )

    # Sparkle.framework is a prebuilt binary, not something CMake's bundle logic knows to embed —
    # this project builds with Ninja (see build-artifacts.yml), which has no "Embed Frameworks"
    # build phase the way an Xcode project would, so copy it into Contents/Frameworks ourselves.
    # `ditto` (not `cp`) preserves the framework's Versions/Current symlink structure, matching how
    # CI already ditto's the whole .app for packaging. CI's `codesign --deep` step re-signs this
    # nested framework along with everything else in the bundle.
    add_custom_command(TARGET AgentSynth POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:AgentSynth>/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_BUNDLE_CONTENT_DIR:AgentSynth>/Frameworks/Sparkle.framework"
        COMMAND ditto "${sparkle_SOURCE_DIR}/Sparkle.framework" "$<TARGET_BUNDLE_CONTENT_DIR:AgentSynth>/Frameworks/Sparkle.framework"
        COMMENT "Embedding Sparkle.framework"
    )
endif()

if(WIN32)
    # No Info.plist on Windows, so the feed URL / public key that macOS reads out of the bundle at
    # runtime are baked in as compile-time string literals instead. Escaped for MSVC's preprocessor
    # (the URL contains no quotes/backslashes today, but the public key is base64 and could in
    # theory need escaping — CMake's target_compile_definitions already quotes the whole value).
    target_compile_definitions(AgentSynth PRIVATE
        SYNTH_UPDATE_FEED_URL_STR="${SYNTH_UPDATE_FEED_URL}"
        SYNTH_WINSPARKLE_PUBLIC_KEY_STR="${SYNTH_WINSPARKLE_PUBLIC_KEY}"
    )

    target_include_directories(AgentSynth PRIVATE "${SYNTH_WINSPARKLE_DIR}/include")
    # WinSparkle ships prebuilt per-arch (Win32/x64/ARM64) directories; this project's CI matrix
    # only builds x64 (see build-artifacts.yml), so that's the only one wired up here.
    target_link_libraries(AgentSynth PRIVATE "${SYNTH_WINSPARKLE_DIR}/x64/Release/WinSparkle.lib")

    # WinSparkle.dll is a prebuilt binary CMake's bundle logic doesn't know to place — unlike
    # macOS there's no Frameworks dir, the DLL just needs to sit next to the exe (or be on PATH).
    add_custom_command(TARGET AgentSynth POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SYNTH_WINSPARKLE_DIR}/x64/Release/WinSparkle.dll"
            "$<TARGET_FILE_DIR:AgentSynth>"
        COMMENT "Copying WinSparkle.dll next to Agent Synth.exe"
    )
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
# docs/architecture/architecture.md.
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
        VERSION "${SYNTH_MARKETING_VERSION}"
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
        # The graph editor is keyboard-driven (see docs/control/shortcuts.md); without this the host
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

    # MainComponent.h (in AppUI, which this plugin links) pulls in synth::update::UpdateManager
    # on macOS, same as AgentSynth and Tests — see the Sparkle block above and
    # Tests/CMakeLists.txt's identical comment. Neither AgentSynthPlugin's Info.plist nor AU's gets
    # SUFeedURL/SUPublicEDKey merged in (only AgentSynth's does, above), so UpdateManager::isAvailable()
    # reads empty keys and stays inert in the plugin — this wiring exists purely to satisfy the
    # linker, not to make the plugin self-update.
    if(APPLE)
        foreach(plugin_format_target AgentSynthPlugin_VST3 AgentSynthPlugin_AU)
            target_sources(${plugin_format_target} PRIVATE Source/Update/SparkleUpdateManager.mm)
            target_compile_options(${plugin_format_target} PRIVATE "-F${sparkle_SOURCE_DIR}")
            # Weak (not "-framework Sparkle" as AgentSynth/Tests use): juce_vst3_helper dlopens this
            # bundle at build time, right after linking, to write moduleinfo.json — before this
            # target's own POST_BUILD embed step below has run — so a hard dependency makes that
            # load fail outright ("The bundle ... couldn't be loaded"). Weak-linking also matches
            # actual intent here better than a hard link would: unlike AgentSynth, this plugin never
            # gets SUFeedURL/SUPublicEDKey in its Info.plist, so Sparkle is always inert for it.
            target_link_libraries(${plugin_format_target} PRIVATE "-F${sparkle_SOURCE_DIR}" "-weak_framework Sparkle")

            # @loader_path (not @executable_path — the plugin bundle is loaded by the host app, not
            # run as one) resolves to Contents/MacOS, so ../Frameworks lands on Contents/Frameworks,
            # matching the embed step below.
            set_target_properties(${plugin_format_target} PROPERTIES
                BUILD_WITH_INSTALL_RPATH TRUE
                INSTALL_RPATH "@loader_path/../Frameworks"
            )

            add_custom_command(TARGET ${plugin_format_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:${plugin_format_target}>/Frameworks"
                COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_BUNDLE_CONTENT_DIR:${plugin_format_target}>/Frameworks/Sparkle.framework"
                COMMAND ditto "${sparkle_SOURCE_DIR}/Sparkle.framework" "$<TARGET_BUNDLE_CONTENT_DIR:${plugin_format_target}>/Frameworks/Sparkle.framework"
                COMMENT "Embedding Sparkle.framework into ${plugin_format_target}"
            )
        endforeach()
        set_source_files_properties(Source/Update/SparkleUpdateManager.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    endif()

    # MainComponent.h (in AppUI) pulls in synth::update::UpdateManager on Windows too, same
    # reasoning as the APPLE block above: AgentSynthPlugin_VST3 needs the same WinSparkle
    # source/linking AgentSynth gets, purely to satisfy the linker — the plugin's own build never
    # gets SUFeedURL/SUPublicEDKey-equivalent compile definitions (only AgentSynth does, above), so
    # UpdateManager::isAvailable() reads empty values and stays inert here.
    #
    # Delay-loaded (/DELAYLOAD, not a normal import-lib link) rather than linked hard: on macOS the
    # equivalent hazard is that juce_vst3_helper dlopens the freshly-linked plugin bundle right
    # after linking (to write moduleinfo.json), before this target's own POST_BUILD DLL-copy step
    # below has run, and a hard Sparkle.framework dependency made that early load fail outright —
    # fixed there with a *weak* framework link. Windows' juce_vst3_helper does the equivalent
    # LoadLibrary-based introspection, and normal (non-delay-load) import-lib linking resolves ALL
    # imports at that load time regardless of whether any WinSparkle function is ever actually
    # called — there's no "weak" import equivalent on Windows, so a hard link would hit the same
    # "DLL not found" failure the moment WinSparkle.dll isn't next to the plugin's DLL yet.
    # Delay-loading defers DLL resolution to first *call* of a WinSparkle function, and the inert
    # (empty key/URL) plugin path never calls one — matching the macOS "linked but functionally
    # never invoked" intent. NOT verified against a real Windows juce_vst3_helper run (no Windows
    # toolchain in this environment) — if CI's Windows plugin build fails at the helper step with a
    # WinSparkle.dll-not-found error, this is the block to revisit first.
    if(WIN32)
        target_compile_definitions(AgentSynthPlugin_VST3 PRIVATE
            SYNTH_UPDATE_FEED_URL_STR=""
            SYNTH_WINSPARKLE_PUBLIC_KEY_STR=""
        )
        target_sources(AgentSynthPlugin_VST3 PRIVATE Source/Update/WinSparkleUpdateManager.cpp)
        target_include_directories(AgentSynthPlugin_VST3 PRIVATE "${SYNTH_WINSPARKLE_DIR}/include")
        target_link_libraries(AgentSynthPlugin_VST3 PRIVATE
            "${SYNTH_WINSPARKLE_DIR}/x64/Release/WinSparkle.lib"
            delayimp
        )
        target_link_options(AgentSynthPlugin_VST3 PRIVATE "/DELAYLOAD:WinSparkle.dll")

        add_custom_command(TARGET AgentSynthPlugin_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SYNTH_WINSPARKLE_DIR}/x64/Release/WinSparkle.dll"
                "$<TARGET_FILE_DIR:AgentSynthPlugin_VST3>"
            COMMENT "Copying WinSparkle.dll next to AgentSynthPlugin_VST3"
        )
    endif()
endif()

if(ENABLE_TESTS)
    add_subdirectory(Tests)
endif()

# Measurement harnesses for AI patch/timeline validity and quality. AIPatchHarness/AIEvalHarness
# need a live Ollama; TimelineOpsHarness replays recorded fixtures instead and needs none, but is
# gated the same way for consistency (it is a measurement instrument, not something CI gates on —
# see Tests/Timeline/TimelineOpsFixtureTests.cpp for the CI-enforced twin). None of the three is in
# the default build (READMEs under Tools/); in CI only Linux builds them, to catch rot. Each links
# AppUI/Core through cmake/AiHarness.cmake, so it adds just its own Main.cpp (FRO290).
if(ENABLE_AI_HARNESS)
    add_subdirectory(Tools/AIPatchHarness)
    add_subdirectory(Tools/AIEvalHarness)
    add_subdirectory(Tools/TimelineOpsHarness)
endif()

