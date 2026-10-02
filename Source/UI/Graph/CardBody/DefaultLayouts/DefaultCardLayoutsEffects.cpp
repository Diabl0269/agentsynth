// DefaultCardLayoutsEffects.cpp -- the code-default card layouts of Delay, Reverb, Chorus, Phaser, Flanger, Distortion,
// Bitcrusher, Ring Modulator and Pitch Shifter. Each type registers with defaults.add(type, layout, revision,
// dimRules), built with the cardlayout:: helpers (DefaultCardLayoutsFamilies.h); a type left out draws the automatic
// layout. docs/layout/module-card-layout.md#default-layouts.
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"

namespace synth {

namespace {

using namespace cardlayout;

constexpr int kRevision = 1;

CardLayout layoutOf(std::vector<CardSection> sections) {
    CardLayout layout;
    layout.sections = std::move(sections);
    return layout;
}

// The Delay's Time/Sync switch (the boolean's own off and on texts, so a segmented control) swaps the Time
// knob for its note division in one cell.
CardLayout delayLayout() {
    return layoutOf({section("main", std::nullopt,
                             {param("tempoSync", CardWidget::Segmented),
                              showWhen(param("time", CardWidget::KnobLarge), "tempoSync", {"false"}),
                              showWhen(param("timeDiv"), "tempoSync", {"true"}), param("feedback"), param("mix")}),
                     footer({param("pingPong"), param("outputLevel")})});
}

CardLayout reverbLayout() {
    return layoutOf({section("room", juce::String("Room"), {param("roomSize"), param("damping"), param("preDelay")}),
                     section("mix", juce::String("Mix"),
                             {param("dry", CardWidget::FaderV), param("wet", CardWidget::FaderV), param("width")}),
                     footer({param("outputLevel")})});
}

// Chorus, Phaser and Flanger share the shape; `centre` is the tone control that differs. The delay-based
// ones relabel "Centre Delay (ms)" as "Delay (ms)": at a knob's width the full name would ellipsise.
CardLayout modulationLayout(CardParamItem centre) {
    return layoutOf({section("motion", juce::String("Motion"), {param("rate"), param("depth"), param("mix")}),
                     section("tone", juce::String("Tone"), {std::move(centre), param("feedback")}),
                     footer({param("outputLevel")})});
}

CardLayout distortionLayout() {
    return layoutOf({section("type", std::nullopt, {param("type", CardWidget::Segmented)}),
                     section("main", std::nullopt, {param("drive", CardWidget::KnobLarge), param("mix")}),
                     footer({param("oversampling"), param("outputLevel")})});
}

CardLayout bitcrusherLayout() {
    return layoutOf({section("main", std::nullopt, {param("depth"), param("rate"), param("mix")}),
                     footer({param("dither"), param("outputLevel")})});
}

CardLayout ringModulatorLayout() {
    return layoutOf({section("main", std::nullopt, {param("drive"), param("character"), param("mix")}),
                     footer({param("oversampling"), param("outputLevel")})});
}

// Frequency mode swaps Pitch and Fine for Shift (Hz). A swap group holds one cell, so Pitch and Shift swap in
// one cell and Fine dims in Frequency mode: two conditional sections would hide knobs whose CV jacks must
// stay knob-bound, and would resize the card on every mode flip.
CardLayout pitchShifterLayout() {
    return layoutOf({section("mode", std::nullopt, {param("shiftMode", CardWidget::Segmented)}),
                     section("main", std::nullopt,
                             {showWhen(param("pitch", CardWidget::KnobLarge), "shiftMode", {"Pitch"}),
                              showWhen(param("shiftHz", CardWidget::KnobLarge), "shiftMode", {"Frequency"}),
                              dimUnless(param("fine"), "shiftMode", {"Pitch"}), param("mix")}),
                     section("blend", std::nullopt, {param("window"), param("feedback")}),
                     footer({param("outputLevel")})});
}

} // namespace

void registerEffectCardLayouts(DefaultCardLayouts& defaults) {
    defaults.add("Delay", delayLayout(), kRevision);
    defaults.add("Reverb", reverbLayout(), kRevision);
    defaults.add("Chorus", modulationLayout(labelled(param("centreDelay"), "Delay (ms)")), kRevision);
    defaults.add("Flanger", modulationLayout(labelled(param("centreDelay"), "Delay (ms)")), kRevision);
    defaults.add("Phaser", modulationLayout(param("centreFreq")), kRevision);
    defaults.add("Distortion", distortionLayout(), kRevision);
    defaults.add("Bitcrusher", bitcrusherLayout(), kRevision);
    defaults.add("Ring Modulator", ringModulatorLayout(), kRevision);
    defaults.add("Pitch Shifter", pitchShifterLayout(), kRevision);
}

} // namespace synth
