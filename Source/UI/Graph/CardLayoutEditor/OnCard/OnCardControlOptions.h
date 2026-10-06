#pragma once

// What the on-card editor's per-control panel shows and how its fields turn into layout edits: read from
// the card's plan and layout, and written back as pure layout changes, with no UI.
// docs/layout/module-card-layout.md#editing-a-layout.

#include "UI/Graph/CardLayoutEditor/CardLayoutEditorSource.h"
#include <optional>
#include <vector>

namespace synth {
class CardBody;
}

namespace synth::ui {

struct ControlOptions {
    juce::String paramId;
    juce::String displayName;                 ///< The parameter's own name.
    juce::String caption;                     ///< What the card shows: the override, else the name.
    std::vector<CardWidget> widgetChoices;    ///< Fewer than 2 = no Show as row.
    CardWidget widget = CardWidget::Auto;     ///< The choice now in force (never Auto when there are choices).
    std::optional<juce::Range<double>> range; ///< The layout's narrowed range.
    std::optional<juce::Range<double>>
        fullRange; ///< Set only when the Range row applies (a float shown as a knob or fader).
};

/** What a control is drawn as, apart from its size or direction: the panel's Show as lists these, and a knob's
 *  Size and a fader's Direction are separate rows. */
enum class WidgetKind { Auto, Knob, Fader, Toggle, Menu, Segmented, Stepper };

WidgetKind widgetKindOf(CardWidget widget);
/** "Knob", "Fader", "Toggle", "Menu", "Segmented", "Stepper". */
juce::String widgetKindName(WidgetKind kind);
/** The kinds `widgetChoices` offer, once each, in the order they first appear. */
std::vector<WidgetKind> widgetKinds(const std::vector<CardWidget>& widgetChoices);
/** The widget to draw when the user picks `kind`: `current` when it already is that kind (a large knob stays
 *  large, a horizontal fader stays horizontal), else the first choice of that kind. */
CardWidget widgetForKind(WidgetKind kind, CardWidget current, const std::vector<CardWidget>& widgetChoices);
/** The knob size / fader direction switches apply when both of the pair are among the choices and
 *  `current` is one of them. */
bool offersKnobSize(const std::vector<CardWidget>& widgetChoices, CardWidget current);
bool offersFaderDirection(const std::vector<CardWidget>& widgetChoices, CardWidget current);

/** The panel's contents for `paramId`, or nothing when the card shows no such control. `params` is the
 *  source's parameters(), `layout` the layout the card draws now. */
std::optional<ControlOptions> readControlOptions(const synth::CardBody& body,
                                                 const std::vector<CardLayoutEditorParam>& params,
                                                 const CardLayout& layout, const juce::String& paramId);

/** `layout` with an item for `paramId` in the section the card draws it in, when the layout has none (the
 *  footer's Poly toggle joins the footer without being listed): every edit below needs an item to change. */
CardLayout withPlacedItem(CardLayout layout, const juce::String& paramId, const synth::CardBody& body);

/** `layout` with `paramId`'s item drawn as `widget`, or unchanged when the layout has no such item. */
CardLayout withShowAs(CardLayout layout, const juce::String& paramId, CardWidget widget);
/** `layout` with `paramId`'s caption override set from `text` (labelOverrideFor's rule). */
CardLayout withControlLabel(CardLayout layout, const juce::String& paramId, const juce::String& displayName,
                            const juce::String& text);
/** `layout` with `paramId`'s range set, or cleared for nullopt. */
CardLayout withControlRange(CardLayout layout, const juce::String& paramId, std::optional<juce::Range<double>> range);

/** What typing a minimum and a maximum means for a parameter whose own range is `full`. */
struct RangeEntry {
    bool ok = false;
    std::optional<juce::Range<double>> range; ///< Nothing = the full range.
    juce::String hint;                        ///< Why it was refused.
};

/** Blank both = the full range; one blank leaves that end at the parameter's own. Each end is clamped to
 *  `full`; not a number, or a minimum that is not below the maximum, is refused. */
RangeEntry parseRangeEntry(const juce::String& minText, const juce::String& maxText, juce::Range<double> full);

/** A range end as the fields show it: plain digits, no trailing zeros. */
juce::String formatRangeValue(double value);

} // namespace synth::ui
