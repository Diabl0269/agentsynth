// NoteKeyStrip.cpp -- the By hand key map's keys: layout (white keys with black keys over them), painting the
// shape dots, clicks, keys and the accessibility buttons. The buttons take no clicks or focus themselves (the
// strip does), the same arrangement as CardSegmentedSwitch.
#include "NoteKeyStrip.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/TooltipHelpHandler.h"
#include "UI/ModuleViews/MorphMap/MorphMapPalette.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kBlackHeight = 0.6f; // of the strip's height
constexpr float kBlackWidth = 0.6f;  // of a white key's width
constexpr float kDot = 9.0f;

juce::String noteName(int note) { return juce::MidiMessage::getMidiNoteName(note, true, true, 4); }
} // namespace

/** A key as the accessibility tree sees it: a press cycles the key, like a click on the strip. It paints
    nothing; the strip paints every key. */
class NoteKeyStrip::KeyButton : public juce::Button {
public:
    explicit KeyButton(NoteKeyStrip& owner)
        : juce::Button("key")
        , owner_(owner) {
        setWantsKeyboardFocus(false);
        setInterceptsMouseClicks(false, false);
        onClick = [this] { owner_.cycle(note); };
    }
    void paintButton(juce::Graphics&, bool, bool) override {}
    int note = 0;

private:
    NoteKeyStrip& owner_;
};

NoteKeyStrip::NoteKeyStrip() {
    keys_.fill(-1);
    setTitle("Key map");
    setTooltip("Key map: click a key to pick the shape its notes go to (Space cycles, Page Up/Down shift the octave)");
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    for (int i = 0; i < kNumKeys; ++i) {
        auto* button = buttons_.add(new KeyButton(*this));
        addAndMakeVisible(button);
    }
    updateKeyNames();
}

NoteKeyStrip::~NoteKeyStrip() = default;

bool NoteKeyStrip::isBlack(int semitone) noexcept {
    switch (((semitone % 12) + 12) % 12) {
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

void NoteKeyStrip::setKeys(const std::array<int8_t, 128>& keys, const juce::StringArray& shapeNames) {
    if (keys == keys_ && shapeNames == shapeNames_)
        return;
    keys_ = keys;
    shapeNames_ = shapeNames;
    updateKeyNames();
    repaint();
}

void NoteKeyStrip::setBaseNote(int note) {
    const int base = juce::jlimit(kLowestBase, kHighestBase, note - (((note % 12) + 12) % 12));
    if (base == base_)
        return;
    base_ = base;
    updateKeyNames();
    repaint();
}

void NoteKeyStrip::shiftOctave(int octaves) { setBaseNote(base_ + 12 * octaves); }

void NoteKeyStrip::cycle(int note) {
    if (note < 0 || note >= 128)
        return;
    const int count = shapeNames_.size();
    const int current = keys_[(size_t)note];
    const int next = count == 0 ? -1 : (current < 0 ? 0 : (current + 1 < count ? current + 1 : -1));
    assign(note, next);
}

void NoteKeyStrip::assign(int note, int shape) {
    if (keys_[(size_t)note] == shape)
        return;
    keys_[(size_t)note] = (int8_t)shape;
    updateKeyNames();
    repaint();
    if (onKeyChanged)
        onKeyChanged(note, shape);
}

/** "C4: ah" (or "C4: no shape"); the tooltip says what a click does. */
void NoteKeyStrip::updateKeyNames() {
    for (int i = 0; i < buttons_.size(); ++i) {
        auto* b = buttons_[i];
        b->note = base_ + i;
        const int shape = keys_[(size_t)b->note];
        const auto target = shape >= 0 && shape < shapeNames_.size() ? shapeNames_[shape] : juce::String("no shape");
        b->setTitle(noteName(b->note) + ": " + target);
        b->setTooltip(noteName(b->note) + " goes to " + target + " - click for the next shape");
    }
}

juce::Button* NoteKeyStrip::getKeyButton(int note) const {
    const int i = note - base_;
    return i >= 0 && i < buttons_.size() ? buttons_[i] : nullptr;
}

/** White keys share the width equally; a black key straddles the line between its two white neighbours. */
juce::Rectangle<int> NoteKeyStrip::getKeyBounds(int note) const {
    const int i = note - base_;
    if (i < 0 || i >= kNumKeys)
        return {};
    int whites = 0, whiteIndex = 0;
    for (int k = 0; k < kNumKeys; ++k)
        if (!isBlack(k)) {
            if (k <= i)
                whiteIndex = whites;
            ++whites;
        }
    const float w = (float)getWidth() / (float)whites;
    if (!isBlack(i))
        return juce::Rectangle<float>((float)whiteIndex * w, 0.0f, w, (float)getHeight()).toNearestInt();
    const float centre = (float)(whiteIndex + 1) * w;
    return juce::Rectangle<float>(centre - w * kBlackWidth * 0.5f, 0.0f, w * kBlackWidth,
                                  (float)getHeight() * kBlackHeight)
        .toNearestInt();
}

void NoteKeyStrip::resized() {
    for (int i = 0; i < buttons_.size(); ++i)
        buttons_[i]->setBounds(getKeyBounds(base_ + i));
}

int NoteKeyStrip::keyAt(juce::Point<int> local) const {
    for (int i = 0; i < kNumKeys; ++i) // black keys sit on top
        if (isBlack(i) && getKeyBounds(base_ + i).contains(local))
            return base_ + i;
    for (int i = 0; i < kNumKeys; ++i)
        if (!isBlack(i) && getKeyBounds(base_ + i).contains(local))
            return base_ + i;
    return -1;
}

void NoteKeyStrip::paint(juce::Graphics& g) {
    const auto& colors = synth::theme::themeOf(*this).colors;
    for (bool black : {false, true})
        for (int i = 0; i < kNumKeys; ++i) {
            if (isBlack(i) != black)
                continue;
            const int note = base_ + i;
            const auto r = getKeyBounds(note).toFloat().reduced(black ? 0.0f : 0.5f, 0.0f);
            g.setColour(black ? colors.pianoKeyBlack : colors.pianoKeyWhite);
            g.fillRoundedRectangle(r, 2.0f);
            g.setColour(colors.border);
            g.drawRoundedRectangle(r, 2.0f, 1.0f);
            if (const int shape = keys_[(size_t)note]; shape >= 0 && shape < shapeNames_.size()) {
                g.setColour(morphShapeColour(colors, shape));
                g.fillEllipse(r.getCentreX() - kDot * 0.5f, r.getBottom() - kDot - 6.0f, kDot, kDot);
            }
            if (!black && note % 12 == 0) {
                g.setColour(colors.pianoKeyBlack.withAlpha(0.7f));
                g.setFont(juce::Font(juce::FontOptions(10.0f)));
                g.drawText(noteName(note),
                           r.withTrimmedTop(r.getHeight() * kBlackHeight).withHeight(12.0f).toNearestInt(),
                           juce::Justification::centred, false);
            }
        }
}

/** The ring follows the focused key while the strip holds focus. */
void NoteKeyStrip::paintOverChildren(juce::Graphics& g) {
    if (hasKeyboardFocus(false))
        paintFocusRingAlways(g, getKeyBounds(base_ + focused_).toFloat(), *this, 2.0f);
}

/** Left/Right walk the keys and shift the octave at either end, so every note is reachable from the keyboard. */
bool NoteKeyStrip::keyPressed(const juce::KeyPress& key) {
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
        const int step = code == juce::KeyPress::leftKey ? -1 : 1;
        const int target = focused_ + step;
        if (target < 0 && base_ > kLowestBase) {
            shiftOctave(-1);
            focused_ = 11;
        } else if (target >= kNumKeys && base_ < kHighestBase) {
            shiftOctave(1);
            focused_ = kNumKeys - 12;
        } else {
            focused_ = juce::jlimit(0, kNumKeys - 1, target);
        }
        repaint();
        return true;
    }
    if (code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey) {
        shiftOctave(code == juce::KeyPress::pageUpKey ? 1 : -1);
        return true;
    }
    if (code == juce::KeyPress::spaceKey || code == juce::KeyPress::returnKey) {
        cycle(base_ + focused_);
        return true;
    }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) {
        assign(base_ + focused_, -1);
        return true;
    }
    return false;
}

void NoteKeyStrip::mouseDown(const juce::MouseEvent& e) {
    if (!isEnabled() || e.mods.isPopupMenu())
        return;
    const int note = keyAt(e.getPosition());
    if (note < 0)
        return;
    focused_ = note - base_;
    cycle(note);
}

void NoteKeyStrip::focusGained(FocusChangeType) { repaint(); }
void NoteKeyStrip::focusLost(FocusChangeType) { repaint(); }

std::unique_ptr<juce::AccessibilityHandler> NoteKeyStrip::createAccessibilityHandler() {
    return std::make_unique<TooltipHelpHandler>(*this, juce::AccessibilityRole::group);
}

} // namespace synth::ui
