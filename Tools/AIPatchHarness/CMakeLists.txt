add_executable(AIPatchHarness
    Main.cpp
    # AppUndoManager.cpp (compiled into Core) references GraphEditor via SafePointer for its
    # UI-refresh callbacks; Core doesn't compile GraphEditor itself (only AgentSynth/Tests do),
    # so any executable linking Core must supply it too or the link fails on GraphEditor's vtable
    # and typeinfo.
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditor.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorCables.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorConnections.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorModuleTitles.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorSmartConnections.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/SmartConnectionEngine/SmartConnectionEngine.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/SmartConnectionEngine/SmartConnectionEngineApply.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorCanvas.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorSelection.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorMacroApi.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorMacroCards.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorMacroPrompts.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/MacroGroupController/MacroGroupController.h
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/MacroGroupController/MacroGroupControllerGeometry.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/MacroGroupController/MacroGroupControllerGrouping.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/MacroGroupController/MacroGroupControllerBypassMute.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/MacroGroupController/MacroGroupControllerPortSplice.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/MacroGroupController/MacroGroupControllerPorts.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorChannels.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorCommands.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorDragDrop.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphDragDropController/GraphDragDropController.h
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphDragDropController/GraphDragDropController.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorStereoWiring.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/GraphEditor/GraphEditorPersistence.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModuleComponent/ModuleComponent.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModuleComponent/ModuleComponentEQCard.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModuleComponent/ModuleComponentAudioDrop.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModuleComponent/ModuleComponentWavetable.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModuleComponent/ModuleComponentLayout.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModuleComponent/ModuleComponentPaint.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModuleComponent/ModuleComponentInteraction.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModuleComponent/ModuleComponentEnvelopeCard.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModuleComponent/ModuleComponentMidiLearn.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/ModuleViews/CurveEditor/CurveModel.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/ModuleViews/CurveEditor/CurveEditorGeometry.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/ModuleViews/CurveEditor/CurveEditorComponent.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/ModuleViews/CurveEditor/CurveEditorPaint.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Macros/MacroCardComponent.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Macros/MacroPortConfigDialog/MacroPortConfigDialogLifecycle.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Macros/MacroPortConfigDialog/MacroPortConfigDialogRowOrdering.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Macros/MacroPortConfigDialog/MacroPortConfigDialogTestSeams.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Macros/MacroPortConfigDialog/MacroPortConfigDialogAutoPortPrompt.cpp
    ${CMAKE_SOURCE_DIR}/Source/UI/Graph/ModMatrixComponent.cpp
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
