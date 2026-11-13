// DrumMachineKit.cpp -- when the open clip plays a Drum Kit, its pads decide the rows: a pad that is switched off has
// no row (its hits still play), and the "Show all" toggle switches the hidden pads back on and, pressed again, hides
// the same pads again.
// docs/timeline/drum-machine.md#a-drum-kits-pads-as-rows.

#include "DrumMachineComponent.h"
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadShown.h"

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
    if (const auto more = showAllText(kitRows_.hiddenCount, kitRows_.revealedCount).caption; more.isNotEmpty())
        text += juce::String::fromUTF8(" \xC2\xB7 ") + more;
    return text;
}

// With a kit the button is the kit's Show all toggle, in the card chip's words: it works while a pad is hidden, and
// lit, while Show all can hide the pads it brought back again. Without one it is the General MIDI table's own toggle.
// Its width follows its text (the toolbar is laid out again when that changes).
void DrumMachineComponent::refreshShowAllButton() {
    const auto oldText = showAllButton_.getButtonText();
    if (!hasKit_) {
        showAllButton_.setButtonText("Show all drums");
        showAllButton_.setTitle("Show all drums");
        showAllButton_.setEnabled(true);
        showAllButton_.setToggleState(showAll_, juce::dontSendNotification);
        showAllButton_.setTooltip("Show every General MIDI drum as a row, not only the common ones");
    } else {
        const auto text = showAllText(kitRows_.hiddenCount, kitRows_.revealedCount);
        showAllButton_.setButtonText("Show all");
        showAllButton_.setTitle(text.enabled ? "Show all, " + kitRowsCaption() : "Show all, none hidden");
        applyShowAllChip(showAllButton_, text);
        showAllButton_.setTooltip(kitRowsCaption() + ". " + text.tooltip);
    }
    if (showAllButton_.getButtonText() != oldText)
        layoutToolbar();
}

} // namespace synth::ui
