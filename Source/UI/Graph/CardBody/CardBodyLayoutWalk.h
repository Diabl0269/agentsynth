#pragma once

// The card body's layout walk as free functions over a plan, shared by CardBody (live widgets) and
// CardBodyMeasure (a plan never built, so every widget is null and only apply = false is valid).

#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/CardBody/CardBodyPlan.h"

namespace synth {

/** The width and height a freeform section gives one control of `kind` (caption included) in a section of
 *  `columns`; empty for a view, whose height is its factory's. */
juce::Point<int> cardBodyCellSize(CardBodyItem::Kind kind, int columns, const cardbody::BodyGeometry& g);

/** `indices` (into plan.items) in card order, `columns` knobs per row; returns the y below them. */
int layoutCardBodyItems(const CardBodyPlan& plan, juce::AudioProcessor& module, const std::vector<int>& indices,
                        int columns, int y, const cardbody::BodyGeometry& g, bool apply);

/** Every shown section of `plan` but the footer, top to bottom (a tab group as its strip and its
 *  tallest tab); returns the y below them. */
int layoutCardBodySections(const CardBodyPlan& plan, juce::AudioProcessor& module, int y,
                           const cardbody::BodyGeometry& g, bool apply);

/** A card chrome toggle that joins the footer row: its button (null when measuring) and its text. */
struct CardFooterExtra {
    juce::Component* widget = nullptr;
    juce::String text;
};

/** The footer row: the plan's footer items, then `extras`, left to right, wrapping when the card is
 *  too narrow; returns the y below it (`y` itself when there is nothing to show). */
int layoutCardBodyFooter(const CardBodyPlan& plan, const std::vector<CardFooterExtra>& extras, int y,
                         const cardbody::BodyGeometry& g, bool apply);

} // namespace synth
