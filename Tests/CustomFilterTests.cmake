# The Filter's Custom type tests, kept in their own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/FilterResonanceFitTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/ResonantLadderTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/FilterSweepClickTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Filter/AllocationCounter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Filter/PartitionedConvolverTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Filter/CustomFilterEngineTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Filter/CustomFilterBlendTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Filter/CustomFilterCutoffTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Filter/CustomFilterVowelTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Filter/CustomFilterPeakMorphTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Filter/FilterCustomModuleTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Filter/FilterPerNoteTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Filter/FilterPerNoteMonoRenderTests.cpp
)
