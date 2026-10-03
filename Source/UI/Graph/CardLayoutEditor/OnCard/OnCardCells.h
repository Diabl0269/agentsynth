#pragma once

// What the on-card layout editor outlines: one cell per control a card draws in a grid section, read
// from the card's plan (its widgets' real bounds), and the layout written back with those cells' free
// positions. docs/layout/module-card-layout.md#editing-a-layout.

#include "Modules/CardLayout.h"
#include "OnCardLayoutMath.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

class ModuleComponent;

namespace synth {
class CardBody;
}

namespace synth::ui {

/** One control's cell: its widget and caption (one swap group shares a cell, shown by one member). */
struct OnCardCell {
    juce::String key; ///< The shown member's parameter id.
    juce::String caption;
    int section = -1;                   ///< Index into the plan's sections.
    std::vector<juce::String> paramIds; ///< Every member of the cell, shown or not.
    bool panelOnly = false;             ///< In the footer or a tab: opens the options panel, never moves.
    juce::Rectangle<int> rect;          ///< Caption and widget together, in card pixels.
    juce::Rectangle<int> widgetRel;     ///< The widget's bounds relative to rect's top-left.
    juce::Rectangle<int> labelRel;      ///< The caption's, empty without one.
    juce::Component::SafePointer<juce::Component> widget;
    juce::Component::SafePointer<juce::Component> label;
};

/** Every outlined cell of `card`, section by section in card order: shown controls of a visible section.
 *  The footer's and the selected tab's cells are `panelOnly`. */
std::vector<OnCardCell> collectCells(const ModuleComponent& card);

/** A view a grid section shows (its bounds on the card); views are not outlined, but keep their place. */
struct OnCardView {
    CardView view = CardView::Scope;
    juce::Rectangle<int> rect;
};

/** The views drawn in section `section` of `card` now. */
std::vector<OnCardView> collectViews(const ModuleComponent& card, int section);

/** Where section `section`'s cells may sit on a card `cardWidth` wide. */
oncard::Limits limitsFor(const synth::CardBody& body, int section, int cardWidth);

/** Moves the cell's widget and caption so the cell's top-left is `topLeft` (card pixels). */
void placeCell(const OnCardCell& cell, juce::Point<int> topLeft);

/** `layout` with a free position on every param item of `cells` (the cells of one section, at their
 *  `rect`) and on each of that section's `views`, relative to the section's content origin: x from
 *  `contentX`, y from `top`. */
CardLayout withCellPositions(CardLayout layout, const std::vector<OnCardCell>& cells,
                             const std::vector<OnCardView>& views, int contentX, int top);

} // namespace synth::ui
