# The in-app test mode (docs/development/test-mode.md): a local socket server that lets a script click, drag, press keys,
# read a control's state and save a PNG of any component. It needs MainComponent, so it lives in AppUI; every platform
# compiles it, so CI tests and coverage reach it. Only Main.cpp's activation sits behind AGENTSYNTH_AUTOMATION.
target_sources(AppUI PRIVATE
    Source/TestMode/AutomationHiding.cpp
    Source/TestMode/AutomationHiding.h
    Source/TestMode/TestModeAudioDevice.cpp
    Source/TestMode/TestModeAudioDevice.h
    Source/TestMode/TestModeAudioPump.cpp
    Source/TestMode/TestModeAudioPump.h
    Source/TestMode/TestModeCommands.cpp
    Source/TestMode/TestModeCommands.h
    Source/TestMode/TestModeComponents.cpp
    Source/TestMode/TestModeComponents.h
    Source/TestMode/TestModeInput.cpp
    Source/TestMode/TestModeInput.h
    Source/TestMode/TestModeFeedVisual.cpp
    Source/TestMode/TestModeFeedVisual.h
    Source/TestMode/TestModeLevel.h
    Source/TestMode/TestModeLaunch.cpp
    Source/TestMode/TestModeLaunch.h
    Source/TestMode/TestModeAiAnswer.cpp
    Source/TestMode/TestModeAiAnswer.h
    Source/TestMode/TestModeMidiInject.cpp
    Source/TestMode/TestModeMidiInject.h
    Source/TestMode/TestModePopWatch.cpp
    Source/TestMode/TestModePopWatch.h
    Source/TestMode/TestModePresentedFrame.cpp
    Source/TestMode/TestModePresentedFrame.h
    Source/TestMode/TestModeProtocol.cpp
    Source/TestMode/TestModeProtocol.h
    Source/TestMode/TestModeScreenFrame.cpp
    Source/TestMode/TestModeScreenFrame.h
    Source/TestMode/TestModeServer.cpp
    Source/TestMode/TestModeServer.h
    Source/TestMode/TestModeStateSource.h
)

# Hidden automation runs (alpha-0 windows, accessory activation policy, no App Nap): the Cocoa half. AppUI so the app and
# the Tests binary share it; ARC for the retained activity token.
if(APPLE)
    target_sources(AppUI PRIVATE Source/TestMode/AutomationHidingMac.mm)
    set_source_files_properties(Source/TestMode/AutomationHidingMac.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
endif()

# Off unless asked for, so a build nobody asked for (packaged, plugin-only, a plain `cmake -B build`) refuses
# `--automation`. scripts/ci-local.sh configures it ON (the build ui-verify and the in-app checks drive); a build
# by hand for test mode passes -DAGENTSYNTH_AUTOMATION=ON. Guard: scripts/tests/automation-default-off.test.sh.
option(AGENTSYNTH_AUTOMATION "Let --automation <port> start the local test-mode server" OFF)
if(AGENTSYNTH_AUTOMATION)
    target_compile_definitions(AgentSynth PRIVATE AGENTSYNTH_AUTOMATION=1)
endif()

include(cmake/PluginSleepSources.cmake) # Core and AppUI both exist here
# The silent device an automation launch renders through (Core units).
include(${CMAKE_CURRENT_LIST_DIR}/SilentDeviceSources.cmake)
