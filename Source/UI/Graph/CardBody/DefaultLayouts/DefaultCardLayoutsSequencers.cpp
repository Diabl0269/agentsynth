// DefaultCardLayoutsSequencers.cpp -- the code-default card layout of the Sequencer and the Poly Sequencer: four
// `row` sections (Play, Time, Notes, Pattern) side by side, then the step grid view. The sections flow in one line
// and wrap only when the 560 px card is too narrow, so the whole-pattern controls take the fewest rows possible.
// docs/layout/module-card-layout.md#default-layouts, docs/modules/sequencer-design.md#card.
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"

namespace synth {

namespace {

using namespace cardlayout;

constexpr int kRevision = 2;

CardLayout sequencerLayout() {
    CardLayout layout;
    layout.sections = {
        rowSection("play", "Play",
                   {labelled(param("run", CardWidget::Toggle), "Run"),
                    labelled(param("syncToTransport", CardWidget::Toggle), "Sync"),
                    showWhen(param("bpm", CardWidget::FaderH), "syncToTransport", {"false"})}),
        rowSection("time", "Time", {param("rate"), param("steps", CardWidget::Stepper)}),
        rowSection("notes", "Notes", {param("scaleRoot")}),
        rowSection("pattern", "Pattern",
                   {param("transpose", CardWidget::Knob), param("gate", CardWidget::Knob),
                    param("velocity", CardWidget::Knob), param("chance", CardWidget::Knob)}),
        section("grid", std::nullopt, {view(CardView::StepGrid)}),
    };
    return layout;
}

} // namespace

void registerSequencerCardLayouts(DefaultCardLayouts& defaults) {
    for (const char* type : {"Sequencer", "Poly Sequencer"})
        defaults.add(type, sequencerLayout(), kRevision);
}

} // namespace synth
