add_executable(AIEvalHarness
    Main.cpp
    # AppUndoManager.cpp (compiled into Core) references GraphEditor via SafePointer for its
    # UI-refresh callbacks; Core doesn't compile GraphEditor itself (only AgentSynth/Tests do),
    # so any executable linking Core must supply it too or the link fails on GraphEditor's vtable
    # and typeinfo.
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditor.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorCables.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorConnections.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorSmartConnections.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorSmartConnectionsApply.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorCanvas.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorSelection.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorMacroGeometry.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorMacroGrouping.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorMacroPortSplice.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorChannels.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorMacroPorts.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorCommands.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorDragDrop.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorStereoWiring.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorPersistence.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModuleComponent/ModuleComponent.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModuleComponent/ModuleComponentEQCard.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModuleComponent/ModuleComponentAudioDrop.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModuleComponent/ModuleComponentWavetable.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModuleComponent/ModuleComponentLayout.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModuleComponent/ModuleComponentPaint.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModuleComponent/ModuleComponentInteraction.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Macros/MacroCardComponent.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Macros/MacroPortConfigDialog/MacroPortConfigDialogLifecycle.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Macros/MacroPortConfigDialog/MacroPortConfigDialogRowOrdering.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Macros/MacroPortConfigDialog/MacroPortConfigDialogTestSeams.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Macros/MacroPortConfigDialog/MacroPortConfigDialogAutoPortPrompt.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModMatrixComponent.cpp
)

target_link_libraries(AIEvalHarness PRIVATE
    Core
    juce::juce_core
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_audio_processors
)

target_include_directories(AIEvalHarness PRIVATE
    ${CMAKE_SOURCE_DIR}/Source
)
