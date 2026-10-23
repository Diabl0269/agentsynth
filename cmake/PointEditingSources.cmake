# Moving and duplicating points from the keyboard (docs/layout/animation.md#motion-rules, docs/control/point-editing.md):
# the shared glide model, the hold's frame glue, the eased drawn offsets, the keyboard drags of the automation lane and
# the curve editor, and where Cmd+D puts the copies. Appended to APPUI_SOURCES (cmake/AppUISources.cmake is at the
# file-size cap).
list(APPEND APPUI_SOURCES
    Source/UI/Layout/KeyGlide/KeyGlide.h
    Source/UI/Layout/KeyGlide/KeyGlide.cpp
    Source/UI/Layout/KeyGlide/KeyGlideHost.h
    Source/UI/Layout/KeyGlide/KeyGlideHost.cpp
    Source/UI/Layout/KeyGlide/KeyHold.h
    Source/UI/Layout/KeyGlide/KeyHold.cpp
    Source/UI/Layout/KeyGlide/KeyedPointTween.h
    Source/UI/Layout/KeyGlide/KeyedPointTween.cpp
    Source/UI/Timeline/AutomationLaneEditorKeyGlide.cpp
    Source/UI/ModuleViews/CurveEditor/CurveEditorKeyGlide.cpp
    Source/UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h
)
