# Settings > Audio in folding sections: the per-interface input layout store, a track's input found by name, and the
# tab's sections, Inputs table and Remembered interfaces.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/AudioInputLayoutStoreTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/AudioInputByNameTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Timeline/AudioInputMenuNamesTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Settings/AudioSettingsTabTests.cpp
)
