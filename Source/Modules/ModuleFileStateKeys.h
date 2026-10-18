#pragma once

// The node "state" keys (ModuleBase::getExtraState) that hold a path on disk. One definition shared
// by the modules that write them and by synth::ModuleFileRefs, which rewrites them to bundle-relative
// refs inside a saved project — see docs/architecture/project-bundle.md#projectbundle-agsproj.
namespace synth::module_file_keys {

inline constexpr const char* kSamplerType = "Sampler";
inline constexpr const char* kSampleFile = "sampleFile";
// The Sampler's Instrument mode saves exactly one source: an SFZ (folder + path inside it), a sound pack entry, or
// dropped files (paths + the placement of each).
inline constexpr const char* kInstrumentFolder = "instrumentFolder";
inline constexpr const char* kInstrumentSfz = "instrumentSfz";
inline constexpr const char* kInstrumentPack = "instrumentPack";
inline constexpr const char* kInstrumentFiles = "instrumentFiles";
inline constexpr const char* kInstrumentRegions = "instrumentRegions";
inline constexpr const char* kInstrumentName = "instrumentName";

inline constexpr const char* kWavetableType = "Wavetable";
inline constexpr const char* kWavetableFile = "wavetableFile";
inline constexpr const char* kWavetableFolder = "wavetableFolder";

} // namespace synth::module_file_keys
