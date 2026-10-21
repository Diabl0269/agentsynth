// StepGridChordPicker.cpp -- the chord picker's layout (chips wrap inside 250 px), painting, mouse and keys.
#include "StepGridChordPicker.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/FocusRing.h"
#include "UI/ModuleViews/ModuleViewAccessibility.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr int kPad = 10;
constexpr int kLabelWidth = 54;
constexpr int kChipHeight = 22;
constexpr int kChipGap = 4;
constexpr int kChipPadding = 7;
constexpr int kRowGap = 6;
constexpr int kHeaderHeight = 14;
constexpr int kGroupHeight = 16;
constexpr float kFont = 11.0f;
constexpr float kSmallFont = 10.5f;

int chipWidth(const juce::String& name) { return theme::AppLookAndFeel::uiTextWidth(name, kFont) + 2 * kChipPadding; }

} // namespace

StepGridChordPicker::StepGridChordPicker(stepgrid::ChordPickerContent content, SequencerChord current)
    : content_(std::move(content))
    , current_(current) {
    setTitle("Chord picker");
    setTooltip("Chords for this step. Arrow keys move, Enter picks, Escape closes");
    setWantsKeyboardFocus(true);
    for (int r = 0; r < (int)content_.rows.size(); ++r)
        for (int c = 0; c < (int)content_.rows[(size_t)r].chips.size(); ++c)
            if (content_.rows[(size_t)r].chips[(size_t)c].chord == current_) {
                row_ = r;
                chip_ = c;
            }
    setSize(kWidth, layoutAll());
}

StepGridChordPicker::~StepGridChordPicker() { endAudition(); }

SequencerChord StepGridChordPicker::getFocusedChord() const {
    return content_.rows[(size_t)row_].chips[(size_t)chip_].chord;
}

juce::Rectangle<int> StepGridChordPicker::chipBounds(int row, int chip) const {
    for (const auto& p : placed_)
        if (p.row == row && p.chip == chip)
            return p.bounds;
    return {};
}

// Places the header, then each row's label and its chips wrapped inside the width; returns the height needed.
int StepGridChordPicker::layoutAll() {
    placed_.clear();
    rowPlaces_.assign(content_.rows.size(), {});
    int y = kPad + kHeaderHeight + kRowGap;
    for (int r = 0; r < (int)content_.rows.size(); ++r) {
        if (r == content_.firstOutsideRow) {
            y += 4;
            outsideHeaderY_ = y;
            y += kGroupHeight + 2;
        }
        const int left = kPad + kLabelWidth;
        const int right = kWidth - kPad;
        int x = left;
        int rowTop = y;
        for (int c = 0; c < (int)content_.rows[(size_t)r].chips.size(); ++c) {
            const int w = chipWidth(content_.rows[(size_t)r].chips[(size_t)c].name);
            if (x + w > right && x > left) {
                x = left;
                y += kChipHeight + kChipGap;
            }
            placed_.push_back({r, c, {x, y, w, kChipHeight}});
            x += w + kChipGap;
        }
        rowPlaces_[(size_t)r] = {rowTop, kChipHeight};
        y += kChipHeight + kRowGap;
    }
    return y - kRowGap + kPad;
}

void StepGridChordPicker::resized() { layoutAll(); }

void StepGridChordPicker::paint(juce::Graphics& g) {
    const auto& c = theme::themeOf(*this).colors;
    g.fillAll(c.surfaceHi);
    g.setFont(theme::AppLookAndFeel::uiFont(kSmallFont));
    g.setColour(c.textMuted);
    g.drawText(content_.header, juce::Rectangle<int>(kPad, kPad, kWidth - 2 * kPad, kHeaderHeight),
               juce::Justification::centredLeft);
    for (int r = 0; r < (int)content_.rows.size(); ++r) {
        g.setColour(c.textMuted);
        g.drawText(content_.rows[(size_t)r].label,
                   juce::Rectangle<int>(kPad, rowPlaces_[(size_t)r].labelY, kLabelWidth, kChipHeight),
                   juce::Justification::centredLeft);
    }
    g.setColour(c.border);
    g.fillRect(kPad, outsideHeaderY_ - 3, kWidth - 2 * kPad, 1);
    g.setColour(c.textMuted);
    g.drawText("Outside the key", juce::Rectangle<int>(kPad, outsideHeaderY_, kWidth - 2 * kPad, kGroupHeight),
               juce::Justification::centredLeft);

    g.setFont(theme::AppLookAndFeel::uiFont(kFont));
    const bool focused = hasKeyboardFocus(false);
    for (const auto& p : placed_) {
        const auto& chip = content_.rows[(size_t)p.row].chips[(size_t)p.chip];
        const bool isCurrent = chip.chord == current_;
        const bool hot = p.row == hoverRow_ && p.chip == hoverChip_;
        const auto bounds = p.bounds.toFloat();
        g.setColour(isCurrent ? c.accent.withAlpha(0.3f) : (hot ? c.accent.withAlpha(0.15f) : c.surface));
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(isCurrent || hot ? c.accent : c.border);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
        g.setColour(isCurrent ? c.textPrimary : c.textMuted.brighter(hot ? 0.4f : 0.0f));
        g.drawText(chip.name, p.bounds, juce::Justification::centred);
        if (focused && p.row == row_ && p.chip == chip_)
            paintFocusRingAlways(g, bounds, *this, 4.0f);
    }
}

bool StepGridChordPicker::findChip(juce::Point<int> p, int& row, int& chip) const {
    for (const auto& placed : placed_)
        if (placed.bounds.contains(p)) {
            row = placed.row;
            chip = placed.chip;
            return true;
        }
    return false;
}

void StepGridChordPicker::setHover(int row, int chip) {
    if (row == hoverRow_ && chip == hoverChip_)
        return;
    hoverRow_ = row;
    hoverChip_ = chip;
    if (onHover)
        onHover(row >= 0 ? std::optional<SequencerChord>(content_.rows[(size_t)row].chips[(size_t)chip].chord)
                         : std::nullopt);
    // Only the chip the pointer rests on plays: every move restarts the wait, leaving the chips ends the sound at once.
    if (row >= 0)
        startTimer(kAuditionDelayMs);
    else
        endAudition();
    repaint();
}

void StepGridChordPicker::timerCallback() {
    stopTimer();
    if (hoverRow_ < 0 || !onAudition)
        return;
    auditioning_ = true;
    onAudition(content_.rows[(size_t)hoverRow_].chips[(size_t)hoverChip_].chord);
}

void StepGridChordPicker::endAudition() {
    stopTimer();
    if (!auditioning_)
        return;
    auditioning_ = false;
    if (onAudition)
        onAudition(std::nullopt);
}

void StepGridChordPicker::mouseMove(const juce::MouseEvent& e) {
    int row = -1;
    int chip = -1;
    if (!findChip(e.getPosition(), row, chip))
        row = chip = -1;
    setHover(row, chip);
}

void StepGridChordPicker::mouseExit(const juce::MouseEvent&) { setHover(-1, -1); }

void StepGridChordPicker::mouseDown(const juce::MouseEvent& e) {
    int row = 0;
    int chip = 0;
    if (findChip(e.getPosition(), row, chip))
        pickChip(row, chip);
}

void StepGridChordPicker::pickChip(int row, int chip) {
    if (row < 0 || row >= (int)content_.rows.size() || chip < 0 || chip >= (int)content_.rows[(size_t)row].chips.size())
        return;
    const auto picked = content_.rows[(size_t)row].chips[(size_t)chip].chord;
    if (onPick)
        onPick(picked);
    closeHostingWindow(*this);
}

bool StepGridChordPicker::keyPressed(const juce::KeyPress& key) {
    const int code = key.getKeyCode();
    const int rows = (int)content_.rows.size();
    if (code == juce::KeyPress::escapeKey)
        return closeHostingWindow(*this);
    if (code == juce::KeyPress::returnKey || code == juce::KeyPress::spaceKey) {
        pickChip(row_, chip_);
        return true;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) {
        row_ = juce::jlimit(0, rows - 1, row_ + (code == juce::KeyPress::downKey ? 1 : -1));
        chip_ = juce::jlimit(0, (int)content_.rows[(size_t)row_].chips.size() - 1, chip_);
    } else if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
        chip_ = juce::jlimit(0, (int)content_.rows[(size_t)row_].chips.size() - 1,
                             chip_ + (code == juce::KeyPress::rightKey ? 1 : -1));
    } else {
        return false;
    }
    setHover(row_, chip_);
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    repaint();
    return true;
}

void StepGridChordPicker::focusGained(FocusChangeType) { repaint(); }
void StepGridChordPicker::focusLost(FocusChangeType) { repaint(); }

// The popover grows in, and the keyboard lands on the current chord's chip.
void StepGridChordPicker::parentHierarchyChanged() {
    reveal_.startIfInCallout();
    if (findParentComponentOfClass<juce::CallOutBox>() == nullptr)
        return;
    juce::Component::SafePointer<StepGridChordPicker> safe(this);
    juce::MessageManager::callAsync([safe] {
        if (safe != nullptr)
            safe->grabKeyboardFocus();
    });
}

// "Triad row, C, current chord": where the keyboard is.
juce::String StepGridChordPicker::describe() const {
    const auto& row = content_.rows[(size_t)row_];
    auto text = row.label + " row, " + row.chips[(size_t)chip_].name;
    if (row.chips[(size_t)chip_].chord == current_)
        text << ", current chord";
    return text;
}

std::unique_ptr<juce::AccessibilityHandler> StepGridChordPicker::createAccessibilityHandler() {
    return makeValueTextHandler(*this, [this] { return describe(); });
}

} // namespace synth::ui
