#include "SequencerScaleSnap.h"

namespace synth {

const juce::StringArray& sequencerScaleChoices() {
    static const juce::StringArray choices = [] {
        juce::StringArray names;
        names.add("No scale");
        for (const auto& preset : builtInScalePresets())
            names.add(preset.name);
        names.add("Custom");
        return names;
    }();
    return choices;
}

int sequencerCustomScaleIndex() { return sequencerScaleChoices().size() - 1; }

MusicalScale sequencerScale(int rootPitchClass, int scaleIndex, std::uint16_t customMask) {
    const auto& presets = builtInScalePresets();
    const int index = std::clamp(scaleIndex, 0, sequencerCustomScaleIndex());
    if (index == 0)
        return makeScaleFromMask(rootPitchClass, 0xFFF);
    if (index == sequencerCustomScaleIndex())
        return makeScaleFromMask(rootPitchClass, customMask);
    return makeScaleFromMask(rootPitchClass, presets[(size_t)index - 1].mask);
}

void SequencerScaleSnap::update(int rootPitchClass, int scaleIndex, std::uint16_t customMask) noexcept {
    const int index = std::clamp(scaleIndex, 0, sequencerCustomScaleIndex());
    const std::uint16_t mask =
        index == sequencerCustomScaleIndex() ? (std::uint16_t)(customMask & 0xFFF) : (std::uint16_t)0;
    if (rootPitchClass == root_ && index == index_ && mask == mask_)
        return;
    root_ = rootPitchClass;
    index_ = index;
    mask_ = mask;
    // The name stays empty, so rebuilding never allocates.
    scale_ = sequencerScale(rootPitchClass, index, customMask);
    table_ = scale_.snapTable();
}

int sequencerPlayedPitch(int writtenPitch, int transpose, int rootPitchClass, int scaleIndex,
                         std::uint16_t customMask) {
    const auto scale = sequencerScale(rootPitchClass, scaleIndex, customMask);
    return scale.snapPitch(std::clamp(writtenPitch + transpose, 0, 127));
}

} // namespace synth
