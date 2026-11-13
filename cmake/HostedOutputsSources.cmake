# The Hosted Plugin card's outputs strip (fold line, aux jack motion) and its Outputs panel (docs/control/plugin-card-layout.md),
# appended to APPUI_SOURCES because cmake/AppUISources.cmake is at the file-size cap.
list(APPEND APPUI_SOURCES
    Source/UI/Graph/HostedOutputs/HostedOutputsStrip.h
    Source/UI/Graph/HostedOutputs/HostedOutputsStrip.cpp
    Source/UI/Graph/HostedOutputs/HostedOutputsPanel.h
    Source/UI/Graph/HostedOutputs/HostedOutputsPanel.cpp
)
