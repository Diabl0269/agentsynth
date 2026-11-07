# What a card drag on the patch canvas draws and drives beyond the card itself: the edge scroll, the dot grid and the view fly
# (docs/layout/layout.md#edge-scroll-while-dragging), appended to APPUI_SOURCES by AppUISources.cmake.
list(APPEND APPUI_SOURCES
    Source/UI/Graph/CanvasEdgeScroll/CanvasEdgeScroll.h
    Source/UI/Graph/CanvasEdgeScroll/CanvasEdgeScroller.h
    Source/UI/Graph/CanvasEdgeScroll/CanvasEdgeScroller.cpp
    Source/UI/Graph/CanvasEdgeScroll/CanvasEdgeScrollDriver.h
    Source/UI/Graph/CanvasEdgeScroll/CanvasEdgeScrollDriver.cpp
    Source/UI/Graph/DragPreviewGrid/DragPreviewGrid.h
    Source/UI/Graph/DragPreviewGrid/DragPreviewGrid.cpp
    Source/UI/Graph/GraphEditor/GraphEditorViewFly.cpp
)
