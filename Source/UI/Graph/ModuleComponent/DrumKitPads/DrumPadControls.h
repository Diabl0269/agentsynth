#pragma once

// The picked pad's controls under the Drum Kit's pad grid: its colour dot, name and note on top, the Sound row (what
// the pad plays, plus Open for an instrument pad), then Level, Tune and Decay (plus Start for an audio-file pad, Plays
// pitch for an instrument pad). Switching pads never fades the panel: the text changes at once and the four knobs glide
// from the old pad's values to the new pad's. docs/modules/drum-kit.md#the-card.

#include "Modules/DrumKit/DrumKitModule.h"
#include "TestMode/TestModeStateSource.h"
#include "UI/Graph/CardWidgets/CardStepper.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerInstrumentPicker.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Timeline/TrackShowModuleButton.h"
#include <array>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

namespace synth::ui {

class DrumPadControls
    : public juce::Component
    , public synth::testmode::StateSource {
public:
    static constexpr int kHeaderHeight = 20;
    static constexpr int kHeaderGap = 2;
    static constexpr int kSoundLabelWidth = 48;
    static constexpr int kRowGap = 4;
    static constexpr int kShownGap = 4;
    static constexpr int kShownHeight = 22;
    static constexpr double kGlideMs = 160.0;
    static constexpr double kReducedMs = 80.0;

    /** The panel's height, the same for every pad. */
    static int height();

    /** The kit must outlive this. */
    explicit DrumPadControls(DrumKitModule& kit);
    ~DrumPadControls() override;

    /** Shows pad `index`'s controls at once; with `animate` each knob glides from the old pad's value to the new
     *  pad's (never fading the panel). */
    void showPad(int index, bool animate);
    /** The pad the knobs are bound to now. */
    int boundPad() const noexcept { return bound_; }
    /** Re-reads the bound pad's name, note, kind and whether it plays a recording. */
    void refresh();
    /** True while a knob value, a column or the pick's ring cross-fade is still moving. */
    bool isMoving() const noexcept;
    void addTestModeState(juce::DynamicObject& into) const override;

    /** A pad cable's ring on a knob that no longer shows its pad, still fading out after a pick. */
    struct LeavingRing {
        juce::Slider* knob = nullptr;
        float alpha = 0.0f;
    };
    /** The ring of `paramId` (a pad parameter) that is fading out of its knob after a pick, or none. */
    std::optional<LeavingRing> leavingRing(const juce::String& paramId) const;
    /** How opaque the picked pad's ring is: 0 to 1 over the pick, 1 at rest. */
    float arrivingRingAlpha() const noexcept { return ringProgress_; }
    /** The value of knob `which` as drawn now, 0 to 1 along its range. */
    double shownProportion(DrumKitModule::PadParam which) const;
    /** Tests: animate as if on screen, and step or land the running motions. */
    KeyedPointTween& valueGlideForTest() noexcept { return valueGlide_; }
    void stepMotionForTest(double ms);
    void completeMotionForTest();

    CardKnobSlider& knob(DrumKitModule::PadParam which) noexcept { return knobs_[(size_t)which]; }
    juce::Label& nameLabel() noexcept { return name_; }
    juce::Label& noteLabel() noexcept { return note_; }
    SamplerInstrumentPicker& soundButton() noexcept { return sound_; }
    TrackShowModuleButton& openButton() noexcept { return open_; }
    CardStepper& pitchStepper() noexcept { return pitch_; }

    /** What pad `index` plays, as {name, detail} ("Pluck 1", "instrument"); unset, the pad's name. */
    std::function<std::pair<juce::String, juce::String>(int index)> describeSound;
    /** The Sound row was pressed (click, Return or Space) for the bound pad. */
    std::function<void()> onSoundMenu;
    /** Open was pressed on an instrument pad. */
    std::function<void()> onOpenInstrument;
    /** Plays pitch stepped by `delta`, or typed. */
    std::function<void(int delta)> onPitchStep;
    std::function<void(const juce::String& text)> onPitchTyped;
    /** The Shown switch of the bound pad. */
    juce::ToggleButton& shownSwitch() noexcept { return shown_; }
    /** The switch was flipped for pad `index` (the owner records the undo step and may ask first). */
    std::function<void(int index, bool shown)> onShownChanged;

    /** After every bind or refresh: `knob` drives `param` (the bound pad's), with its tooltip set. */
    std::function<void(juce::Slider& knob, juce::RangedAudioParameter* param)> onKnobBound;
    /** Just before the knobs move from one pad to another. */
    std::function<void()> onBeforeRebind;
    /** True while an undo or redo restores the document: a knob value it changes glides. Unset: never. */
    std::function<bool()> isRestoring;

    /** The kit is already freed: leaks the knobs' attachments rather than detaching them from its parameters. */
    void releaseBindings();

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
        /** The parameter's own change moved the knob (a pick, an undo, automation): the proportion shown before
         *  and the new one. */
        std::function<void(double before, double after)> onModelValue;
        /** The user moved the knob (drag, keys, typing): whatever glide was running gives way. */
        std::function<void()> onUserValue;
        bool keyPressed(const juce::KeyPress& key) override;
        void valueChanged() override;
        /** The parameter this knob drives now. */
        void setModelParam(juce::RangedAudioParameter* param) noexcept { param_ = param; }
        /** Draws `proportion` (0 to 1 along the range) without telling the parameter. */
        void showProportion(double proportion);
        double shownProportion() const noexcept { return shown_; }

    private:
        juce::RangedAudioParameter* param_ = nullptr;
        double shown_ = 0.0;
    };

    class ShownSwitch : public juce::ToggleButton {
    public:
        std::function<void()> onEscape;
        bool keyPressed(const juce::KeyPress& key) override;
    };

    using ColumnRects = std::array<juce::Rectangle<int>, 6>; // caption and knob of Level, Tune, Decay

    void bind(int index);
    void refreshSoundRow(const synth::drums::Pad& pad);
    void layoutColumns(juce::Rectangle<int> area);
    ColumnRects columnRects(juce::Rectangle<int> area) const;
    void placeColumns(const ColumnRects& finals);
    void refreshShownSwitch();
    void knobFollowedModel(size_t f, double before, double after);
    void applyDrawnValues();
    void repaintCardAroundKnobs();
    void startRingFade(int oldPad);

    DrumKitModule& kit_;
    std::array<PadKnob, 4> knobs_;
    std::array<juce::Label, 4> captions_;
    std::array<std::unique_ptr<juce::SliderParameterAttachment>, 4> attachments_;
    juce::Label name_;
    juce::Label note_;
    juce::Label soundLabel_{"Sound label", "Sound"};
    SamplerInstrumentPicker sound_;
    TrackShowModuleButton open_;
    juce::Label pitchCaption_{"Plays pitch label", "Plays pitch"};
    CardStepper pitch_{"Plays pitch"};
    ShownSwitch shown_;
    juce::Colour dot_;
    int bound_ = -1;
    bool glideBinding_ = false; // a pick is re-binding the knobs: their new values glide
    bool bindingKnobs_ = false; // the knobs are being re-attached: their own echoes are not the user
    bool armColumns_ = false;   // the next layout glides the knob columns
    bool startOn_ = false;      // the bound pad shows Start / Plays pitch / Open (the fades lag isVisible)
    bool pitchOn_ = false;
    bool openOn_ = false;
    FadeVisibility fadeStart_{&knobs_[(size_t)DrumKitModule::PadParam::Start],
                              &captions_[(size_t)DrumKitModule::PadParam::Start]};
    FadeVisibility fadePitch_{&pitch_, &pitchCaption_};
    FadeVisibility fadeOpen_{&open_};
    KeyedPointTween valueGlide_{*this};
    std::array<double, 4> resting_{};
    std::array<bool, 4> released_{};
    juce::VBlankAnimatorUpdater updater_{this};
    AnimationDriver columnDriver_;
    ColumnRects columnFrom_{}, columnTo_{};
    AnimationDriver ringDriver_;
    float ringProgress_ = 1.0f;
    std::array<juce::String, 4> leavingIds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumPadControls)
};

} // namespace synth::ui
