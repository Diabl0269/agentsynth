# The Oscillator voice tests, kept in their own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/OscillatorMonoCVInPlaceTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/OscillatorPulseWidthGlideTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/OscillatorVoiceFeelTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/OscillatorMonoFeelTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/ADSRLegatoTests.cpp
)
