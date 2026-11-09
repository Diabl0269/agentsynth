# A cable dragged onto a folded macro's card: its jacks, the drop hint and the slide to its port (docs/macros/ports.md),
# kept in their own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Macros/MacroCardJackTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Macros/MacroCardDropHintTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Macros/MacroCardDropSlideTests.cpp
)
