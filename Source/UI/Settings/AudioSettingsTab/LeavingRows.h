#pragma once

#include "UI/Layout/FadeVisibility.h"
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

// Rows that leave a Settings list fade out where they stood and are deleted afterwards, instead of vanishing. The
// owner hands a row it has dropped from its layout to retire(); the row stays a child of its parent while it fades
// (the click that dropped it may still be on the stack, so it is never deleted synchronously). Where nothing
// animates (a hidden tab, Animations Off) the row is gone at once and deleted on the next message-loop turn.
class LeavingRows : private juce::AsyncUpdater {
public:
    ~LeavingRows() override { cancelPendingUpdate(); }

    void retire(std::unique_ptr<juce::Component> row) {
        auto entry = std::make_unique<Entry>();
        entry->fade = std::make_unique<synth::ui::FadeVisibility>(std::vector<juce::Component*>{row.get()});
        entry->row = std::move(row);
        auto* raw = entry.get();
        entry->fade->onHidden = [this, raw] {
            raw->done = true;
            triggerAsyncUpdate();
        };
        entry->fade->setShown(false);
        if (!entry->row->isVisible())
            entry->done = true;
        entries_.push_back(std::move(entry));
        if (entries_.back()->done)
            triggerAsyncUpdate();
    }

    int count() const noexcept { return (int)entries_.size(); }

private:
    struct Entry {
        std::unique_ptr<juce::Component> row;
        std::unique_ptr<synth::ui::FadeVisibility> fade;
        bool done = false;
    };

    void handleAsyncUpdate() override {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [](const auto& e) { return e->done; }),
                       entries_.end());
    }

    std::vector<std::unique_ptr<Entry>> entries_;
};
