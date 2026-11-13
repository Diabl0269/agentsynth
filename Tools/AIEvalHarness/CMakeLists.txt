include(${CMAKE_SOURCE_DIR}/cmake/AiHarness.cmake)

# UpdateManagerStub.cpp: MainComponent (built by --save-projects) needs an UpdateManager; see the file.
synth_add_harness(AIEvalHarness Main.cpp UpdateManagerStub.cpp)
