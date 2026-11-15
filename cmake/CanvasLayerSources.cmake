# The canvas' static layer, the picture of what is drawn under the cards while only the flow dots move
# (docs/layout/rendering.md#an-idle-canvas-with-moving-flow-dots-draws-one-picture), appended to APPUI_SOURCES
# (cmake/AppUISources.cmake is at the file-size cap).
list(APPEND APPUI_SOURCES
    Source/UI/Graph/CanvasLayer/CanvasStaticLayer.h
    Source/UI/Graph/CanvasLayer/CanvasStaticLayer.cpp
    Source/UI/Graph/CanvasLayer/CableFlowDots.h
    Source/UI/Graph/CanvasLayer/CableFlowDots.cpp
)
