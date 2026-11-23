// DefaultCardLayoutsSequencers.cpp -- the code-default card layout of the Sequencer and the Poly Sequencer: the step
// grid on top, a mode row with Run, then the lanes Pitch (Key, Transpose), Shape (Gate, Velocity, Chance) and Time
// (Follow transport, BPM, Rate, Steps). BPM dims while Follow transport is on, so the card never resizes. There is no
// Out lane: a sequencer has no level.
// docs/layout/module-card-layout.md#default-layouts, docs/modules/sequencer-design.md#card.
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"

namespace synth {

namespace {

using namespace cardlayout;

constexpr int kRevision = 3; // the lane grid (earlier: four Row sections)

CardLayout sequencerLayout() {
    CardLayout layout;
    layout.sections = {
        section("grid", std::nullopt, {view(CardView::StepGrid)}),
        modeRow({labelled(param("run", CardWidget::Toggle), "Run")}),
        lane("pitch", "Pitch", {param("scaleRoot"), param("transpose", CardWidget::Knob)}),
        lane("shape", "Shape",
             {param("gate", CardWidget::Knob), param("velocity", CardWidget::Knob), param("chance", CardWidget::Knob)}),
        lane("time", "Time",
             {labelled(param("syncToTransport", CardWidget::Toggle), "Follow transport"),
              dimUnless(param("bpm", CardWidget::Knob), "syncToTransport", {"false"}), param("rate"),
              param("steps", CardWidget::Stepper)}),
    };
    return layout;
}

} // namespace

void registerSequencerCardLayouts(DefaultCardLayouts& defaults) {
    for (const char* type : {"Sequencer", "Poly Sequencer"})
        defaults.add(type, sequencerLayout(), kRevision);
}

} // namespace synth
