# The shortcut table's tests: defaults, conflicts, key matching and the one-shot migrations of saved bindings,
# kept in their own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/App/ShortcutManager/ShortcutManagerActionRegressionTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/ShortcutManager/ShortcutManagerAddTrackKeyTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/ShortcutManager/ShortcutManagerCommandPaletteKeyTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/ShortcutManager/ShortcutManagerMoveItemKeyTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/ShortcutManager/ShortcutManagerApiTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/ShortcutManager/ShortcutManagerBottomPanelMigrationTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/ShortcutManager/ShortcutManagerConflictTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/ShortcutManager/ShortcutManagerDefaultsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/ShortcutManager/ShortcutManagerKeyMatchingTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/ShortcutManager/ShortcutManagerTransportActionsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/ShortcutManager/ShortcutManagerTransportChordsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/ShortcutManager/ShortcutManagerCursorActionsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/ShortcutManager/ShortcutManagerMarkerActionsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/ShortcutManager/ShortcutManagerSelectionStepTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/ShortcutManager/ShortcutManagerRemovedActionsTests.cpp
)
