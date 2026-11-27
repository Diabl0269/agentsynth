# Hum-to-melody: the DSP (pitch tracking, note cutting, quantising; no document types) and the host operation behind
# the clip menu's Convert to melody. Included from cmake/SongKeySources.cmake because CMakeLists.txt is at the
# file-size cap.
target_sources(Core PRIVATE
    Source/Timeline/Melody/HumToMelody.cpp
    Source/Timeline/Melody/HumToMelody.h
    Source/Timeline/Melody/MelodyQuantize.cpp
    Source/Timeline/Melody/MelodyQuantize.h
    Source/Timeline/Melody/NoteCutter.cpp
    Source/Timeline/Melody/NoteCutter.h
    Source/Timeline/Melody/PitchTracker.cpp
    Source/Timeline/Melody/PitchTracker.h
)
list(APPEND APPUI_SOURCES
    Source/MainComponent/MainComponentConvertToMelody.cpp
)
