// CardBodyTabs.cpp -- a card body's tab strips: one per run of `presentation: tab` sections, drawn as the
// joined-segment switch (one Tab stop, Left/Right/Home/End to switch, the accent focus ring, a group of
// titled radio buttons to a screen reader). Selecting a tab shows that section's widgets, hides the
// other tabs' and re-lays the card out; the group is as tall as its tallest tab, so the card never
// resizes. The selected tab lives on the card only: a rebuilt card opens on its first tab.
// docs/layout/module-card-layout.md#rendering.
#include "CardBody.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

namespace synth {

namespace {

constexpr const char* kTabStripTitle = "Control tabs";
constexpr const char* kTabStripTooltip = "Pages of this card's controls: Left and Right switch between them";

} // namespace

// Created after every parameter widget and section header, so the controls' child and
// screen-reader order stays the declaration order; Tab order follows position, so the strip is
// reached between the controls above it and its tab's controls.
void CardBody::createTabStrips() {
    for (int g = 0; g < (int)plan_.tabGroups.size(); ++g) {
        auto& group = plan_.tabGroups[(size_t)g];
        juce::StringArray titles;
        for (int section : group.sections)
            titles.add(plan_.tabTitle(section));
        auto* strip = new synth::ui::CardSegmentedSwitch(kTabStripTitle, titles);
        widgets_.add(strip);
        strip->setComponentID("cardTabStrip");
        strip->setTooltip(kTabStripTooltip);
        strip->setSelectedIndex(group.selected, juce::dontSendNotification);
        strip->onChange = [this, g](int tab) { selectTab(g, tab); };
        card_.addAndMakeVisible(strip);
        group.strip = strip;
    }
}

juce::Component* CardBody::getTabStrip(int group) const {
    return group >= 0 && group < (int)plan_.tabGroups.size() ? plan_.tabGroups[(size_t)group].strip : nullptr;
}

int CardBody::getSelectedTab(int group) const {
    return group >= 0 && group < (int)plan_.tabGroups.size() ? plan_.tabGroups[(size_t)group].selected : -1;
}

// A tab switch changes which knobs are on the card, so the cables re-anchor (the graph's content
// changed) as well as the card re-laying itself out.
void CardBody::selectTab(int group, int tab) {
    if (group < 0 || group >= (int)plan_.tabGroups.size())
        return;
    auto& tabs = plan_.tabGroups[(size_t)group];
    if (tab < 0 || tab >= (int)tabs.sections.size() || tab == tabs.selected)
        return;
    tabs.selected = tab;
    if (auto* strip = dynamic_cast<synth::ui::CardSegmentedSwitch*>(tabs.strip))
        strip->setSelectedIndex(tab, juce::dontSendNotification);
    applyVisibility();
    card_.resized();
    card_.repaint();
    card_.owner.notifyModuleContentChanged();
}

bool CardBody::isTabbed(const juce::Component& widget) const {
    for (int i = 0; i < (int)plan_.items.size(); ++i)
        if (plan_.items[(size_t)i].widget == &widget)
            return plan_.isTabbed(i);
    return false;
}

} // namespace synth
