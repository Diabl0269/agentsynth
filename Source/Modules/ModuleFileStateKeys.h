#pragma once

// The node "state" keys (ModuleBase::getExtraState) that hold a path on disk. One definition shared
// by the modules that write them and by synth::ModuleFileRefs, which rewrites them to bundle-relative
// refs inside a saved project — see docs/architecture/project-bundle.md#projectbundle-agsproj.
namespace synth::module_file_keys {

inline constexpr const char* kSamplerType = "Sampler";
inline constexpr const char* kSampleFile = "sampleFile";

inline constexpr const char* kWavetableType = "Wavetable";
inline constexpr const char* kWavetableFile = "wavetableFile";
inline constexpr const char* kWavetableFolder = "wavetableFolder";

} // namespace synth::module_file_keys
