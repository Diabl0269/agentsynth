#pragma once

#include "Music/MusicalScale.h"
#include "UI/Layout/KeyPicker/KeyPickerPalette.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

/** Two octaves C..B drawn like a keyboard, the key's notes tinted and its root keys solid. Clicking a key reports its
 *  pitch class. Never takes keyboard focus. */
class KeyPickerKeyboard
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    KeyPickerKeyboard();

    std::function<void(int pitchClass)> onRootClicked;

    /** The key whose notes are tinted; nullopt draws plain keys. */
    void setKey(const std::optional<MusicalScale>& key);
    void setPalette(const keypicker::Palette& palette);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

    /** The pitch class under `p`, or -1. */
    int pitchClassAt(juce::Point<int> p) const;

private:
    juce::Rectangle<float> keyBounds(int semitone) const;

    std::optional<MusicalScale> key_;
    keypicker::Palette palette_;
};

} // namespace synth::ui
