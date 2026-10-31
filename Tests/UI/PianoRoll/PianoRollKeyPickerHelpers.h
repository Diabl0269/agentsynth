#pragma once
// The key picker's side of the piano roll tests: open the scale-assist panel's picker and drive it. Apart from
// PianoRollTestHelpers.h so only the tests that use it include KeyPicker.h.
#include "PianoRollTestHelpers.h"
#include "UI/Layout/KeyPicker/KeyPicker.h"

// Picks a key through the panel's Key field and its key picker, as a click and a typed search would: `query` is the
// picker's search ("d dor"), and its first match is picked as the open clip's own key.
inline void pickPanelKey(synth::ui::ScaleAssistPanel& panel, const juce::String& query) {
    std::unique_ptr<juce::Component> held;
    panel.pickerLauncher = [&held](std::unique_ptr<juce::Component> picker, juce::Rectangle<int>) {
        held = std::move(picker);
    };
    panel.openKeyPicker();
    auto* picker = panel.getOpenKeyPicker();
    ASSERT_NE(picker, nullptr);
    picker->setQuery(query);
    picker->pickHighlighted();
    held.reset();
    panel.pickerLauncher = nullptr;
}

// The group titles of the key picker the panel opens (and closes again).
inline std::vector<juce::String> pickerGroupTitles(ScaleAssistPanel& panel) {
    std::unique_ptr<juce::Component> held;
    panel.pickerLauncher = [&held](std::unique_ptr<juce::Component> picker, juce::Rectangle<int>) {
        held = std::move(picker);
    };
    panel.openKeyPicker();
    std::vector<juce::String> titles;
    if (auto* picker = panel.getOpenKeyPicker())
        titles = picker->visibleGroupTitles();
    held.reset();
    panel.pickerLauncher = nullptr;
    return titles;
}

inline bool hasTitle(const std::vector<juce::String>& titles, const juce::String& title) {
    return std::find(titles.begin(), titles.end(), title) != titles.end();
}

// Chooses the picker's Follow song key row for the open clip.
inline void chooseFollowSongKey(synth::ui::ScaleAssistPanel& panel) {
    std::unique_ptr<juce::Component> held;
    panel.pickerLauncher = [&held](std::unique_ptr<juce::Component> picker, juce::Rectangle<int>) {
        held = std::move(picker);
    };
    panel.openKeyPicker();
    ASSERT_NE(panel.getOpenKeyPicker(), nullptr);
    panel.getOpenKeyPicker()->clickFollow();
    held.reset();
    panel.pickerLauncher = nullptr;
}

// Picks C Major as the open clip's own key, which is how the panel, the chip and the key all learn the scale.
inline void chooseMajorScaleForOpenClip(PianoRollFixture& f) { pickPanelKey(f.roll.getScaleAssistPanel(), "c major"); }
