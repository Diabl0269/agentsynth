# The command palette (Cmd+K): its ranking, the picker's per-row extras it uses, the picker's first-row guard, and the palette through the real
# window, kept in their own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/CommandPalette/CommandPaletteRowsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/CommandPalette/CommandPaletteMainComponentTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModMatrixPickerRowExtrasTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModMatrixPickerSwitchGlideTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModMatrixPickerFirstRowTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/TagHueTests.cpp
)
