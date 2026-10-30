# Modulation cables on the shared output stage's Level, Pan and Mix (docs/modules/fx-modules.md), kept in their own list
# so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/OutputLevelCvTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/SavedCablesKeepChannelsTests.cpp
)
