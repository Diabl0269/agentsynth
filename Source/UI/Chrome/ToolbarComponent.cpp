#include "ToolbarComponent.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReadOnlyTextValue.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

// ---------------------------------------------------------------------------
ToolbarComponent::ToolbarComponent() {
    // Makes grabKeyboardFocus() on THIS component (the "toolbar" focus region's root) succeed
    // deterministically rather than depending on JUCE's position-ordered descent into children --
    // see the identical comment in ModuleLibraryComponent's ctor. This component owns no children of
    // its own (the buttons are direct children of MainComponent), so there is nothing for descent to
    // find anyway; opting in here is what makes a grab land on the toolbar itself instead of failing.
    setWantsKeyboardFocus(true);
    setTitle("Toolbar");
}

// The buttons give up keyboard focus of their own: the toolbar is the single Tab stop and its roving
// ring is the only focus indicator, so a focused button could never double the ring or swallow the
// arrow keys before they reach keyPressed().
void ToolbarComponent::setButtons(std::array<juce::DrawableButton*, NumSlots> btns) {
    buttons_ = btns;
    for (auto* b : buttons_)
        if (b != nullptr)
            b->setWantsKeyboardFocus(false);
}

// ---------------------------------------------------------------------------
// Sub-group membership per Slot, used both to space groups apart in layoutButtons() and to
// find the group boundaries paint() draws separator hairlines at. Matches the Slot enum's
// documented grouping (ToolbarComponent.h:21-22): left [Library]|[New,Save,Load]|
// [Settings,Feedback]|[Undo,Redo]|[AutoArrange]; right
// [ToggleMinimap,ToggleModMatrix,ToggleAiPanel,ToggleBottomPanel]|[ToggleTheme].
namespace {
int groupOf(int slot) {
    switch (slot) {
    case ToolbarComponent::Library:
        return 0;
    case ToolbarComponent::New:
    case ToolbarComponent::Save:
    case ToolbarComponent::Load:
        return 1;
    case ToolbarComponent::Settings:
    case ToolbarComponent::Feedback:
        return 2;
    case ToolbarComponent::Undo:
    case ToolbarComponent::Redo:
        return 3;
    case ToolbarComponent::AutoArrange:
        return 4;
    case ToolbarComponent::ToggleMinimap:
    case ToolbarComponent::ToggleModMatrix:
    case ToolbarComponent::ToggleAiPanel:
    case ToolbarComponent::ToggleBottomPanel:
        return 5;
    case ToolbarComponent::ToggleTheme:
        return 6;
    default:
        return -1;
    }
}
} // namespace

// ---------------------------------------------------------------------------
void ToolbarComponent::layoutButtons(juce::Rectangle<int> bounds) {
    // At or below the breakpoint (== the enforced minimum window width) the wide-mode
    // labelled buttons no longer fit, so collapse to icon-only.
    narrowMode_ = bounds.getWidth() <= narrowThreshold_;

    // Wide-mode preferred widths (icon + text). Narrow mode collapses everything to 32 px.
    static constexpr float kNarrowPref = 32.0f;
    const std::array<float, NumSlots> widePref = {
        96.0f,  // Library
        88.0f,  // New
        112.0f, // Save
        116.0f, // Load
        96.0f,  // Settings
        40.0f,  // Feedback — always icon-only, never grows a text label
        72.0f,  // Undo
        72.0f,  // Redo
        120.0f, // AutoArrange
        108.0f, // ToggleMinimap ("Hide Minimap"/"Show Minimap")
        104.0f, // ToggleModMatrix
        92.0f,  // ToggleAiPanel
        100.0f, // ToggleBottomPanel ("Hide Panel"/"Show Panel")
        110.0f  // ToggleTheme
    };

    auto prefFor = [&](int slot) { return narrowMode_ ? kNarrowPref : widePref[(size_t)slot]; };

    juce::FlexBox fb;
    fb.flexDirection = juce::FlexBox::Direction::row;
    fb.alignItems = juce::FlexBox::AlignItems::center;

    // Small gap inserted between sub-groups (on top of each button's own margin) so related
    // actions read as clusters rather than one flat row.
    static constexpr float kGroupGap = 12.0f;

    // Left group: Library, Save, Load, Settings, Undo, Redo, AutoArrange.
    // Invisible buttons yield their slot entirely rather than leaving a reserved gap.
    int lastGroup = -1;
    for (int slot = Library; slot <= AutoArrange; ++slot)
        if (buttons_[(size_t)slot] != nullptr && buttons_[(size_t)slot]->isVisible()) {
            if (lastGroup != -1 && groupOf(slot) != lastGroup)
                fb.items.add(juce::FlexItem().withWidth(kGroupGap));
            lastGroup = groupOf(slot);
            fb.items.add(juce::FlexItem(*buttons_[(size_t)slot])
                             .withMinWidth(0.0f)
                             .withWidth(prefFor(slot))
                             .withHeight((float)bounds.getHeight())
                             .withMargin(juce::FlexItem::Margin(3.0f)));
        }

    // Flexible spacer pushes the right group to the far edge.
    fb.items.add(juce::FlexItem().withFlex(1.0f));

    // Right group: ToggleMinimap, ToggleModMatrix, ToggleAiPanel, ToggleBottomPanel, ToggleTheme.
    lastGroup = -1;
    for (int slot = ToggleMinimap; slot <= ToggleTheme; ++slot)
        if (buttons_[(size_t)slot] != nullptr && buttons_[(size_t)slot]->isVisible()) {
            if (lastGroup != -1 && groupOf(slot) != lastGroup)
                fb.items.add(juce::FlexItem().withWidth(kGroupGap));
            lastGroup = groupOf(slot);
            fb.items.add(juce::FlexItem(*buttons_[(size_t)slot])
                             .withMinWidth(0.0f)
                             .withWidth(prefFor(slot))
                             .withHeight((float)bounds.getHeight())
                             .withMargin(juce::FlexItem::Margin(3.0f)));
        }

    fb.performLayout(bounds.toFloat());
}

// ---------------------------------------------------------------------------
void ToolbarComponent::paint(juce::Graphics& g) {
    using namespace synth::theme;

    auto* lnf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel());
    if (lnf != nullptr)
        g.fillAll(lnf->getTheme().colors.bg0);
    else
        g.fillAll(juce::Colour(0xff0B0D10));

    if (lnf == nullptr)
        return; // headless test LnF: no themed border token available, skip separators.

    // Draw a hairline at each sub-group boundary, using the buttons' own post-layout bounds so
    // this pass never disagrees with layoutButtons()'s FlexItem order. Only consider slots whose
    // button is non-null && isVisible() — the same guard layoutButtons() already applies, so an
    // unset slot can't anchor a separator on stale bounds.
    // Kept subtle (~45% alpha, inset off the strip's top/bottom edge) so it reads as a quiet
    // grouping cue rather than a hard divider.
    g.setColour(lnf->getTheme().colors.border.withAlpha(0.45f));

    // Sections match the two layoutButtons() loops (left: Library..AutoArrange, right:
    // ToggleMinimap..ToggleTheme) — the boundary BETWEEN sections is the existing withFlex(1.0f)
    // spacer, not a sub-group gap, so it is deliberately excluded here.
    auto sectionOf = [](int slot) { return slot <= AutoArrange ? 0 : 1; };

    int prevGroup = -1;
    int prevSection = -1;
    int prevRight = -1;
    for (int slot = 0; slot < NumSlots; ++slot) {
        auto* b = buttons_[(size_t)slot];
        if (b == nullptr || !b->isVisible())
            continue;

        const int group = groupOf(slot);
        const int section = sectionOf(slot);
        const auto bounds = b->getBounds();
        if (prevGroup != -1 && group != prevGroup && section == prevSection && prevRight != -1 &&
            bounds.getX() > prevRight) {
            const float midX = 0.5f * (float)(prevRight + bounds.getX());
            const float inset = (float)getHeight() * 0.22f; // proportional so it scales with toolbarHeight
            g.drawLine(midX, inset, midX, (float)getHeight() - inset, 1.0f);
        }
        prevGroup = group;
        prevSection = section;
        prevRight = bounds.getRight();
    }
}

// Focus-region outline (Source/UI/Layout/FocusRegion.h) -- see the paintOverChildren declaration's
// comment in the header for why this component uses the same convention as the other five region
// roots despite owning no children of its own.
void ToolbarComponent::paintOverChildren(juce::Graphics& g) {
    synth::ui::paintFocusRegionOutline(*this, g);

    // The ring goes in the margin around the button, not on top of it: the buttons are siblings that
    // paint above this strip, so anything drawn inside their bounds would be covered.
    const int slot = getFocusedSlot();
    if (slot < 0)
        return;
    constexpr int kRingOutset = 2;
    const auto area = buttons_[(size_t)slot]->getBounds().translated(-getX(), -getY()).expanded(kRingOutset);
    synth::ui::paintFocusRing(g, area.toFloat(), *this, 4.0f);
}

// ---------------------------------------------------------------------------
// Roving focus.
bool ToolbarComponent::isNavigable(int slot) const {
    if (slot < 0 || slot >= NumSlots)
        return false;
    const auto* b = buttons_[(size_t)slot];
    return b != nullptr && b->isVisible() && b->isEnabled();
}

int ToolbarComponent::navigableSlotFrom(int from, int step) const {
    for (int slot = from + step; slot >= 0 && slot < NumSlots; slot += step)
        if (isNavigable(slot))
            return slot;
    return -1;
}

// A last-used button that has since been hidden or disabled (Undo right after it ran out of steps)
// hands the ring to its nearest navigable neighbour, so it never jumps back to the far left.
int ToolbarComponent::getFocusedSlot() const {
    if (isNavigable(focusedSlot_))
        return focusedSlot_;
    if (focusedSlot_ < 0)
        return navigableSlotFrom(-1, 1);
    const int after = navigableSlotFrom(focusedSlot_, 1);
    return after >= 0 ? after : navigableSlotFrom(focusedSlot_, -1);
}

juce::String ToolbarComponent::getFocusedButtonName() const {
    const int slot = getFocusedSlot();
    if (slot < 0)
        return {};
    const auto* b = buttons_[(size_t)slot];
    return b->getTitle().isNotEmpty() ? b->getTitle() : b->getButtonText();
}

void ToolbarComponent::setFocusedSlot(int slot) {
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

bool ToolbarComponent::keyPressed(const juce::KeyPress& key) {
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
        // Pin the ring to the pressed button first: an action that disables it (Undo's last step)
        // then hands the ring to a neighbour via getFocusedSlot().
        focusedSlot_ = current;
        auto* button = buttons_[(size_t)current];
        if (button->onClick)
            button->onClick();
        repaint();
    } else {
        return false;
    }
    return true;
}

void ToolbarComponent::focusGained(FocusChangeType) {
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

void ToolbarComponent::focusLost(FocusChangeType) { repaint(); }

std::unique_ptr<juce::AccessibilityHandler> ToolbarComponent::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::group, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{
            std::make_unique<synth::ui::ReadOnlyTextValue>([this] { return getFocusedButtonName(); })});
}
