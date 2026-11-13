# The sequencers following the song key (included from Tests/SongKeyTests.cmake).
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Sequencer/SequencerSongKeyTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/FollowKeyFieldTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/SequencerKey/SequencerKeyFieldTests.cpp
)
