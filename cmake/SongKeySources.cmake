# The song key: Core's note histogram for Detect from notes, then the key field, the transport bar's Song key field
# and the key picker popover, appended to APPUI_SOURCES (CMakeLists.txt and cmake/AppUISources.cmake are at the
# file-size cap).
target_sources(Core PRIVATE
    Source/Timeline/SongKeyNotes.cpp
    Source/Timeline/SongKeyNotes.h
)
list(APPEND APPUI_SOURCES
    Source/UI/Layout/KeyField.cpp
    Source/UI/Layout/KeyField.h
    Source/UI/Layout/KeyPicker/KeyPicker.cpp
    Source/UI/Layout/KeyPicker/KeyPicker.h
    Source/UI/Layout/KeyPicker/KeyPickerKeyboard.cpp
    Source/UI/Layout/KeyPicker/KeyPickerKeyboard.h
    Source/UI/Layout/KeyPicker/KeyPickerPalette.h
    Source/UI/Layout/KeyPicker/KeyPickerParts.cpp
    Source/UI/Layout/KeyPicker/KeyPickerParts.h
    Source/UI/Timeline/SongKeyField.cpp
    Source/UI/Timeline/SongKeyField.h
)
include(cmake/SequencerKeySources.cmake)
