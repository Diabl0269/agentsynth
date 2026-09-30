// Concern: MixerPanelComponent's pin / hide edits of the project's MixerViewDoc -- each one an undo
// step that rebuilds every live mixer view -- and the side pane's open / close.
#include "AppUndoManager.h"
#include "MixerPanelComponent.h"

namespace synth::ui {

bool MixerPanelComponent::toggleSidePane(bool forceOpen) {
    if (!sidePane_.hasContent())
        return false;
    if (forceOpen)
        sidePane_.setOpen(true);
    else
        sidePane_.toggle();
    return true;
}

// The view document is saved with the project, so an edit is an undo step exactly like the pan law or a
// macro collapse -- that is also what marks the document unsaved. The restore hook captures the owner's
// callback rather than this panel, which may be a mirror view closed by the time the step is undone.
void MixerPanelComponent::applyViewEdit(const std::function<void(synth::MixerViewDoc&)>& edit, bool keepSoloRestore) {
    const auto before = viewDoc_->toVar();
    edit(*viewDoc_);
    const auto after = viewDoc_->toVar();
    if (!keepSoloRestore) {
        soloShownId_ = {};
        soloRestoreHidden_.clear();
    }
    if (undoManager_ != nullptr) {
        juce::Component::SafePointer<MixerPanelComponent> safe(this);
        undoManager_->recordMixerViewChange(*viewDoc_, before, after, [safe, callback = onMixerViewChanged] {
            if (callback)
                callback();
            else if (safe != nullptr)
                safe->rebuild();
            if (safe != nullptr) {
                safe->soloShownId_ = {};
                safe->soloRestoreHidden_.clear();
            }
        });
    }
    refreshAfterViewChange();
}

void MixerPanelComponent::refreshAfterViewChange() {
    if (onMixerViewChanged)
        onMixerViewChanged();
    else
        rebuild();
}

void MixerPanelComponent::pinChannel(const juce::String& channelId, synth::MixerZone zone) {
    applyViewEdit([&](synth::MixerViewDoc& doc) { doc.setZone(channelId, zone); });
}

void MixerPanelComponent::setChannelHidden(const juce::String& channelId, bool hidden) {
    applyViewEdit([&](synth::MixerViewDoc& doc) { doc.setHidden(channelId, hidden); });
}

void MixerPanelComponent::showAllChannels() {
    applyViewEdit([](synth::MixerViewDoc& doc) { doc.showAll(); });
}

// Alt-click on an eye. The first one remembers what was hidden and hides everything else (Master cannot
// be hidden, so it stays); a second on the same channel puts the remembered set back. Any other visibility
// edit forgets the memory, so the "again" only ever undoes the solo-show it follows.
void MixerPanelComponent::soloShowChannel(const juce::String& channelId) {
    if (soloShownId_ == channelId && !soloShownId_.isEmpty()) {
        const auto restore = soloRestoreHidden_;
        applyViewEdit([&](synth::MixerViewDoc& doc) {
            doc.showAll();
            for (const auto& id : restore)
                doc.setHidden(id, true);
        });
        return;
    }
    const auto previous = viewDoc_->getHiddenIds();
    const auto ids = channelIds_;
    applyViewEdit(
        [&](synth::MixerViewDoc& doc) {
            for (const auto& id : ids)
                doc.setHidden(id, id != channelId);
        },
        /*keepSoloRestore=*/true);
    soloShownId_ = channelId;
    soloRestoreHidden_ = previous;
}

} // namespace synth::ui
