# The mod dot's and the port connections panel's tests (docs/modules/modulation.md#the-mod-dot-menu,
# docs/layout/cables.md#port-connections-panel), kept in their own list so Tests/CMakeLists.txt stays under the
# file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModDot/ModDotGestureTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModDot/ModDotTooltipGeometryTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModDot/KnobModSourcesTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModDot/ModDotAddSourceTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModDot/ModDotMacroRoutingTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModDot/ModDotPopoverTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModDot/ModDotSourceRemovalMotionTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModDot/ModDotRowPolishTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModDot/ModDotDoubleClickTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModDot/ModDotDoubleClickZoomTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModDot/ModDotInlineAddTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModDot/ModDotPickOnCanvasTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModDot/ModDotPanelGeometryTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/PortPanel/PortPanelClickTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/PortPanel/PortPanelRowsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/PortPanel/PortPanelAddConnectionTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/PortPanel/PortPanelMacroCardTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/PortPanel/PortPanelDimTests.cpp
)
