#pragma once

#include "UI/Layout/ArrowKeyNavigation.h"
#include "UI/Layout/ChevronTurn.h"
#include <juce_gui_basics/juce_gui_basics.h>

// The header row of a folding section in a Settings tab: a turning chevron, the section's name in bold, an optional
// one-line summary after it and a hairline under it (docs/layout/settings-preferences.md). A juce::Button so it is a
// Tab stop with the accent focus ring; its accessible name carries the state ("Device, expanded").
namespace synth::ui {

constexpr int kFoldSectionHeaderHeight = 26;

/** Paints a header into `button`'s bounds. Shared by every folding Settings tab so they look and read alike. */
void paintFoldSectionHeader(juce::Graphics& g, const juce::Button& button, const juce::String& name,
                            const ChevronTurn& turn, bool hot, const juce::String& summary = {});

class FoldSectionHeader
    : public juce::Button
    , public FoldableHeader {
public:
    explicit FoldSectionHeader(const juce::String& name);

    /** Shown in the muted colour after the name; empty shows none. */
    void setSummary(const juce::String& summary);
    const juce::String& getSummary() const noexcept { return summary_; }

    /** The open/folded state: names the header for a screen reader and turns the chevron. */
    void setOpen(bool open);
    bool isOpen() const noexcept { return open_; }

    /** Called when a click or arrow key asks for the other state; the owner decides and calls setOpen. */
    std::function<void(bool open)> onOpenRequested;

    bool isFolded() const override { return !open_; }
    void setFolded(bool folded) override {
        if (onOpenRequested)
            onOpenRequested(!folded);
    }
    float getChevronOpenness() const noexcept { return turn_.openness(); }
    bool isChevronTurning() const noexcept { return turn_.isTurning(); }

    void paintButton(juce::Graphics& g, bool hot, bool down) override;

private:
    void refreshTitle();

    juce::String name_;
    juce::String summary_;
    bool open_ = true;
    ChevronTurn turn_{*this};
};

} // namespace synth::ui
