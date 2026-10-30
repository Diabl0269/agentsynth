// DrumMachineKit.cpp -- when the open clip plays a Drum Kit, its pads decide the rows: a pad that is switched off has
// no row (its hits still play), and the "Show all" button switches the hidden pads back on.
// docs/timeline/drum-machine.md#a-drum-kits-pads-as-rows.

#include "DrumMachineComponent.h"

namespace synth::ui {

// Re-reads the owner's answer. True when it changed what the rows should be.
bool DrumMachineComponent::pullKitRows() {
    KitRows fresh;
    const bool has = kitRowsProvider && kitRowsProvider(fresh);
    const bool changed = has != hasKit_ || (has && fresh != kitRows_);
    hasKit_ = has;
    kitRows_ = has ? std::move(fresh) : KitRows();
    refreshShowAllButton();
    return changed;
}

void DrumMachineComponent::refreshKitRows() {
    if (!isOpen() || !pullKitRows())
        return;
    syncLanes(true);
    layoutRows();
    repaint();
}

juce::String DrumMachineComponent::kitRowsCaption() const {
    if (!hasKit_)
        return {};
    const int shown = kitRows_.total - kitRows_.hiddenCount;
    auto text = juce::String(shown) + " of " + juce::String(kitRows_.total) + " drums";
    if (kitRows_.hiddenCount > 0)
        text += juce::String::fromUTF8(" \xC2\xB7 ") + juce::String(kitRows_.hiddenCount) + " hidden";
    return text;
}

// With a kit the button is the kit's Show all: it works while any pad is hidden and says how many rows that is.
void DrumMachineComponent::refreshShowAllButton() {
    if (!hasKit_) {
        showAllButton_.setButtonText("Show all drums");
        showAllButton_.setTitle("Show all drums");
        showAllButton_.setEnabled(true);
        showAllButton_.setTooltip("Show every General MIDI drum as a row, not only the common ones");
        return;
    }
    const auto caption = kitRowsCaption();
    const bool any = kitRows_.hiddenCount > 0;
    showAllButton_.setButtonText("Show all");
    showAllButton_.setTitle(any ? "Show all, " + caption : "Show all, none hidden");
    showAllButton_.setEnabled(any);
    showAllButton_.setTooltip(any ? caption + ". Show every hidden drum as a row again."
                                  : caption + ". Hide a drum with its pad's Shown switch on the Drum Kit card.");
}

} // namespace synth::ui
