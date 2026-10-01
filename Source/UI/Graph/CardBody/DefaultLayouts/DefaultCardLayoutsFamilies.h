#pragma once

// The per-family registration functions DefaultCardLayouts::builtIn() calls, one per
// DefaultCardLayouts<Family>.cpp, and small builders that keep a hand-written layout short.
// docs/layout/module-card-layout.md#default-layouts.

#include "UI/Graph/CardBody/DefaultCardLayouts.h"

namespace synth {

void registerSourceCardLayouts(DefaultCardLayouts& defaults);
void registerEnvelopeCardLayouts(DefaultCardLayouts& defaults);
void registerFilterDynamicsCardLayouts(DefaultCardLayouts& defaults);
void registerEffectCardLayouts(DefaultCardLayouts& defaults);

namespace cardlayout {

inline CardParamItem param(const juce::String& paramId, CardWidget widget = CardWidget::Auto) {
    CardParamItem item;
    item.paramId = paramId;
    item.widget = widget;
    return item;
}

/** `item` shown only while `conditionParam`'s value is one of `is` (a swap with its neighbours). */
inline CardParamItem showWhen(CardParamItem item, const juce::String& conditionParam, juce::StringArray is) {
    item.when = CardCondition{conditionParam, std::move(is), CardConditionEffect::Show};
    return item;
}

/** `item` greyed out unless `conditionParam`'s value is one of `is`. */
inline CardParamItem dimUnless(CardParamItem item, const juce::String& conditionParam, juce::StringArray is) {
    item.when = CardCondition{conditionParam, std::move(is), CardConditionEffect::Dim};
    return item;
}

inline CardViewItem view(CardView kind, bool open = true) { return CardViewItem{kind, open}; }

inline CardSection section(const juce::String& id, std::optional<juce::String> title, std::vector<CardItem> items,
                           int columns = CardSection::kDefaultColumns) {
    CardSection result;
    result.id = id;
    result.title = std::move(title);
    result.columns = columns;
    result.items = std::move(items);
    return result;
}

/** The footer row (CardSection::kFooterId): toggles as pills, a level as a horizontal fader. */
inline CardSection footer(std::vector<CardItem> items) {
    return section(CardSection::kFooterId, std::nullopt, std::move(items));
}

} // namespace cardlayout

} // namespace synth
