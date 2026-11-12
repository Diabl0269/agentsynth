# Displays that show the value the sound plays: the Wavetable display following its Position and Warp cables, and
# the guard over every in-card display (a display that reads a stored value while modulation moves the sound).
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/WavetableOscillatorModule/WavetableLiveDisplayTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/DisplaysShowModulatedValueTests.cpp
)
