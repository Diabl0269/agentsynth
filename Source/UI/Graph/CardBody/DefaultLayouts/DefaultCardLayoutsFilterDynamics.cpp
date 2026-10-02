// DefaultCardLayoutsFilterDynamics.cpp -- the code-default card layouts of Filter, Compressor, Limiter and Gate.
// Each type registers with defaults.add(type, layout, revision, dimRules), built with the cardlayout::
// helpers (DefaultCardLayoutsFamilies.h); a type left out draws the automatic layout.
// docs/layout/module-card-layout.md#default-layouts.
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"

namespace synth {

namespace {

using namespace cardlayout;

// The Filter's Show Response and Show Spectrum panels are card chrome (ModuleComponentCardView.cpp), and
// with a footer they are pills in it; Poly joins it on its own. Key Track never dims: the card knows no
// cable, so it cannot tell an unplugged Pitch input.
CardLayout filterLayout() {
    CardLayout layout;
    layout.sections = {
        section("type", std::nullopt, {param("filterType")}),
        section("tone", std::nullopt, {param("cutoff", CardWidget::KnobLarge), param("resonance"), param("drive")}),
        section("modulation", "Modulation", {param("keyTrack"), param("outputLevel")}),
        footer({}),
    };
    return layout;
}

// The meter first, so the reduction reads at a glance above the controls that cause it. Threshold and
// Makeup are two faders side by side, their levels compared at a glance; the three time and ratio knobs
// share one row. "Makeup Gain (dB)" is relabelled: at a knob's width it would ellipsise.
CardLayout compressorLayout() {
    CardLayout layout;
    layout.sections = {
        section("meter", std::nullopt, {view(CardView::GainReduction)}),
        section(
            "levels", std::nullopt,
            {param("threshold", CardWidget::FaderV), labelled(param("makeupGain", CardWidget::FaderV), "Makeup (dB)")},
            2),
        section("dynamics", std::nullopt, {param("ratio"), param("attack"), param("release")}),
        footer({param("knee")}),
    };
    return layout;
}

// Top to bottom is the signal path: the reduction the limiter makes, what goes in and the ceiling it stops
// at (two faders side by side), then Threshold and Release. Threshold stays on the card, beside Release: its
// CV jack needs a knob to land on, which a folded More row does not give it.
CardLayout limiterLayout() {
    CardLayout layout;
    layout.sections = {
        section("meter", std::nullopt, {view(CardView::GainReduction)}),
        section("levels", std::nullopt, {param("inputGain", CardWidget::FaderV), param("ceiling", CardWidget::FaderV)},
                2),
        section("timing", std::nullopt, {param("threshold"), param("release")}),
    };
    return layout;
}

// The Threshold view needs live level telemetry the Gate does not publish yet, so Threshold is a knob.
CardLayout gateLayout() {
    CardLayout layout;
    layout.sections = {
        section("main", std::nullopt, {param("threshold"), param("attack"), param("hold"), param("release")}),
        footer({param("range"), param("outputLevel")}),
    };
    return layout;
}

} // namespace

void registerFilterDynamicsCardLayouts(DefaultCardLayouts& defaults) {
    defaults.add("Filter", filterLayout(), 1);
    defaults.add("Compressor", compressorLayout(), 1);
    defaults.add("Limiter", limiterLayout(), 1);
    defaults.add("Gate", gateLayout(), 1);
}

} // namespace synth
