# The top bar's button (docs/layout/chrome.md#toolbar): its state and art, and the look-and-feel unit
# that paints it. In Core because AppLookAndFeel, whose drawToolbarButton paints it, is compiled there.
target_sources(Core PRIVATE
    Source/UI/Chrome/ToolbarButton/ToolbarButton.h
    Source/UI/Chrome/ToolbarButton/ToolbarButton.cpp
    Source/UI/Chrome/ToolbarButton/ToolbarButtonArt.cpp
    Source/UI/Theme/AppLookAndFeel/AppLookAndFeelToolbarButton.cpp
)
