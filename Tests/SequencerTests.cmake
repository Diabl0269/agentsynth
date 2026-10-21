# The Sequencer and Poly Sequencer tests kept out of Tests/CMakeLists.txt so it stays under the file-size cap: the
# pattern data and its JSON, playback of both modules, the transport-synced clock, and how old parameters and new
# state load (docs/modules/sequencer-design.md), and the card: the step grid and the lane chip row.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Sequencer/SequencerModuleTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Sequencer/PolySequencerModuleTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Sequencer/SequencerSyncedTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Sequencer/SequencerPatternTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Sequencer/SequencerStateTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/StepGrid/StepGridModelTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/StepGrid/StepGridCardTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/LaneChips/LaneChipRowTests.cpp
)
