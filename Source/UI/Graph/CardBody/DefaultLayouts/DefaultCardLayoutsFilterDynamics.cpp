// DefaultCardLayoutsFilterDynamics.cpp -- the code-default card layouts of Filter, Compressor, Limiter and Gate.
// Each type registers with defaults.add(type, layout, revision, dimRules), built with the cardlayout::
// helpers (DefaultCardLayoutsFamilies.h); a type left out draws the automatic layout. All four are lane grids
// (docs/layout/module-card-layout.md#lane-presentation): lanes by role, Mix, Pan and the output control in the Out
// lane, and the picture on top (the Filter's response view is card chrome, the dynamics cards' gain-reduction meter is
// a view). docs/layout/module-card-layout.md#default-layouts.
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"

namespace synth {

namespace {

using namespace cardlayout;

// A lane a type shows only in its Custom mode: Morph X and Morph Y, which the card hides while its morph map stands in
// for them (CardBody::setSectionHidden("custom")). Its id is the section's name, so that call finds it.
CardSection morphLane() {
    auto morph = lane("custom", "Morph", {param("morphX"), param("morphY")});
    morph.visibleWhen = CardCondition{"filterType", {"Custom"}, CardConditionEffect::Show};
    return morph;
}

// The Filter. The response picture above the mode row is card chrome (ModuleComponentFilterPicture.cpp) with its Show
// Response and Show Spectrum icons; the mode row holds the Type menu on the left and Poly (then Show Scope, and in
// Custom the morph pills) at its right end. Key Track never dims: the card knows no cable, so it cannot tell an
// unplugged Pitch input. The Custom type uses Cutoff and Resonance too, so the lanes are the same in every type;
// Shift, Depth and Resonance Mode join the Shape lane (a second column) only in Custom.
CardLayout filterLayout() {
    CardLayout layout;
    layout.sections = {
        modeRow({param("filterType"), param("poly")}),
        lane("hero", "Hero", {param("cutoff", CardWidget::KnobLarge)}),
        lane("shape", "Shape",
             {param("resonance"), param("drive"), showWhen(param("shift"), "filterType", {"Custom"}),
              showWhen(param("depth"), "filterType", {"Custom"}),
              showWhen(param("resonanceMode"), "filterType", {"Custom"})}),
        lane("motion", "Motion", {param("keyTrack")}),
        morphLane(),
        outLane({param("mix"), param("pan"), labelled(param("outputLevel"), "Output")}),
    };
    return layout;
}

// The meter first, so the reduction reads at a glance above the controls that cause it, then the lanes. Threshold is a
// tall fader that spans both rows of the Hero lane; "Makeup Gain (dB)" is relabelled Makeup, the output control's name
// on a compressor. No mode row: the card has no mode switch and no Show Scope, so nothing would stand in it.
CardLayout compressorLayout() {
    CardLayout layout;
    layout.sections = {
        section("meter", std::nullopt, {view(CardView::GainReduction)}),
        lane("hero", "Hero", {param("threshold", CardWidget::FaderVLong)}),
        lane("shape", "Shape", {param("ratio"), param("knee")}),
        lane("time", "Time", {param("attack"), param("release")}),
        outLane({param("mix"), param("pan"), labelled(param("makeupGain"), "Makeup")}),
    };
    return layout;
}

// The meter, then the Threshold fader as the hero, Input Gain, Release, and the Out lane: Pan and Ceiling (the
// limiter's output control; it has no Mix, the lookahead latency would misalign the dry signal).
CardLayout limiterLayout() {
    CardLayout layout;
    layout.sections = {
        section("meter", std::nullopt, {view(CardView::GainReduction)}),
        lane("hero", "Hero", {param("threshold", CardWidget::FaderVLong)}),
        lane("shape", "Shape", {param("inputGain")}),
        lane("time", "Time", {param("release")}),
        outLane({param("pan"), param("ceiling")}),
    };
    return layout;
}

// The Threshold view needs live level telemetry the Gate does not publish yet, so Threshold is a knob (the hero).
CardLayout gateLayout() {
    CardLayout layout;
    layout.sections = {
        lane("hero", "Hero", {param("threshold", CardWidget::KnobLarge)}),
        lane("shape", "Shape", {param("range")}),
        lane("time", "Time", {param("attack"), param("hold"), param("release")}),
        outLane({param("mix"), param("pan"), labelled(param("outputLevel"), "Output")}),
    };
    return layout;
}

} // namespace

void registerFilterDynamicsCardLayouts(DefaultCardLayouts& defaults) {
    defaults.add("Filter", filterLayout(), 7);
    defaults.add("Compressor", compressorLayout(), 2);
    defaults.add("Limiter", limiterLayout(), 2);
    defaults.add("Gate", gateLayout(), 2);
}

} // namespace synth
