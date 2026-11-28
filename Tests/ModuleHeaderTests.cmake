# Tests of a card's header buttons and its shared panel picture, kept in their own list so Tests/CMakeLists.txt
# stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModuleComponent/ModuleHeaderMuteTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModuleComponent/CardPanelPictureTests.cpp
)
