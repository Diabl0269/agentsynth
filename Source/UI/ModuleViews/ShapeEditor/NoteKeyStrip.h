#pragma once

// NoteKeyStrip.h -- the By hand key map: an octave and a half of piano keys, each wearing the colour dot of the
// shape its notes go to. Clicking a key cycles its shape (none, then each shape in turn); two buttons beside it
// shift the octave.
#include <array>
#include <cstdint>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

/** One Tab stop. Left/Right move the focused key, Space or Return cycles it, Delete or Backspace clears it, Page
 *  Up/Down shift the octave. To a screen reader it is a group holding one button per key ("C4: ah"). */
class NoteKeyStrip
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    static constexpr int kNumKeys = 18; // C to F an octave up
    static constexpr int kLowestBase = 0;
    static constexpr int kHighestBase = 108;
    static constexpr int kDefaultBase = 48; // C3

    NoteKeyStrip();
    ~NoteKeyStrip() override;

    /** Fired with the note and its new shape (-1 = none) when the user changes a key. */
    std::function<void(int note, int shape)> onKeyChanged;

    /** `keys` is one entry per MIDI note (a shape index or -1); `shapeNames` names the shapes, in order. */
    void setKeys(const std::array<int8_t, 128>& keys, const juce::StringArray& shapeNames);
    int getShapeFor(int note) const noexcept { return note >= 0 && note < 128 ? keys_[(size_t)note] : -1; }

    /** The lowest note shown, a C; clamped to kLowestBase..kHighestBase. */
    void setBaseNote(int note);
    int getBaseNote() const noexcept { return base_; }
    void shiftOctave(int octaves);

    /** Moves `note`'s key to the next shape (none -> first shape -> ... -> last shape -> none). */
    void cycle(int note);
    int getFocusedNote() const noexcept { return base_ + focused_; }

    /** The key button for `note` while it is shown, else null. */
    juce::Button* getKeyButton(int note) const;
    juce::Rectangle<int> getKeyBounds(int note) const;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    class KeyButton;

    static bool isBlack(int semitone) noexcept;
    int keyAt(juce::Point<int> local) const;
    void assign(int note, int shape);
    void updateKeyNames();

    std::array<int8_t, 128> keys_{};
    juce::StringArray shapeNames_;
    int base_ = kDefaultBase;
    int focused_ = 0; // 0..kNumKeys-1
    juce::OwnedArray<KeyButton> buttons_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteKeyStrip)
};

} // namespace synth::ui
