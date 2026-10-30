// DrumPadGrid.cpp -- the pad grid: geometry, the pad faces, the sliding selection ring, the mouse and keys.
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadGrid.h"
#include "UI/DrumMachine/DrumLanes.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

using synth::drums::Pad;
using synth::drums::PadKind;

namespace {

constexpr float kCorner = 5.0f;
constexpr float kDot = 8.0f;
constexpr int kInset = 6;
constexpr float kNameFont = 12.0f;

juce::String kindWords(const DrumKitModule& kit, int index) {
    switch (kit.pad(index).kind) {
    case PadKind::File:
        return "audio file";
    case PadKind::Instrument:
        return "instrument";
    case PadKind::Kit:
        return kit.padPlaysRecording(index) ? "kit sound" : "built-in sound";
    case PadKind::BuiltIn:
        break;
    }
    return "built-in sound";
}

// The pitch the pad's colour follows: the drum it plays, or its own note when it plays none.
int colourPitch(const Pad& pad) { return pad.sound >= 0 ? pad.sound : pad.note; }

// The file mark: one period of a wave.
void paintWaveMark(juce::Graphics& g, juce::Rectangle<float> r) {
    juce::Path wave;
    wave.startNewSubPath(r.getX(), r.getCentreY());
    for (int i = 1; i <= 12; ++i) {
        const float t = (float)i / 12.0f;
        wave.lineTo(r.getX() + t * r.getWidth(),
                    r.getCentreY() - std::sin(t * juce::MathConstants<float>::twoPi) * r.getHeight() * 0.5f);
    }
    g.strokePath(wave, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace

// One pad to a screen reader: a button named after the pad. The grid paints it and takes its mouse and keys, so the
// grid stays one Tab stop.
class DrumPadGrid::Cell : public juce::Component {
public:
    Cell(DrumPadGrid& grid, int index)
        : grid_(grid)
        , index_(index) {
        setInterceptsMouseClicks(false, false);
        setWantsKeyboardFocus(false);
    }
    void paint(juce::Graphics& g) override { grid_.paintPad(g, index_, getLocalBounds()); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        return std::make_unique<juce::AccessibilityHandler>(
            *this, juce::AccessibilityRole::button,
            juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press,
                                                   [this] { grid_.pick(index_, true); }));
    }

private:
    DrumPadGrid& grid_;
    int index_;
};

int DrumPadGrid::heightFor(int count) {
    const int rows = (juce::jlimit(1, synth::drums::kMaxPads, count) + kColumns - 1) / kColumns;
    const int pad = count > kTallPadLimit ? kShortPad : kTallPad;
    return rows * pad + (rows - 1) * kGap;
}

DrumPadGrid::DrumPadGrid(DrumKitModule& kit)
    : kit_(kit) {
    setComponentID("drumPads");
    setTitle("Drum pads");
    setWantsKeyboardFocus(true);
    for (int i = 0; i < synth::drums::kMaxPads; ++i) {
        cells_[(size_t)i] = std::make_unique<Cell>(*this, i);
        addChildComponent(*cells_[(size_t)i]);
    }
    refresh();
}

DrumPadGrid::~DrumPadGrid() {
    ringSlide_.stop();
    flash_.stop();
}

juce::Component& DrumPadGrid::padCell(int index) {
    return *cells_[(size_t)juce::jlimit(0, synth::drums::kMaxPads - 1, index)];
}

void DrumPadGrid::refresh() {
    const int count = kit_.padCount();
    const bool countChanged = count != count_;
    count_ = count;
    for (int i = 0; i < synth::drums::kMaxPads; ++i) {
        auto& cell = *cells_[(size_t)i];
        cell.setVisible(i < count_);
        if (i < count_)
            cell.setTitle(padTitle(i));
    }
    if (picked_ >= count_)
        pick(count_ - 1, false);
    if (countChanged)
        resized();
    repaint();
}

juce::Rectangle<int> DrumPadGrid::padBounds(int index) const {
    const int padH = count_ > kTallPadLimit ? kShortPad : kTallPad;
    const int padW = (getWidth() - (kColumns - 1) * kGap) / kColumns;
    const int row = index / kColumns;
    const int col = index % kColumns;
    // The last column takes the pixels the division leaves, so the grid's right edge lines up with the card's.
    const int x = col * (padW + kGap);
    const int w = col == kColumns - 1 ? getWidth() - x : padW;
    return {x, row * (padH + kGap), w, padH};
}

int DrumPadGrid::padAt(juce::Point<int> p) const {
    for (int i = 0; i < count_; ++i)
        if (padBounds(i).contains(p))
            return i;
    return -1;
}

juce::String DrumPadGrid::padTitle(int index) const {
    return kit_.pad(index).name + " pad, " + kindWords(kit_, index) + ", " + juce::String(index + 1) + " of " +
           juce::String(count_);
}

juce::String DrumPadGrid::padTooltip(int index) const {
    return kit_.pad(index).name + ": click to play, Shift+click to pick without sound";
}

juce::String DrumPadGrid::getTooltip() {
    const int under = padAt(getMouseXYRelative());
    return under >= 0 ? padTooltip(under) : "Drum pads: click a pad to play it, Shift+click to pick it without sound";
}

bool DrumPadGrid::isFlashing(int index) const { return index == flashing_; }

void DrumPadGrid::resized() {
    for (int i = 0; i < synth::drums::kMaxPads; ++i)
        cells_[(size_t)i]->setBounds(i < count_ ? padBounds(i) : juce::Rectangle<int>());
    ringSlide_.stop();
    ringRect_ = padBounds(picked_).toFloat();
    repaint();
}

// ---- Picking and playing ----

void DrumPadGrid::pick(int index, bool play) {
    index = juce::jlimit(0, std::max(0, count_ - 1), index);
    picked_ = index;
    if (play) {
        kit_.auditionPad(index);
        auditioned_ = index;
        // The flash is feedback that the click sounded; with nothing animating (off screen, Animations Off) the sound
        // is the feedback.
        if (FadeVisibility::canAnimateIn(this)) {
            flashing_ = index;
            flash_.start(
                updater_, kFlashMs, [](float t) { return t; }, [](float) {},
                [this] {
                    flashing_ = -1;
                    repaint();
                });
        }
    }
    moveRingTo(index, true);
    if (onPicked)
        onPicked(index, play);
}

// The ring slides to the new pad; under Reduce Motion or Animations Off it moves at once.
void DrumPadGrid::moveRingTo(int index, bool animate) {
    const auto target = padBounds(index).toFloat();
    if (!animate || ringRect_.isEmpty() || prefersReducedMotion() || !FadeVisibility::canAnimateIn(this)) {
        ringSlide_.stop();
        ringRect_ = target;
        repaint();
        return;
    }
    const auto from = ringRect_;
    ringSlide_.start(updater_, kRingSlideMs, easeOutCubic, [this, from, target](float t) {
        ringRect_ = {from.getX() + (target.getX() - from.getX()) * t, from.getY() + (target.getY() - from.getY()) * t,
                     from.getWidth() + (target.getWidth() - from.getWidth()) * t,
                     from.getHeight() + (target.getHeight() - from.getHeight()) * t};
        repaint();
    });
}

void DrumPadGrid::mouseDown(const juce::MouseEvent& e) {
    const int index = padAt(e.getPosition());
    if (index >= 0)
        pick(index, !e.mods.isShiftDown());
}

// Left/Right step one pad, Up/Down one row, Home/End jump to the ends: all pick without a sound. Space and Return play
// the picked pad. Every other key, and any key with Cmd, Ctrl or Alt, passes on to the card and the app's shortcuts.
bool DrumPadGrid::keyPressed(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    const int code = key.getKeyCode();
    int target = -1;
    if (code == juce::KeyPress::leftKey)
        target = picked_ - 1;
    else if (code == juce::KeyPress::rightKey)
        target = picked_ + 1;
    else if (code == juce::KeyPress::upKey)
        target = picked_ - kColumns;
    else if (code == juce::KeyPress::downKey)
        target = picked_ + kColumns;
    else if (code == juce::KeyPress::homeKey)
        target = 0;
    else if (code == juce::KeyPress::endKey)
        target = count_ - 1;
    else if (code == juce::KeyPress::spaceKey || code == juce::KeyPress::returnKey) {
        pick(picked_, true);
        return true;
    } else {
        return false;
    }
    target = juce::jlimit(0, count_ - 1, target);
    if (target != picked_)
        pick(target, false);
    return true;
}

// ---- Painting ----

void DrumPadGrid::paintPad(juce::Graphics& g, int index, juce::Rectangle<int> area) const {
    const auto& c = synth::theme::themeOf(*this).colors;
    const auto& pad = kit_.pad(index);
    const auto r = area.toFloat().reduced(0.5f);
    g.setColour(isFlashing(index) ? c.surfaceHi.overlaidWith(c.accent.withAlpha(0.35f)) : c.surfaceHi);
    g.fillRoundedRectangle(r, kCorner);
    g.setColour(c.border);
    g.drawRoundedRectangle(r, kCorner, 1.0f);

    const auto top = area.reduced(kInset).removeFromTop(10).toFloat();
    g.setColour(drumLaneForPitch(colourPitch(pad)).colour);
    g.fillEllipse(top.getX(), top.getCentreY() - kDot * 0.5f, kDot, kDot);
    g.setColour(c.accent);
    if (pad.kind == PadKind::File)
        paintWaveMark(g, top.withLeft(top.getRight() - 12.0f).reduced(0.0f, 1.0f));
    else if (pad.kind == PadKind::Instrument)
        g.drawRect(top.withLeft(top.getRight() - 9.0f).withSizeKeepingCentre(9.0f, 9.0f), 1.4f);

    g.setColour(c.textPrimary);
    g.setFont(synth::theme::AppLookAndFeel::uiFont(kNameFont));
    g.drawText(pad.name, area.reduced(kInset, 0).removeFromBottom(kInset + 14).removeFromTop(14),
               juce::Justification::centredLeft, true);
}

// The picked pad's accent ring (sliding between pads) and, while the grid holds keyboard focus, the focus ring
// outside it.
void DrumPadGrid::paintOverChildren(juce::Graphics& g) {
    if (count_ <= 0)
        return;
    const auto& c = synth::theme::themeOf(*this).colors;
    g.setColour(c.accent);
    g.drawRoundedRectangle(ringRect_.reduced(1.0f), kCorner, 2.0f);
    if (hasKeyboardFocus(false))
        paintFocusRingAlways(g, getLocalBounds().toFloat(), *this, kCorner);
}

// ---- Screen reader and test mode ----

std::unique_ptr<juce::AccessibilityHandler> DrumPadGrid::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::group);
}

void DrumPadGrid::addTestModeState(juce::DynamicObject& into) const {
    into.setProperty("padCount", count_);
    into.setProperty("pickedPad", picked_ + 1);
    // auditionedPad is the last pad this grid asked to play; lastPlayedPad is set by the audio thread, so it stays 0
    // in a test-mode instance, which has no audio device.
    into.setProperty("auditionedPad", auditioned_ + 1);
    into.setProperty("lastPlayedPad", kit_.lastPlayedPad() + 1);
}

} // namespace synth::ui
