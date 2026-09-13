add_executable(AIPatchHarness
    Main.cpp
    # AppUndoManager.cpp (compiled into Core) references GraphEditor via SafePointer for its
    # UI-refresh callbacks; Core doesn't compile GraphEditor itself (only AgentSynth/Tests do),
    # so any executable linking Core must supply it too or the link fails on GraphEditor's vtable
    # and typeinfo.
    ${CMAKE_SOURCE_DIR}/Source/UI/GraphEditor/GraphEditor.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/GraphEditor/GraphEditorCables.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/GraphEditor/GraphEditorConnections.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/GraphEditor/GraphEditorSmartConnections.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/GraphEditor/GraphEditorSmartConnectionsApply.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/GraphEditor/GraphEditorCanvas.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/GraphEditor/GraphEditorSelection.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/GraphEditor/GraphEditorMacroGeometry.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/GraphEditor/GraphEditorMacroGrouping.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/GraphEditor/GraphEditorMacroPortSplice.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/GraphEditor/GraphEditorChannels.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/GraphEditor/GraphEditorMacroPorts.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/GraphEditor/GraphEditorCommands.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/GraphEditor/GraphEditorDragDrop.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/GraphEditor/GraphEditorStereoWiring.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/GraphEditor/GraphEditorPersistence.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/ModuleComponent.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/MacroCardComponent.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/MacroPortConfigDialog.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/ModMatrixComponent.cpp
)

target_link_libraries(AIPatchHarness PRIVATE
    Core
    juce::juce_core
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_audio_processors
)

target_include_directories(AIPatchHarness PRIVATE
    ${CMAKE_SOURCE_DIR}/Source
)
