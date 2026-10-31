# The modulation curve's views (docs/modules/modulation.md#the-modulation-curve): the box, the enlarged window, the chip and
# the preset store. Appended to APPUI_SOURCES because CMakeLists.txt is at the file-size cap.
list(APPEND APPUI_SOURCES
    Source/UI/ModuleViews/ModCurve/ModCurveEditTarget.h
    Source/UI/ModuleViews/ModCurve/ModCurveEditTarget.cpp
    Source/UI/ModuleViews/ModCurve/ModCurveEditorView.h
    Source/UI/ModuleViews/ModCurve/ModCurveEditorView.cpp
    Source/UI/ModuleViews/ModCurve/ModCurveRangeHandle.h
    Source/UI/ModuleViews/ModCurve/ModCurveRangeHandle.cpp
    Source/UI/ModuleViews/ModCurve/ModCurveBox.h
    Source/UI/ModuleViews/ModCurve/ModCurveBox.cpp
    Source/UI/ModuleViews/ModCurve/ModCurveWindow.h
    Source/UI/ModuleViews/ModCurve/ModCurveWindow.cpp
    Source/UI/ModuleViews/ModCurve/ModCurveChip.h
    Source/UI/ModuleViews/ModCurve/ModCurveChip.cpp
    Source/UI/ModuleViews/ModCurve/ModCurvePopover.h
    Source/UI/ModuleViews/ModCurve/ModCurvePopover.cpp
    Source/UI/ModuleViews/ModCurve/ModCurvePresetStore.h
    Source/UI/ModuleViews/ModCurve/ModCurvePresetStore.cpp
    Source/UI/Graph/ModMatrixCurveColumn.h
    Source/UI/Graph/ModMatrixCurveColumn.cpp
    Source/UI/Graph/GraphEditor/GraphEditorCableMenu.h
    Source/UI/Graph/GraphEditor/GraphEditorCableMenu.cpp
    Source/UI/Graph/GraphEditor/GraphEditorCableCurveChips.h
    Source/UI/Graph/GraphEditor/GraphEditorCableCurveChips.cpp
)
