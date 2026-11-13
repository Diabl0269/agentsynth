#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace synth::ui {

// Gives every juce::ListBox under `root` a keyboard focus ring around its selected row, for a stock
// JUCE component whose rows ignore the selection when they paint (the audio device selector's channel
// and MIDI input lists): a keyboard user can then see which row Up / Down and Return act on. While a
// list has keyboard focus the accent ring (FocusRing.h) is drawn around the selected row by a
// transparent, click-through child laid over the list; a list that takes focus with nothing selected
// gets row 0 selected (selecting never changes what a row does, so a tick is untouched).
//
// The stock selector rebuilds its lists when the device changes, so the set of lists is re-scanned
// whenever a child is added or removed under `root` or under a list's parent, and on every focus change.
// A light timer runs only while a list holds focus, to repaint when the selection moves.
// Message thread only. `root` must outlive this object.
class ListBoxFocusRing
    : public juce::FocusChangeListener
    , private juce::ComponentListener
    , private juce::Timer {
public:
    explicit ListBoxFocusRing(juce::Component& root);
    ~ListBoxFocusRing() override;

    // Finds the lists currently under the root, gives each a ring overlay and drops the ones that went away.
    void rescan();
    void globalFocusChanged(juce::Component* focused) override;

    int getNumListsForTest() const noexcept { return (int)entries_.size(); }
    // A headless window cannot hold keyboard focus: a list set here is treated as focused.
    void setFocusedListForTest(juce::ListBox* list);

private:
    class Overlay;
    struct Entry {
        juce::Component::SafePointer<juce::ListBox> list;
        std::unique_ptr<Overlay> overlay;
        int lastRow = -1;
    };

    void timerCallback() override;
    void componentChildrenChanged(juce::Component&) override { rescan(); }
    bool isFocused(const juce::ListBox& list) const;
    void updateTimer();
    void watchParentOf(juce::ListBox& list);

    juce::Component& root_;
    std::vector<Entry> entries_;
    std::vector<juce::Component::SafePointer<juce::Component>> watched_;
    juce::Component::SafePointer<juce::ListBox> forcedFocus_;
};

} // namespace synth::ui
