// DefaultCardLayoutsMacros.cpp -- the code-default card layout of the Macros bank and its three built-in presets
// (Row, Grid, Faders). The sixteen knobs are always listed, in macro order: the ones above the Knobs count are
// hidden by the module's own rule (CardBodyPlan.cpp, macroCountRules) and take no room, so a layout keeps its place
// for them and growing the count brings them back in order. The output jacks are not part of any layout: they stay in
// the card's right-edge gutter, one per macro.
// docs/layout/module-card-layout.md#default-layouts, docs/macros/macros.md.
#include "Modules/MacroControlModule.h"
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"

namespace synth {

namespace {

using namespace cardlayout;

// The Knobs count heads every layout, in a section of its own, so it never moves with the macros.
CardSection countSection() { return section("bank", std::nullopt, {param("macroCount")}, 4); }

// Macro 1..16 in order, each drawn as `widget`, flowing `columns` to a row.
CardSection macrosSection(CardWidget widget, int columns) {
    std::vector<CardItem> items;
    for (int i = 0; i < MacroControlModule::kMaxMacros; ++i)
        items.push_back(param("macro" + juce::String(i + 1), widget));
    return section("macros", std::nullopt, std::move(items), columns);
}

CardLayout macrosLayout(CardWidget widget, int columns) {
    CardLayout layout;
    layout.sections = {countSection(), macrosSection(widget, columns)};
    return layout;
}

} // namespace

// Grid is the default: two rows of four for the standard eight, which reads as a small controller; a bank of 16
// is four rows. Row runs eight across (a second row only past eight), Faders is the mixer-style strip.
void registerMacroCardLayouts(DefaultCardLayouts& defaults) {
    defaults.add("Macros", macrosLayout(CardWidget::Knob, 4), 1);
    defaults.addPreset("Macros", "Row", macrosLayout(CardWidget::Knob, 8));
    defaults.addPreset("Macros", "Grid", macrosLayout(CardWidget::Knob, 4));
    defaults.addPreset("Macros", "Faders", macrosLayout(CardWidget::FaderV, 8));
}

} // namespace synth
