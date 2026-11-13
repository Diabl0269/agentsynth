// DrumPadShown.cpp -- the Shown switch's undo steps, Show all as a toggle and its words, and the "hide it anyway?"
// question.
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadShown.h"
#include "UI/Chrome/ConfirmDontAskDialog.h"
#include <algorithm>

namespace synth::ui {

namespace {

// One undo step around `edit`, when the caller can record it.
template <typename Edit>
auto editUndoably(AppUndoManager* undo, juce::AudioProcessorGraph* graph, Edit&& edit) {
    const bool record = undo != nullptr && graph != nullptr;
    if (record)
        undo->captureBeforeState(*graph);
    const auto result = edit();
    if (record)
        undo->pushSnapshotFromCapture(*graph);
    return result;
}

} // namespace

bool setPadShownUndoably(AppUndoManager* undo, juce::AudioProcessorGraph* graph, DrumKitModule& kit, int index,
                         bool shown) {
    return setPadsShownUndoably(undo, graph, kit, {index}, shown) > 0;
}

int setPadsShownUndoably(AppUndoManager* undo, juce::AudioProcessorGraph* graph, DrumKitModule& kit,
                         const std::vector<int>& pads, bool shown) {
    const bool any = std::any_of(pads.begin(), pads.end(), [&](int i) {
        return i >= 0 && i < synth::drums::kMaxPads && kit.padShown(i) != shown;
    });
    if (!any)
        return 0;
    return editUndoably(undo, graph, [&] {
        int changed = 0;
        for (int i : pads)
            changed += kit.setPadShown(i, shown) ? 1 : 0;
        return changed;
    });
}

int showAllPadsUndoably(AppUndoManager* undo, juce::AudioProcessorGraph* graph, DrumKitModule& kit) {
    bool anyHidden = false;
    for (int i = 0; i < synth::drums::kMaxPads; ++i)
        anyHidden = anyHidden || !kit.padShown(i);
    if (!anyHidden)
        return 0;
    return editUndoably(undo, graph, [&] { return kit.showAllPads(); });
}

// The toggle reads the same state its chip shows: lit (pads remembered within the count) means the press hides them.
int toggleShowAllPadsUndoably(AppUndoManager* undo, juce::AudioProcessorGraph* graph, DrumKitModule& kit) {
    if (kit.revealedPadCount() > 0)
        return editUndoably(undo, graph, [&] { return kit.hidePadsAgain(); });
    return showAllPadsUndoably(undo, graph, kit);
}

// One wording for the card's chip, its pad menu and the drum machine's button, so the three never drift apart. The
// label stays "Show all" in both states (the lit pill says it is on, as the lane chips do), so the chip never changes
// width under the pointer; the caption and the tooltip carry the count.
ShowAllText showAllText(int hidden, int revealed) {
    ShowAllText text;
    const auto pads = [](int n) { return juce::String(n) + (n == 1 ? " pad" : " pads"); };
    if (revealed > 0) {
        text.on = true;
        text.enabled = true;
        text.caption = juce::String(revealed) + " shown again";
        text.tooltip = "Hide the " + pads(revealed) + " Show all brought back. Undo works too.";
        text.menuItem = revealed == 1 ? "Hide it again" : "Hide the " + juce::String(revealed) + " again";
    } else if (hidden > 0) {
        text.enabled = true;
        text.caption = juce::String(hidden) + " hidden";
        text.tooltip = "Show the " + pads(hidden) + " you hid. A second press hides the same pads again.";
        text.menuItem = "Show all pads";
    } else {
        text.tooltip = "Nothing is hidden. Hide a drum with its pad's Shown switch or from its row's menu.";
        text.menuItem = "Show all pads";
    }
    return text;
}

void applyShowAllChip(juce::ToggleButton& chip, const ShowAllText& text) {
    chip.setToggleState(text.on, juce::dontSendNotification);
    chip.setEnabled(text.enabled);
    chip.setTooltip(text.tooltip);
}

HideDrumConfirmText hideDrumConfirmText(const juce::String& padName, int hits) {
    return {padName + " has " + juce::String(hits) + (hits == 1 ? " hit" : " hits") + "; hide it anyway?",
            "A hidden drum still plays its hits; only its pad and its row go. Show all brings it back, and so does "
            "undo."};
}

namespace test_hooks {
std::function<void(const HideDrumConfirmText&, std::function<void(bool, bool)>)>& hideDrumConfirmHookForTest() {
    static std::function<void(const HideDrumConfirmText&, std::function<void(bool, bool)>)> hook;
    return hook;
}
} // namespace test_hooks

void confirmHideDrum(const HideDrumConfirmText& text, std::function<void(bool, bool)> done) {
    if (auto& hook = test_hooks::hideDrumConfirmHookForTest()) {
        hook(text, std::move(done));
        return;
    }
    showConfirmDontAsk({text.title, text.message, "Hide", "hideDrumDontAskAgain",
                        "Stop asking before hiding a drum that has hits. Hiding never mutes it."},
                       std::move(done));
}

int drumHitsOnNote(const TimelineDoc& doc, int note, const std::function<bool(TrackId)>& playsKit) {
    int hits = 0;
    for (const auto& track : doc.getTracks())
        if (playsKit(track.id))
            for (const auto& clip : track.clips)
                for (const auto& n : clip.notes)
                    hits += n.pitch == note ? 1 : 0;
    return hits;
}

void askBeforeHidingDrum(juce::Component& anchor, juce::ApplicationProperties& props, const juce::String& padName,
                         int hits, std::function<void()> proceed) {
    const auto* settings = props.getUserSettings();
    if (hits == 0 || (settings != nullptr && !settings->getBoolValue(kAskBeforeHidingDrumKey, true))) {
        proceed();
        return;
    }
    juce::Component::SafePointer<juce::Component> alive(&anchor); // props belongs to the anchor's owner
    juce::ApplicationProperties* propsPtr = &props;
    confirmHideDrum(hideDrumConfirmText(padName, hits),
                    [alive, propsPtr, proceed = std::move(proceed)](bool confirmed, bool dontAskAgain) {
                        if (alive == nullptr || !confirmed)
                            return;
                        if (dontAskAgain)
                            if (auto* userSettings = propsPtr->getUserSettings()) {
                                userSettings->setValue(kAskBeforeHidingDrumKey, "0");
                                userSettings->saveIfNeeded();
                            }
                        proceed();
                    });
}

} // namespace synth::ui
