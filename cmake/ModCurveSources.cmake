# The modulation response curve and the Attenuverter's out-of-line code (docs/modules/modulation.md), added to Core
# outside the main list in CMakeLists.txt, which sits at the repository's file-size cap.
target_sources(Core PRIVATE
    Source/Modules/ModCurve.h
    Source/Modules/ModCurve.cpp
    Source/Modules/AttenuverterModule.cpp
)
