#pragma once

// The picked pad's controls under the Drum Kit's pad grid: its colour dot, name and note on top, then Level, Tune and
// Decay (plus Start for an audio-file pad). Switching pads cross-fades the panel. docs/modules/drum-kit.md#the-card.

#include "Modules/DrumKit/DrumKitModule.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"
#include "UI/Layout/UIAnimation.h"
#include <array>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

class DrumPadControls : public juce::Component {
public:
    static constexpr int kHeaderHeight = 20;
    static constexpr int kHeaderGap = 2;
    static constexpr double kFadeOutMs = 80.0;
    static constexpr double kFadeInMs = 160.0;
    static constexpr double kReducedMs = 80.0;

    /** The panel's height, the same for every pad. */
    static int height();

    /** The kit must outlive this. */
    explicit DrumPadControls(DrumKitModule& kit);
    ~DrumPadControls() override;

    /** Shows pad `index`'s controls, cross-fading from the shown pad when `animate`. */
    void showPad(int index, bool animate);
    /** The pad the knobs are bound to now (during a cross-fade, the old one until the panel is clear). */
    int boundPad() const noexcept { return bound_; }
    /** Re-reads the bound pad's name, note, kind and whether it plays a recording. */
    void refresh();

    juce::Slider& knob(DrumKitModule::PadParam which) noexcept { return knobs_[(size_t)which]; }
    juce::Label& nameLabel() noexcept { return name_; }
    juce::Label& noteLabel() noexcept { return note_; }

    /** Called when Escape is pressed on a knob; the owner moves focus back to the pad. */
    std::function<void()> onEscape;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void lookAndFeelChanged() override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    class PadKnob : public CardKnobSlider {
    public:
        std::function<void()> onEscape;
        bool keyPressed(const juce::KeyPress& key) override;
    };

    void bind(int index);

    DrumKitModule& kit_;
    std::array<PadKnob, 4> knobs_;
    std::array<juce::Label, 4> captions_;
    std::array<std::unique_ptr<juce::SliderParameterAttachment>, 4> attachments_;
    juce::Label name_;
    juce::Label note_;
    juce::Colour dot_;
    int bound_ = -1;
    int pending_ = -1;
    juce::VBlankAnimatorUpdater updater_{this};
    AnimationDriver fade_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumPadControls)
};

} // namespace synth::ui
