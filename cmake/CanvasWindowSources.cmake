# The canvas in its own window: the controller and MainComponent's wiring of it. Split out of AppUISources.cmake, which
# is at the file-size cap.
target_sources(AppUI PRIVATE
    Source/MainComponent/MainComponentCanvasWindow.cpp
    Source/UI/Layout/CanvasWindow/CanvasWindowController.cpp
    Source/UI/Layout/CanvasWindow/CanvasWindowController.h
)
