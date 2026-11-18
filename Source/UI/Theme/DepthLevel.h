#pragma once

#include "UI/Theme/Theme.h"
#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>

namespace synth::theme {

// The Depth look (Settings > Appearance > Controls > Depth): its ids, labels and persistence. The enum itself
// (Flat, Soft, Rich: the picker order) is in Theme.h beside Treatment. A user setting, never part of a theme file;
// Soft is today's look.
inline constexpr int kDepthLevelCount = 3;

// Persisted ids and picker labels: never rename a shipped id. A missing or unknown id loads as Soft.
const char* depthLevelId(DepthLevel level) noexcept;
const char* depthLevelLabel(DepthLevel level) noexcept;
DepthLevel depthLevelFromId(const juce::String& id) noexcept;

inline const char* depthLevelKey() noexcept { return "depthLevel"; }

// A missing or unknown key loads as Soft.
DepthLevel loadDepthLevel(juce::PropertiesFile& props);
// Writes the key without flushing; call props.saveIfNeeded() at the commit point.
void writeDepthLevel(juce::PropertiesFile& props, DepthLevel level);

} // namespace synth::theme
