# The shared per-pass graph lookups and the mixer model built on them (docs/architecture/graph-queries.md), added to
# Core outside the main list in CMakeLists.txt, which sits at the repository's file-size cap.
target_sources(Core PRIVATE
    Source/AudioEngine/ConnectionIndex.h
    Source/AudioEngine/ConnectionIndex.cpp
    Source/AudioEngine/NodeUuidCache.h
    Source/AudioEngine/NodeUuidCache.cpp
    Source/MacroOwnerIndex.h
    Source/MacroOwnerIndex.cpp
    Source/Mixer/MixerModel/MixerModel.h
    Source/Mixer/MixerModel/MixerModelInternal.h
    Source/Mixer/MixerModel/MixerModelColumns.cpp
    Source/Mixer/MixerModel/MixerModelGraphView.cpp
    Source/Mixer/MixerModel/MixerModelInserts.cpp
    Source/Mixer/MixerModel/MixerModelSends.cpp
)
