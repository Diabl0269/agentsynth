// DefaultCardLayoutsEffects.cpp -- the code-default card layouts of Delay, Reverb, Chorus, Phaser, Flanger, Distortion,
// Bitcrusher, Ring Modulator and Pitch Shifter. Each type registers with defaults.add(type, layout, revision,
// dimRules), built with the cardlayout:: helpers (DefaultCardLayoutsFamilies.h); a type left out draws the automatic
// layout. docs/layout/module-card-layout.md#default-layouts.
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"

namespace synth {

namespace {

using namespace cardlayout;

// Revision 3 is the lane grid (docs/layout/module-card-layout.md#lane-presentation): a mode row where the module has a
// mode, then lanes by role (the main control first, titled by what it does: Time, Size, Rate; then pitch, shape, time,
// motion, voice) and last the Out lane with Mix, Pan and Output.
constexpr int kRevision = 4;           // 4: the first lane is titled by its control (Rate, Drive, Pitch), not "Hero"
constexpr int kBitcrusherRevision = 3; // no lane titled by role, so it stays at the lane grid
constexpr int kReverbRevision = 5;     // 5: the first lane is titled Size
constexpr int kDelayRevision = 5;      // the Sync button at the end of Time's caption; 5: the first lane is titled Time

CardLayout layoutOf(std::vector<CardSection> sections) {
    CardLayout layout;
    layout.sections = std::move(sections);
    return layout;
}

// The Out lane: the output stage as small knobs, Mix, Pan, Output, bottom up.
CardSection effectOutLane() { return outLane({param("mix"), param("pan"), labelled(param("outputLevel"), "Output")}); }

// The Time knob and its Sync button: Sync sits at the end of Time's caption (lit = the note division swaps into the
// same cell, the Time knob swaps back when it is off).
CardLayout delayLayout() {
    return layoutOf({lane("hero", "Time",
                          {param("tempoSync", CardWidget::Sync),
                           showWhen(param("time", CardWidget::KnobLarge), "tempoSync", {"false"}),
                           showWhen(labelled(param("timeDiv"), "Time"), "tempoSync", {"true"})}),
                     lane("motion", "Motion", {param("feedback"), param("pingPong")}), effectOutLane()});
}

// One Mix knob like every other blending effect, now in the Out lane.
CardLayout reverbLayout() {
    return layoutOf(
        {lane("hero", "Size", {param("roomSize", CardWidget::KnobLarge)}),
         lane("shape-time", juce::String::fromUTF8("Shape \xc2\xb7 Time"), {param("damping"), param("preDelay")}),
         lane("voice", "Voice", {param("width")}), effectOutLane()});
}

// Chorus and Flanger share the shape; the delay-based ones relabel "Centre Delay (ms)" as "Delay": at a knob's width
// the full name would ellipsise.
CardLayout delayModulationLayout() {
    return layoutOf({lane("hero", "Rate", {param("rate", CardWidget::KnobLarge)}),
                     lane("time-motion", juce::String::fromUTF8("Time \xc2\xb7 Motion"),
                          {labelled(param("centreDelay"), "Delay"), param("depth"), param("feedback")}),
                     effectOutLane()});
}

CardLayout phaserLayout() {
    return layoutOf({lane("hero", "Rate", {param("rate", CardWidget::KnobLarge)}),
                     lane("shape-motion", juce::String::fromUTF8("Shape \xc2\xb7 Motion"),
                          {labelled(param("centreFreq"), "Freq"), param("depth"), param("feedback")}),
                     effectOutLane()});
}

CardLayout distortionLayout() {
    return layoutOf({modeRow({param("type", CardWidget::Segmented), param("oversampling")}),
                     lane("hero", "Drive", {param("drive", CardWidget::KnobLarge)}), effectOutLane()});
}

CardLayout bitcrusherLayout() {
    return layoutOf(
        {lane("shape", "Shape", {param("depth"), labelled(param("rate"), "Rate"), param("dither")}), effectOutLane()});
}

CardLayout ringModulatorLayout() {
    return layoutOf({modeRow({param("oversampling")}), lane("hero", "Drive", {param("drive", CardWidget::KnobLarge)}),
                     lane("shape", "Shape", {param("character")}), effectOutLane()});
}

// Frequency mode swaps Pitch and Fine for Shift (Hz). A swap group holds one cell, so Pitch and Shift swap in
// one cell and Fine dims in Frequency mode: two conditional sections would hide knobs whose CV jacks must
// stay knob-bound, and would resize the card on every mode flip.
CardLayout pitchShifterLayout() {
    return layoutOf({modeRow({param("shiftMode", CardWidget::Segmented)}),
                     lane("hero", "Pitch",
                          {showWhen(param("pitch", CardWidget::KnobLarge), "shiftMode", {"Pitch"}),
                           showWhen(param("shiftHz", CardWidget::KnobLarge), "shiftMode", {"Frequency"})}),
                     lane("pitch-motion", juce::String::fromUTF8("Pitch \xc2\xb7 Motion"),
                          {dimUnless(param("fine"), "shiftMode", {"Pitch"}), param("window"), param("feedback")}),
                     effectOutLane()});
}

} // namespace

void registerEffectCardLayouts(DefaultCardLayouts& defaults) {
    defaults.add("Delay", delayLayout(), kDelayRevision);
    defaults.add("Reverb", reverbLayout(), kReverbRevision);
    defaults.add("Chorus", delayModulationLayout(), kRevision);
    defaults.add("Flanger", delayModulationLayout(), kRevision);
    defaults.add("Phaser", phaserLayout(), kRevision);
    defaults.add("Distortion", distortionLayout(), kRevision);
    defaults.add("Bitcrusher", bitcrusherLayout(), kBitcrusherRevision);
    defaults.add("Ring Modulator", ringModulatorLayout(), kRevision);
    defaults.add("Pitch Shifter", pitchShifterLayout(), kRevision);
}

} // namespace synth
