# The sound index (FRO989), the Sounds view in the left sidebar (FRO990), its filters (FRO991) and the preview player
# (FRO992) and dragging a sound (FRO993), kept in their own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundIndexTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundListsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundNameGuessTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundBrowserViewTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundBrowserLayoutTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundDragTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundFilterTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundFilterViewTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundBrowserKeyboardReachTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundPackOfferTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundFolderViewTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundFolderBookTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundPreviewPlayerTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundBrowser/SoundPreviewViewTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/MainComponent/MainComponentSoundDropTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/GraphDragDropController/GraphDragDropControllerSoundTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModuleComponent/ModuleComponentSoundDropTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Controls/RangeSliderTests.cpp
)
