// DefaultCardLayoutsEnvelopes.cpp -- the code-default card layouts of ADSR (and its "Amp Env" and "Filter Env"
// factory keys, each its own entry), VCA, Envelope Follower, Sample & Hold, Math, Voice Mixer, Poly MIDI and
// MIDI Keyboard. Each type registers with defaults.add(type, layout, revision, dimRules), built with the
// cardlayout:: helpers (DefaultCardLayoutsFamilies.h); a type left out draws the automatic layout.
// The MIDI Keyboard card builds no CardBody (cardBodyBuildsWidgetsFor is false for it), so its Octave
// stepper is card code (ModuleComponentMidiKeyboardCard.cpp), not an entry here.
// docs/layout/module-card-layout.md#default-layouts.
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"

namespace synth {

namespace {

using namespace cardlayout;

constexpr int kRevision = 2; // the lane grid (earlier: a Free/Sync switch, rows of faders, footers)

CardLayout layoutOf(std::vector<CardSection> sections) {
    CardLayout layout;
    layout.sections = std::move(sections);
    return layout;
}

// One ADSR stage in tempo-sync's two readings: the time as a fader while Sync is off, its note
// division, a stepped fader with its S | T | D picker, in the same cell while it is on. The caption is the same either
// way, so a stage keeps its name. The four pairs all test tempoSync with the same values; each pair is its own swap
// group (a member whose condition repeats one already in the group starts the next group).
std::vector<CardItem> stage(const juce::String& time, const juce::String& division, const juce::String& caption) {
    return {showWhen(labelled(param(time, CardWidget::FaderV), caption), "tempoSync", {"false"}),
            showWhen(labelled(param(division, CardWidget::FaderV), caption), "tempoSync", {"true"})};
}

// The Envelope and Threshold views on top, the Poly pill (and the card's Show Envelope Graph and Show Scope pills) in
// the mode row, then two lanes: Time, the five stages A H D S R as faders with the Sync button at the end of Attack's
// caption (each time swaps for its division when Sync is lit), and Voice, Velocity. There is no Out lane: an envelope
// has no level of its own. Legato waits in the More row: a pill beside Poly would crowd the row.
CardLayout adsrLayout() {
    std::vector<CardItem> stages{param("tempoSync", CardWidget::Sync)};
    for (const auto& pair :
         {stage("attack", "attackDiv", "Atk"), stage("hold", "holdDiv", "Hold"), stage("decay", "decayDiv", "Dec")})
        stages.insert(stages.end(), pair.begin(), pair.end());
    stages.emplace_back(labelled(param("sustain", CardWidget::FaderV), "Sus"));
    for (const auto& item : stage("release", "releaseDiv", "Rel"))
        stages.push_back(item);
    return layoutOf({section("envelope", std::nullopt, {view(CardView::Envelope), view(CardView::Threshold)}),
                     modeRow({param("poly")}), lane("time", "Time", std::move(stages)),
                     lane("voice", "Voice", {param("velocity")})});
}

// The clock decides what the hero knob holds: the internal clock's Rate, or the external clock's trigger Threshold,
// which also opens the trigger meter above the lanes. The cell is a swap group (a swapped-out knob keeps its CV jack,
// so the gutter never changes); the meter is a picture section's visibleWhen, because a view cannot be a group member.
// The Source, Mode and Clock choices are the mode row. The card changes height with the Clock switch, a deliberate mode
// choice, never while a value is dragged.
CardLayout sampleHoldLayout() {
    CardSection meter = section("trigger-meter", std::nullopt, {view(CardView::Threshold)});
    meter.visibleWhen = CardCondition{"clock", {"External"}, CardConditionEffect::Show};
    return layoutOf({std::move(meter),
                     modeRow({param("source", CardWidget::Segmented), param("holdMode"), param("clock")}),
                     lane("hero", "Hero",
                          {showWhen(param("rate", CardWidget::KnobLarge), "clock", {"Internal"}),
                           showWhen(param("trigThreshold", CardWidget::KnobLarge), "clock", {"External"})}),
                     lane("shape", "Shape", {param("slew"), param("offset")}), outLane({param("level")})});
}

} // namespace

void registerEnvelopeCardLayouts(DefaultCardLayouts& defaults) {
    for (const char* type : {"ADSR", "Amp Env", "Filter Env"})
        defaults.add(type, adsrLayout(), kRevision);
    // A VCA, a Voice Mixer and a Math card have one control group, so each is one lane (a VCA and a Voice Mixer: only
    // Out).
    defaults.add("VCA", layoutOf({modeRow({param("poly")}), outLane({param("gain")})}), kRevision);
    defaults.add("Envelope Follower",
                 layoutOf({modeRow({param("detection", CardWidget::Segmented)}),
                           lane("hero", "Hero", {param("sensitivity", CardWidget::KnobLarge)}),
                           lane("time", "Time", {param("attack"), param("release")})}),
                 kRevision);
    defaults.add("Sample & Hold", sampleHoldLayout(), kRevision);
    defaults.add("Math", layoutOf({lane("shape", "Shape", {param("clip", CardWidget::Segmented)})}), kRevision);
    defaults.add("Voice Mixer", layoutOf({outLane({param("level")})}), kRevision);
    // Voice Steal stays a combo: "Round-Robin" is longer than a segmented switch's 10 characters.
    defaults.add(
        "Poly MIDI",
        layoutOf({lane("voice", "Voice", {param("voiceSteal"), labelled(param("velToGate"), "Velocity sets gate")})}),
        kRevision);
}

} // namespace synth
