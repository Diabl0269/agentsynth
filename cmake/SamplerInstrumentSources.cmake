# The Sampler's multi-sample instrument engine (regions, SFZ reader, polyphonic voices). Part of Core; their own
# list so the root file stays under the line cap.
target_sources(Core PRIVATE
    Source/Modules/Sampler/SamplerSampleData.cpp
    Source/Modules/Sampler/SamplerSampleData.h
    Source/Modules/Sampler/SamplerNoteNames.cpp
    Source/Modules/Sampler/SamplerNoteNames.h
    Source/Modules/Sampler/SamplerInstrument.cpp
    Source/Modules/Sampler/SamplerInstrument.h
    Source/Modules/Sampler/SfzReader.cpp
    Source/Modules/Sampler/SfzReader.h
    Source/Modules/Sampler/SamplerVoicePool.cpp
    Source/Modules/Sampler/SamplerVoicePool.h
    Source/Modules/Sampler/SamplerInstrumentPlayer.cpp
    Source/Modules/Sampler/SamplerInstrumentPlayer.h
    Source/Modules/Sampler/SamplerInstrumentSlot.cpp
    Source/Modules/Sampler/SamplerInstrumentSlot.h
    Source/Modules/Sampler/SamplerRegionEdit.cpp
    Source/Modules/Sampler/SamplerRegionEdit.h
    Source/Modules/Sampler/SamplerModuleInstrument.cpp
    Source/Modules/Sampler/SoundPackLibrary.cpp
    Source/Modules/Sampler/SoundPackLibrary.h
)
