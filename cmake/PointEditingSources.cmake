# Moving and duplicating points from the keyboard (docs/layout/animation.md#motion-rules, docs/control/point-editing.md):
# the shared point model both point editors run on (PointEditing/: selection, box select, group move, clipboard, keys), the shared
# glide model, the hold's frame glue, the eased drawn offsets, the keyboard drags of the automation lane and
# the curve editor, where Cmd+D puts the copies, and the units that glide moved things back on undo and redo
# (RestoreWatch and the per-surface RestoreGlide files). Appended to APPUI_SOURCES (cmake/AppUISources.cmake is at the
# file-size cap).
list(APPEND APPUI_SOURCES
    Source/UI/Layout/KeyGlide/KeyGlide.h
    Source/UI/Layout/KeyGlide/KeyGlide.cpp
    Source/UI/Layout/KeyGlide/KeyGlideHost.h
    Source/UI/Layout/KeyGlide/KeyGlideHost.cpp
    Source/UI/Layout/KeyGlide/KeyHold.h
    Source/UI/Layout/KeyGlide/KeyHold.cpp
    Source/UI/Layout/KeyGlide/KeyedTween.h
    Source/UI/Layout/KeyGlide/KeyedTween.cpp
    Source/UI/Layout/KeyGlide/KeyedPointTween.h
    Source/UI/Layout/PointEditing/PointSelectionModel.h
    Source/UI/Layout/PointEditing/PointSelectionModel.cpp
    Source/UI/Layout/PointEditing/PointGroupMove.h
    Source/UI/Layout/PointEditing/PointBoxSelect.h
    Source/UI/Layout/PointEditing/PointBoxSelect.cpp
    Source/UI/Layout/PointEditing/PointGroupMove.cpp
    Source/UI/ModuleViews/CurveEditor/CurveEditorBoxSelect.cpp
    Source/UI/Layout/PointEditing/PointClipboard.h
    Source/UI/Layout/PointEditing/PointEditorKeys.h
    Source/UI/Layout/PointEditorDuplicate.h
    Source/UI/Layout/PointEditorDuplicate.cpp
    Source/UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabDuplicatePoints.cpp
    Source/UI/Timeline/AutomationLaneEditorKeyGlide.cpp
    Source/UI/ModuleViews/CurveEditor/CurveEditorKeyGlide.cpp
    Source/UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h
    Source/UI/Layout/KeyGlide/RestoreWatch.h
    Source/UI/ModuleViews/MorphMap/MorphMapRestoreGlide.cpp
    Source/UI/ModuleViews/CurveEditor/CurveEditorRestoreGlide.cpp
    Source/UI/Timeline/AutomationLaneEditorRestoreGlide.cpp
    Source/UI/Timeline/TimelineClipLaneArea/TimelineClipLaneRestoreGlide.cpp
)
