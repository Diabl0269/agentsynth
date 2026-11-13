// DefaultCardLayoutsMacros.cpp -- the code-default card layout of the Macros bank and its four built-in presets
// (Row, Grid, Faders, XY). The sixteen knobs are always listed, in macro order: the ones above the Knobs count are
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

// Macro 1 as the X axis and macro 2 as the Y axis of an XY pad, under the Knobs count; the other macros stay knobs
// below it. Macro 2 is listed hidden: the pad stands in for its knob, and its jack stays in the gutter.
CardLayout xyLayout() {
    CardParamItem pad;
    pad.paramId = "macro1";
    pad.widget = CardWidget::XYPad;
    pad.yParamId = "macro2";
    CardLayout layout;
    layout.sections = {countSection(), section("pad", std::nullopt, {CardItem(pad)}, 1)};
    std::vector<CardItem> knobs;
    for (int i = 2; i < MacroControlModule::kMaxMacros; ++i)
        knobs.push_back(param("macro" + juce::String(i + 1), CardWidget::Knob));
    layout.sections.push_back(section("macros", std::nullopt, std::move(knobs), 4));
    layout.hidden.add("macro2");
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
    defaults.addPreset("Macros", "XY", xyLayout());
}

} // namespace synth
