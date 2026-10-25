#include "KeyPickerKeyboard.h"

namespace synth::ui {

namespace {
constexpr int kWhiteKeys = 14;
constexpr int kSemitones = 24;
constexpr float kBlackHeight = 0.6f;
constexpr float kBlackWidth = 0.6f;

bool isBlack(int pitchClass) {
    switch (pitchClass) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
        return true;
    default:
        return false;
    }
}

// Index of the white key at or just below `semitone` (0..13).
int whiteIndexOf(int semitone) {
    int count = 0;
    for (int s = 0; s < semitone; ++s)
        if (!isBlack(s % 12))
            ++count;
    return count;
}
} // namespace

KeyPickerKeyboard::KeyPickerKeyboard() {
    setWantsKeyboardFocus(false);
    setTitle("Root keyboard");
    setTooltip("Click a key to make it the root.");
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void KeyPickerKeyboard::setKey(const std::optional<MusicalScale>& key) {
    key_ = key;
    repaint();
}

void KeyPickerKeyboard::setPalette(const keypicker::Palette& palette) {
    palette_ = palette;
    repaint();
}

juce::Rectangle<float> KeyPickerKeyboard::keyBounds(int semitone) const {
    const float whiteWidth = (float)getWidth() / (float)kWhiteKeys;
    const float height = (float)getHeight();
    const int white = whiteIndexOf(semitone);
    if (!isBlack(semitone % 12))
        return {(float)white * whiteWidth, 0.0f, whiteWidth, height};
    const float width = whiteWidth * kBlackWidth;
    return {(float)white * whiteWidth - width * 0.5f, 0.0f, width, height * kBlackHeight};
}

void KeyPickerKeyboard::paint(juce::Graphics& g) {
    for (int pass = 0; pass < 2; ++pass) {
        for (int s = 0; s < kSemitones; ++s) {
            const int pc = s % 12;
            if (isBlack(pc) != (pass == 1))
                continue;
            const bool black = pass == 1;
            auto colour = black ? palette_.blackKey : palette_.whiteKey;
            if (key_.has_value()) {
                const int interval = ((pc - key_->rootPitchClass) % 12 + 12) % 12;
                if (interval == 0)
                    colour = palette_.accent;
                else if ((key_->mask & (1u << interval)) != 0)
                    colour = colour.interpolatedWith(palette_.accent, black ? 0.5f : 0.4f);
            }
            const auto bounds = keyBounds(s);
            g.setColour(colour);
            g.fillRect(bounds);
            g.setColour(palette_.panel);
            g.drawRect(bounds, 0.6f);
        }
    }
}

int KeyPickerKeyboard::pitchClassAt(juce::Point<int> p) const {
    const auto point = p.toFloat();
    for (int pass = 0; pass < 2; ++pass) {
        for (int s = 0; s < kSemitones; ++s) {
            const bool black = isBlack(s % 12);
            if (black == (pass == 0) && keyBounds(s).contains(point))
                return s % 12;
        }
    }
    return -1;
}

void KeyPickerKeyboard::mouseDown(const juce::MouseEvent& e) {
    const int pc = pitchClassAt(e.getPosition());
    if (pc >= 0 && onRootClicked)
        onRootClicked(pc);
}

} // namespace synth::ui
