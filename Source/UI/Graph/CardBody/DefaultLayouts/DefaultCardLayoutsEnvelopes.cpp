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

constexpr int kRevision = 1;

CardLayout layoutOf(std::vector<CardSection> sections) {
    CardLayout layout;
    layout.sections = std::move(sections);
    return layout;
}

CardParamItem labelled(CardParamItem item, const juce::String& caption) {
    item.label = caption;
    return item;
}

// One ADSR stage in tempo-sync's two readings: the time as a fader while tempoSync is off, its note
// division in the same cell while it is on. The caption is the same either way, so a stage keeps its name.
// The four pairs all test tempoSync with the same values; each pair is its own swap group (a member whose
// condition repeats one already in the group starts the next group).
std::vector<CardItem> stage(const juce::String& time, const juce::String& division, const juce::String& caption) {
    return {showWhen(labelled(param(time, CardWidget::FaderV), caption), "tempoSync", {"false"}),
            showWhen(labelled(param(division), caption), "tempoSync", {"true"})};
}

// Envelope view open; Time/Tempo switch; A H D S R as faders in one row (each time swaps for its division in
// Tempo mode); Velocity and the threshold view; footer (Poly joins it by itself, with Show Envelope Graph
// and Show Scope).
CardLayout adsrLayout() {
    std::vector<CardItem> stages;
    for (const auto& pair :
         {stage("attack", "attackDiv", "Atk"), stage("hold", "holdDiv", "Hold"), stage("decay", "decayDiv", "Dec")})
        stages.insert(stages.end(), pair.begin(), pair.end());
    stages.emplace_back(labelled(param("sustain", CardWidget::FaderV), "Sus"));
    for (const auto& item : stage("release", "releaseDiv", "Rel"))
        stages.push_back(item);
    return layoutOf(
        {section("envelope", std::nullopt, {view(CardView::Envelope)}),
         section("time", std::nullopt, {labelled(param("tempoSync", CardWidget::Segmented), "Stage times")}),
         section("stages", std::nullopt, std::move(stages), 5),
         section("trigger", std::nullopt, {param("velocity", CardWidget::FaderH), view(CardView::Threshold)}),
         footer({})});
}

// The clock decides what the first knob cell holds: the internal clock's Rate, or the external clock's
// trigger Threshold, which also opens the trigger meter above the knobs. The knob cell is a swap group (a
// swapped-out knob keeps its CV jack, so the gutter never changes); the meter is a section's visibleWhen,
// because a view cannot be a group member. The card changes height with the Clock switch, a deliberate mode
// choice, never while a value is dragged.
CardLayout sampleHoldLayout() {
    CardSection meter = section("trigger-meter", std::nullopt, {view(CardView::Threshold)});
    meter.visibleWhen = CardCondition{"clock", {"External"}, CardConditionEffect::Show};
    return layoutOf({section("source", std::nullopt,
                             {param("source", CardWidget::Segmented), param("holdMode", CardWidget::Segmented),
                              param("clock", CardWidget::Segmented)}),
                     std::move(meter),
                     section("output", std::nullopt,
                             {showWhen(param("rate"), "clock", {"Internal"}),
                              showWhen(param("trigThreshold"), "clock", {"External"}), param("slew"), param("level"),
                              param("offset")},
                             2)});
}

} // namespace

void registerEnvelopeCardLayouts(DefaultCardLayouts& defaults) {
    for (const char* type : {"ADSR", "Amp Env", "Filter Env"})
        defaults.add(type, adsrLayout(), kRevision);
    defaults.add("VCA", layoutOf({section("main", std::nullopt, {param("gain", CardWidget::FaderH)}), footer({})}),
                 kRevision);
    defaults.add("Envelope Follower",
                 layoutOf({section("main", std::nullopt,
                                   {param("detection", CardWidget::Segmented), param("attack"), param("release"),
                                    param("sensitivity")}),
                           footer({})}),
                 kRevision);
    defaults.add("Sample & Hold", sampleHoldLayout(), kRevision);
    defaults.add("Math", layoutOf({section("main", std::nullopt, {param("clip", CardWidget::Segmented)}), footer({})}),
                 kRevision);
    defaults.add("Voice Mixer", layoutOf({section("main", std::nullopt, {param("level", CardWidget::FaderH)})}),
                 kRevision);
    // Voice Steal stays a combo: "Round-Robin" is longer than a segmented switch's 10 characters.
    defaults.add("Poly MIDI",
                 layoutOf({section("main", std::nullopt, {param("voiceSteal", CardWidget::Segmented)}),
                           footer({labelled(param("velToGate"), "Velocity sets gate")})}),
                 kRevision);
}

} // namespace synth
