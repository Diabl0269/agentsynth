# The sound index (FRO989) and the Sounds view in the left sidebar (FRO990), kept in their own list so
# Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundIndexTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundListsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundNameGuessTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundBrowserViewTests.cpp
)
