#pragma once

// Private to CardLayout.cpp / CardLayoutJson.cpp: the version-2 JSON half of CardLayout.

#include "CardLayout.h"

namespace synth::detail {

/** The v2 document for `layout`: its sections (or `slots` as one implicit untitled grid section). */
juce::var layoutToVarV2(const CardLayout& layout);

/** Fills `out` from a version-2 object. False when anything is out of range or unrecognised. */
bool layoutFromObjectV2(const juce::DynamicObject& object, CardLayout& out);

/** `slots` as one untitled grid section with id "main". */
CardSection sectionFromSlots(const std::vector<CardSlot>& slots);

/** The v1 slot kind a v2 widget degrades to. */
CardSlotKind slotKindFromWidget(CardWidget widget);
/** The v2 widget a v1 slot kind stands for. */
CardWidget widgetFromSlotKind(CardSlotKind kind);

} // namespace synth::detail
