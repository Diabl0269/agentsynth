#include "DepthLevel.h"

#include <array>

namespace synth::theme {

namespace {
struct LevelInfo {
    DepthLevel level;
    const char* id;
    const char* label;
};

constexpr std::array<LevelInfo, kDepthLevelCount> kLevels{{
    {DepthLevel::Flat, "flat", "Flat"},
    {DepthLevel::Soft, "soft", "Soft"},
    {DepthLevel::Rich, "rich", "Rich"},
}};

const LevelInfo& infoFor(DepthLevel level) noexcept {
    const auto index = static_cast<size_t>(level);
    return kLevels[index < kLevels.size() ? index : 1];
}
} // namespace

const char* depthLevelId(DepthLevel level) noexcept { return infoFor(level).id; }

const char* depthLevelLabel(DepthLevel level) noexcept { return infoFor(level).label; }

DepthLevel depthLevelFromId(const juce::String& id) noexcept {
    for (const auto& info : kLevels)
        if (id == info.id)
            return info.level;
    return DepthLevel::Soft;
}

DepthLevel loadDepthLevel(juce::PropertiesFile& props) {
    return props.containsKey(depthLevelKey()) ? depthLevelFromId(props.getValue(depthLevelKey())) : DepthLevel::Soft;
}

void writeDepthLevel(juce::PropertiesFile& props, DepthLevel level) {
    props.setValue(depthLevelKey(), juce::String(depthLevelId(level)));
}

} // namespace synth::theme
