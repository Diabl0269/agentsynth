# The mod dot: its gesture, its panel (sources and Add source pages), the modulation row and the one wiring every
# surface that lists a modulation uses (docs/modules/modulation.md#the-mod-dot-menu). Split out of AppUISources.cmake
# to keep that list under the file-size cap; appended to APPUI_SOURCES by it.
list(APPEND APPUI_SOURCES
    Source/UI/Graph/ModDot/KnobModSources.h
    Source/UI/Graph/ModDot/KnobModSources.cpp
    Source/UI/Graph/ModDot/ModDotButton.h
    Source/UI/Graph/ModDot/ModDotCanvasPicker.h
    Source/UI/Graph/ModDot/ModDotCanvasPicker.cpp
    Source/UI/Graph/ModDot/ModDotPanelFrame.h
    Source/UI/Graph/ModDot/ModDotPanelFrame.cpp
    Source/UI/Graph/ModDot/ModDotPanelLaunch.h
    Source/UI/Graph/ModDot/ModDotPanelLaunch.cpp
    Source/UI/Graph/ModDot/ModDotPanelGeometry.h
    Source/UI/Graph/ModDot/ModDotPanelGeometry.cpp
    Source/UI/Graph/ModDot/ModDotController.h
    Source/UI/Graph/ModDot/ModDotController.cpp
    Source/UI/Graph/ModDot/ModDotTooltip.h
    Source/UI/Graph/ModDot/ModDotTooltip.cpp
    Source/UI/Graph/ModDot/ModDotAddSourceParts.h
    Source/UI/Graph/ModDot/ModDotAddSourcePage.h
    Source/UI/Graph/ModDot/ModDotAmountBar.h
    Source/UI/Graph/ModDot/ModDotGlyphButton.h
    Source/UI/Graph/ModDot/ModDotMotion.h
    Source/UI/Graph/ModDot/ModDotPage.h
    Source/UI/Graph/ModDot/ModDotPalette.h
    Source/UI/Graph/ModDot/ModDotPopover.h
    Source/UI/Graph/ModDot/ModDotSourceRow.h
    Source/UI/Graph/ModDot/ModDotSourcesPage.h
    Source/UI/Graph/ModDot/ModSourceRowWiring.h
    Source/UI/Graph/ModDot/ModSourceCatalog.h
    Source/UI/Graph/ModDot/ModDotAddSourcePage.cpp
    Source/UI/Graph/ModDot/ModDotAmountBar.cpp
    Source/UI/Graph/ModDot/ModDotControllerPopover.cpp
    Source/UI/Graph/ModDot/ModDotGlyphs.cpp
    Source/UI/Graph/ModDot/ModDotPopover.cpp
    Source/UI/Graph/ModDot/ModDotSourceRow.cpp
    Source/UI/Graph/ModDot/ModDotSourcesPage.cpp
    Source/UI/Graph/ModDot/ModSourceRowWiring.cpp
    Source/UI/Graph/ModDot/ModSourceCatalog.cpp
)
