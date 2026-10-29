# The in-app test mode (docs/development/test-mode.md): a local socket server that lets a script click, drag, press keys,
# read a control's state and save a PNG of any component. It needs MainComponent, so it lives in AppUI; every platform
# compiles it, so CI tests and coverage reach it. Only Main.cpp's activation sits behind AGENTSYNTH_AUTOMATION.
target_sources(AppUI PRIVATE
    Source/TestMode/TestModeCommands.cpp
    Source/TestMode/TestModeCommands.h
    Source/TestMode/TestModeComponents.cpp
    Source/TestMode/TestModeComponents.h
    Source/TestMode/TestModeInput.cpp
    Source/TestMode/TestModeInput.h
    Source/TestMode/TestModeLaunch.cpp
    Source/TestMode/TestModeLaunch.h
    Source/TestMode/TestModeProtocol.cpp
    Source/TestMode/TestModeProtocol.h
    Source/TestMode/TestModeServer.cpp
    Source/TestMode/TestModeServer.h
    Source/TestMode/TestModeStateSource.h
)

# Packaged builds configure -DAGENTSYNTH_AUTOMATION=OFF, so `--automation` is refused there.
option(AGENTSYNTH_AUTOMATION "Let --automation <port> start the local test-mode server" ON)
if(AGENTSYNTH_AUTOMATION)
    target_compile_definitions(AgentSynth PRIVATE AGENTSYNTH_AUTOMATION=1)
endif()
