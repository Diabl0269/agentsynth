# Mixer insert and send rows (docs/mixer/mixer.md#reordering-inserts, docs/mixer/sends-and-buses.md) and the keys a focused mixer
# control takes, kept in their own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Mixer/MixerSpaceKeyTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Mixer/MixerModel/MixerModelDanglingBranchTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Mixer/MixerInsertListDryPathTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Mixer/MixerInsertListReorderTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Mixer/MixerRowMoveAndRevealTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Mixer/MixerTopBarRingTests.cpp
)
