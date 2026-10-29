// CardLayoutGroupHandle.cpp -- the title row that moves and renames a group, and the outline round the group.
#include "CardLayoutGroupHandle.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr float kCornerRadius = 7.0f;
constexpr float kHoverWash = 0.05f;
constexpr double kHoverMs = 100.0;
constexpr double kHoverReducedMs = 80.0;
constexpr float kGripWidth = 10.0f;

// Three short bars at the title row's right end: the mark of something to grab.
void paintGrip(juce::Graphics& g, juce::Rectangle<float> row) {
    const float x = row.getRight() - kGripWidth - 6.0f;
    const float top = row.getCentreY() - 3.5f;
    for (int i = 0; i < 3; ++i)
        g.fillRect(x, top + (float)i * 3.0f, kGripWidth, 1.0f);
}

} // namespace

CardLayoutGroupHandle::CardLayoutGroupHandle(juce::String sectionId, juce::String title)
    : juce::Button(sectionId)
    , sectionId_(std::move(sectionId)) {
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    setMouseCursor(dragGrabCursor());
    setGroupTitle(title);
}

void CardLayoutGroupHandle::setGroupTitle(const juce::String& title) {
    title_ = title;
    setTitle(title + " group");
    setTooltip("Drag the title to move the group (Alt+Up and Alt+Down move it, Return renames it). Double-click or "
               "right-click to rename");
}

void CardLayoutGroupHandle::setBlock(juce::Rectangle<int> block, int headerHeight) {
    headerHeight_ = headerHeight;
    setBounds(block);
}

void CardLayoutGroupHandle::setLifted(bool lifted) {
    if (lifted_ == lifted)
        return;
    lifted_ = lifted;
    repaint();
}

juce::Rectangle<int> CardLayoutGroupHandle::getTitleRowArea() const {
    return getLocalBounds().removeFromTop(headerHeight_);
}

juce::Rectangle<float> CardLayoutGroupHandle::getGroupArea() const { return getLocalBounds().toFloat().reduced(1.0f); }

// Only the title row takes the mouse: the controls' outlines inside the group stay reachable.
bool CardLayoutGroupHandle::hitTest(int x, int y) { return getTitleRowArea().contains(x, y); }

juce::PopupMenu CardLayoutGroupHandle::buildMenu() const {
    juce::PopupMenu menu;
    menu.addItem("Rename group", [this] {
        if (onRename)
            onRename();
    });
    return menu;
}

// A faint outline round the whole group while it is hovered, focused or lifted; nothing at rest, so the card is
// not covered in boxes.
void CardLayoutGroupHandle::paintButton(juce::Graphics& g, bool, bool) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto accent = theme.colors.accent;
    const auto area = getGroupArea();
    const bool focused = hasKeyboardFocus(false);
    const float solid = (focused || lifted_) ? 1.0f : hover_;
    if (solid <= 0.0f)
        return;
    g.setColour(accent.withAlpha(kHoverWash * solid));
    g.fillRoundedRectangle(area, kCornerRadius);
    g.setColour(accent.withAlpha(0.6f * solid));
    g.drawRoundedRectangle(area.reduced(0.5f), kCornerRadius, 1.0f);
    g.setColour(accent.withAlpha(solid));
    paintGrip(g, getTitleRowArea().toFloat());
    paintFocusRing(g, area, *this, kCornerRadius);
}

void CardLayoutGroupHandle::animateHover(bool over) {
    const float target = over ? 1.0f : 0.0f;
    if (!isShowing()) {
        hover_ = target;
        repaint();
        return;
    }
    const float from = hover_;
    hoverAnim_.start(updater_, prefersReducedMotion() ? kHoverReducedMs : kHoverMs, easeOutCubic,
                     [this, from, target](float t) {
                         hover_ = from + (target - from) * t;
                         repaint();
                     });
}

void CardLayoutGroupHandle::mouseEnter(const juce::MouseEvent& e) {
    juce::Button::mouseEnter(e);
    animateHover(true);
}

void CardLayoutGroupHandle::mouseExit(const juce::MouseEvent& e) {
    juce::Button::mouseExit(e);
    animateHover(false);
}

void CardLayoutGroupHandle::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    if (e.mods.isPopupMenu()) {
        buildMenu().showMenuAsync(
            juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(getTitleRowArea())));
        return;
    }
    if (e.mods.isLeftButtonDown() && onPress)
        onPress(e);
}

void CardLayoutGroupHandle::mouseDrag(const juce::MouseEvent& e) {
    if (onDrag)
        onDrag(e);
}

void CardLayoutGroupHandle::mouseUp(const juce::MouseEvent& e) {
    if (onRelease)
        onRelease(e);
}

void CardLayoutGroupHandle::mouseDoubleClick(const juce::MouseEvent& e) {
    if (!e.mods.isPopupMenu() && onRename)
        onRename();
}

// Return renames (the button's own click would be posted, not run, so the hook is called here).
bool CardLayoutGroupHandle::keyPressed(const juce::KeyPress& key) {
    if (onKey && onKey(key))
        return true;
    if (key != juce::KeyPress::returnKey)
        return false;
    if (onRename)
        onRename();
    return true;
}

} // namespace synth::ui
