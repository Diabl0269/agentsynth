# The app-wide undo manager: graph, parameter, module-state, mixer-view, transport and history edits. Part of Core;
# its own list so the root file stays under the line cap.
target_sources(Core PRIVATE
    Source/AppUndoManager.cpp
    Source/AppUndoManager.h
    Source/AppUndoManagerMixerView.cpp
    Source/AppUndoManagerTransport.cpp
    Source/AppUndoManagerHistory.cpp
    Source/AppUndoManagerSnapshotSize.h
    Source/AppUndoManagerSnapshotSize.cpp
    Source/AppUndoManagerNodeState.cpp
)
