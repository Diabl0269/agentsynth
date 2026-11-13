#pragma once

// What the card layout editor edits: the parameters a card can show and where its layout is stored.
// Two implementations: a built-in module (BuiltInCardLayoutSource) and a hosted plugin instance
// (HostedCardLayoutSource). docs/layout/module-card-layout.md#editing-a-layout.

#include "Modules/CardLayout.h"
#include <optional>
#include <vector>

namespace synth::ui {

/** One parameter the card can show. */
struct CardLayoutEditorParam {
    juce::String paramId;
    juce::String displayName;
    int index = -1;                        ///< The hosted instance's parameter index; -1 for a built-in module.
    std::vector<CardWidget> widgetChoices; ///< The widgets that suit it, automatic first; fewer than 2 = no choice.
};

class CardLayoutEditorSource {
public:
    /** What unticking a row does to it. */
    enum class HiddenRows {
        StayInPlace,   ///< The item keeps its place and joins `hidden` (a built-in card's More row).
        LeaveTheLayout ///< The item leaves the layout; unticked rows list after the ticked ones (v1).
    };

    virtual ~CardLayoutEditorSource() = default;

    /** False once the module is gone; every write is then skipped. */
    virtual bool isAlive() const = 0;
    virtual juce::String title() const = 0;
    virtual juce::String thisScopeText() const = 0;
    virtual juce::String allScopeText() const = 0;
    virtual juce::String resetText() const = 0;
    virtual juce::String resetTooltip() const = 0;
    virtual HiddenRows hiddenRows() const = 0;
    /** True when rows are grouped by section and groups can be added and titled. */
    virtual bool supportsGroups() const = 0;

    virtual std::vector<CardLayoutEditorParam> parameters() const = 0;
    /** The layout the card draws now. */
    virtual CardLayout currentLayout() const = 0;
    /** Writes `layout` to this module, or (`allOfType`) as the type's default, clearing this module's
     *  own layout so it follows it. The card re-lays out at once. */
    virtual void apply(const CardLayout& layout, bool allOfType) = 0;
    /** Removes the chosen scope's layout; returns the layout the card now draws. */
    virtual CardLayout reset(bool allOfType) = 0;

    /** False without a preset store; the preset controls are then disabled. */
    virtual bool hasPresets() const = 0;
    virtual juce::StringArray listPresets() const = 0;
    virtual bool savePreset(const juce::String& name, const CardLayout& layout) = 0;
    virtual std::optional<CardLayout> loadPreset(const juce::String& name) const = 0;
    virtual bool deletePreset(const juce::String& name) = 0;
};

} // namespace synth::ui
