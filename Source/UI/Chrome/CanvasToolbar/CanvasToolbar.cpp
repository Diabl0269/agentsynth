#include "CanvasToolbar.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReadOnlyTextValue.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
// A captioned button is wide enough for "Auto Arrange" at the caption size; an icon-only one is the chip.
constexpr int kLabelledWidth = 84;
constexpr int kIconOnlyWidth = 32;
constexpr int kButtonGap = 2;
constexpr int kEdgeInset = 6;
constexpr int kButtonInset = 0;
constexpr int kRingOutset = 1;
} // namespace

CanvasToolbar::CanvasToolbar() {
    // Makes grabKeyboardFocus() on THIS component (its focus region's root) succeed deterministically; the
    // buttons below give up focus of their own so the roving ring is the only indicator.
    setWantsKeyboardFocus(true);
    setTitle("Canvas toolbar");
    for (auto* b : {&autoArrange_, &minimap_, &matrix_}) {
        addAndMakeVisible(*b);
        b->setWantsKeyboardFocus(false);
    }
    autoArrange_.setComponentID("autoArrangeButton");
    minimap_.setComponentID("toggleMinimap");
    matrix_.setComponentID("toggleModMatrix");
}

ToolbarButton& CanvasToolbar::buttonAt(Slot slot) noexcept {
    switch (slot) {
    case AutoArrange:
        return autoArrange_;
    case Minimap:
        return minimap_;
    case Matrix:
    case NumSlots:
        break;
    }
    return matrix_;
}

const ToolbarButton& CanvasToolbar::buttonAt(Slot slot) const noexcept {
    return const_cast<CanvasToolbar*>(this)->buttonAt(slot);
}

void CanvasToolbar::setIconOnly(bool iconOnly) {
    if (iconOnly_ == iconOnly)
        return;
    iconOnly_ = iconOnly;
    resized();
}

// Left-aligned in a row of fixed-width buttons. The width follows the caption mode, not the button text, so a
// layout pass before the owner has set any text still lands where the final one will.
void CanvasToolbar::resized() {
    const int width = iconOnly_ ? kIconOnlyWidth : kLabelledWidth;
    int x = kEdgeInset;
    for (int slot = 0; slot < NumSlots; ++slot) {
        auto& b = buttonAt((Slot)slot);
        if (!b.isVisible())
            continue;
        b.setBounds(x, kButtonInset, width, juce::jmax(0, getHeight() - 2 * kButtonInset));
        x += width + kButtonGap;
    }
}

void CanvasToolbar::paint(juce::Graphics& g) {
    auto* lnf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lnf == nullptr) {
        g.fillAll(juce::Colour(0xff0B0D10));
        return;
    }
    const auto& colors = lnf->getTheme().colors;
    g.fillAll(colors.bg0);
    // A quiet hairline between the strip and the canvas under it, the same weight the top bar's group rules use.
    g.setColour(colors.border.withAlpha(0.45f));
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void CanvasToolbar::paintOverChildren(juce::Graphics& g) {
    paintFocusRegionOutline(*this, g);

    const int slot = getFocusedSlot();
    if (slot < 0)
        return;
    const auto area = buttonAt((Slot)slot).getBounds().expanded(kRingOutset);
    paintFocusRing(g, area.toFloat(), *this, 4.0f);
}

// ---------------------------------------------------------------------------
// Roving focus, the same keys as the top bar.
bool CanvasToolbar::isNavigable(int slot) const {
    if (slot < 0 || slot >= NumSlots)
        return false;
    const auto& b = buttonAt((Slot)slot);
    return b.isVisible() && b.isEnabled();
}

int CanvasToolbar::navigableSlotFrom(int from, int step) const {
    for (int slot = from + step; slot >= 0 && slot < NumSlots; slot += step)
        if (isNavigable(slot))
            return slot;
    return -1;
}

// A last-used button that has since been disabled (the Minimap while the canvas is too short) hands the ring to
// its nearest navigable neighbour.
int CanvasToolbar::getFocusedSlot() const {
    if (isNavigable(focusedSlot_))
        return focusedSlot_;
    if (focusedSlot_ < 0)
        return navigableSlotFrom(-1, 1);
    const int after = navigableSlotFrom(focusedSlot_, 1);
    return after >= 0 ? after : navigableSlotFrom(focusedSlot_, -1);
}

juce::String CanvasToolbar::getFocusedButtonName() const {
    const int slot = getFocusedSlot();
    if (slot < 0)
        return {};
    const auto& b = buttonAt((Slot)slot);
    return b.getTitle().isNotEmpty() ? b.getTitle() : b.getButtonText();
}

void CanvasToolbar::setFocusedSlot(int slot) {
    if (slot < 0)
        return;
    const bool changed = slot != getFocusedSlot();
    focusedSlot_ = slot;
    if (!changed)
        return;
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

bool CanvasToolbar::keyPressed(const juce::KeyPress& key) {
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;
    const int current = getFocusedSlot();
    if (current < 0)
        return false;

    if (key.isKeyCode(juce::KeyPress::leftKey)) {
        setFocusedSlot(navigableSlotFrom(current, -1));
    } else if (key.isKeyCode(juce::KeyPress::rightKey)) {
        setFocusedSlot(navigableSlotFrom(current, 1));
    } else if (key.isKeyCode(juce::KeyPress::homeKey)) {
        setFocusedSlot(navigableSlotFrom(-1, 1));
    } else if (key.isKeyCode(juce::KeyPress::endKey)) {
        setFocusedSlot(navigableSlotFrom(NumSlots, -1));
    } else if (key.isKeyCode(juce::KeyPress::spaceKey) || key.isKeyCode(juce::KeyPress::returnKey)) {
        focusedSlot_ = current;
        auto& button = buttonAt((Slot)current);
        if (button.onClick)
            button.onClick();
        repaint();
    } else {
        return false;
    }
    return true;
}

void CanvasToolbar::focusGained(FocusChangeType) {
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

void CanvasToolbar::focusLost(FocusChangeType) { repaint(); }

std::unique_ptr<juce::AccessibilityHandler> CanvasToolbar::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::group, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{
            std::make_unique<ReadOnlyTextValue>([this] { return getFocusedButtonName(); })});
}

} // namespace synth::ui
