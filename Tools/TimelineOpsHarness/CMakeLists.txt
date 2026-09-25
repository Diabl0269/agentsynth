include(${CMAKE_SOURCE_DIR}/cmake/AiHarness.cmake)

synth_add_harness(TimelineOpsHarness Main.cpp)

target_compile_definitions(TimelineOpsHarness PRIVATE
    TIMELINE_OPS_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Fixtures"
)
