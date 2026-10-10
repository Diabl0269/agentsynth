# The shared per-pass graph lookups, the undo history's graph snapshot cache and the mixer model built on them
# (docs/architecture/graph-queries.md), added to Core outside the main list in CMakeLists.txt, which sits at the
# repository's file-size cap. The Poly voice-graph switch (docs/modules/modulation.md#the-poly-toggle) is a graph query
# plus its change, so it lives here too.
target_sources(Core PRIVATE
    Source/AudioEngine/ConnectionIndex.h
    Source/AudioEngine/ConnectionIndex.cpp
    Source/AudioEngine/GraphSnapshotCache.h
    Source/AudioEngine/GraphSnapshotCache.cpp
    Source/AudioEngine/NodeUuidCache.h
    Source/AudioEngine/NodeUuidCache.cpp
    Source/MacroOwnerIndex.h
    Source/MacroOwnerIndex.cpp
    Source/Mixer/ChannelFlows/PolyVoiceGraph.h
    Source/Mixer/ChannelFlows/PolyVoiceGraph.cpp
    Source/Mixer/MixerModel/MixerModel.h
    Source/Mixer/MixerModel/MixerModelInternal.h
    Source/Mixer/MixerModel/MixerModelColumns.cpp
    Source/Mixer/MixerModel/MixerModelGraphView.cpp
    Source/Mixer/MixerModel/MixerModelInserts.cpp
    Source/Mixer/MixerModel/MixerModelSends.cpp
)
