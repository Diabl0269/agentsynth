# The card canvas guards (docs/layout/module-card.md): a caption as tall as its text, a card's mode switch leaving the
# modules around it where they were. Kept in their own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardBody/DefaultLayouts/CaptionHeightFitTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardBody/CardModeToggleNeighboursTests.cpp
)
