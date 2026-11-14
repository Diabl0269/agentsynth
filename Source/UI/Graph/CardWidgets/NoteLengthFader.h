#pragma once

#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

namespace synth::ui {

/**
 * A vertical fader for the app's note-division choice (eight straight, eight triplet, eight dotted values):
 * the fader steps the eight lengths and a small S | T | D picker under it picks the feel. Both write the one
 * choice parameter (no extra parameter, nothing new in a save): the picker keeps the length, the fader keeps the
 * feel. The picker is a child, so the fader's cell is the same size as any vertical fader's.
 * docs/layout/module-card.md#faders-switches-and-steppers.
 */
class NoteLengthFader : public CardFader {
public:
    NoteLengthFader();
    ~NoteLengthFader() override;

    /** True when `param` lists the note divisions this fader splits (straight, triplet, dotted, eight each). */
    static bool suits(const juce::AudioParameterChoice& param);

    /** Call after the slider attachment: re-maps the travel to the eight lengths of the parameter's current feel
     *  and binds the picker. The slider's value stays the parameter's own index. */
    void bindTo(juce::AudioParameterChoice& param);

    CardSegmentedSwitch& getPicker() noexcept { return picker_; }

    static constexpr int kPickerHeight = 18;

    void resized() override;

protected:
    void valueBoxStyled() override;

private:
    CardSegmentedSwitch picker_;
    juce::AudioParameterChoice* param_ = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteLengthFader)
};

} // namespace synth::ui
