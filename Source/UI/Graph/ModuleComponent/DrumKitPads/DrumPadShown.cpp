// DrumPadShown.cpp -- the Shown switch's undo steps and the "hide it anyway?" question.
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadShown.h"
#include "UI/Chrome/ConfirmDontAskDialog.h"

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
    if (index < 0 || index >= synth::drums::kMaxPads || kit.padShown(index) == shown)
        return false;
    return editUndoably(undo, graph, [&] { return kit.setPadShown(index, shown); });
}

int showAllPadsUndoably(AppUndoManager* undo, juce::AudioProcessorGraph* graph, DrumKitModule& kit) {
    bool anyHidden = false;
    for (int i = 0; i < synth::drums::kMaxPads; ++i)
        anyHidden = anyHidden || !kit.padShown(i);
    if (!anyHidden)
        return 0;
    return editUndoably(undo, graph, [&] { return kit.showAllPads(); });
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
