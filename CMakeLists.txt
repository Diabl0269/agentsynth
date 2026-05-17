cmake_minimum_required(VERSION 3.15)
project(Gravisynth)

find_package(JUCE CONFIG REQUIRED)

juce_add_gui_app(Gravisynth
    PRODUCT_NAME "Gravisynth"
    VERSION 0.13.2
    FORMATS AU VST3
    SOURCES
        Source/Main.cpp
        Source/MainComponent.cpp
        Source/AudioEngine.cpp
        Source/GravisynthUndoManager.cpp
        Source/PresetManager.cpp
        Source/UI/AIChatComponent.cpp
        Source/UI/ArrangementView.cpp
        Source/UI/GraphEditor.cpp
        Source/UI/ModMatrixComponent.cpp
        Source/UI/ModuleComponent.cpp
        Source/UI/SettingsWindow.cpp
        Source/Modules/AI/AIIntegrationService.cpp
        Source/Modules/AI/AIStateMapper.cpp
        Source/Modules/AI/OllamaProvider.cpp
)

target_compile_features(Gravisynth PRIVATE cxx_std_20)
target_link_libraries(Gravisynth PRIVATE juce::juce_audio_basics juce::juce_audio_devices juce::juce_audio_formats juce::juce_audio_processors juce::juce_audio_utils juce::juce_gui_basics juce::juce_gui_extra)
