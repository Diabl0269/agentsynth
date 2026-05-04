cmake_minimum_required(VERSION 3.22)

project(AgentSynth VERSION 0.13.2)

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
add_library(AgentSynthCore STATIC
    Source/AudioEngine.cpp
    Source/AudioEngine.h
    Source/UndoManager.cpp
    Source/UndoManager.h
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
)

# Link Core against JUCE
target_link_libraries(AgentSynthCore PUBLIC
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
    juce::juce_data_structures
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_dsp
)

target_compile_definitions(AgentSynthCore PUBLIC JUCE_WEB_BROWSER=0)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

    find_package(CURL REQUIRED)

    # Link to Core so JUCE modules compiled within it find the headers
    target_link_libraries(AgentSynthCore PUBLIC ${GTK3_LIBRARIES} CURL::libcurl)
    target_include_directories(AgentSynthCore PUBLIC ${GTK3_INCLUDE_DIRS})
endif()

target_include_directories(AgentSynthCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

if(MSVC)
    target_compile_options(AgentSynthCore PRIVATE /W4)
else()
    target_compile_options(AgentSynthCore PRIVATE -Wall -Wextra)
endif()

# Create the application target
juce_add_gui_app(AgentSynth
    PRODUCT_NAME "AgentSynth"
    VERSION "0.13.2"
    BUNDLE_ID "com.agentsynth.app"
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "We need Bluetooth access to connect to your external MIDI devices."
)

juce_generate_juce_header(AgentSynth)

# Add source files
target_sources(AgentSynth PRIVATE
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
    Source/ShortcutManager.h
)

# Link against JUCE modules
target_link_libraries(AgentSynth PRIVATE
    AgentSynthCore # Link to our core library
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

target_compile_definitions(AgentSynth PRIVATE JUCE_WEB_BROWSER=0)

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

add_subdirectory(Tests)

