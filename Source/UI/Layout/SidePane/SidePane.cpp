// Concern: SidePane's open/close tween, width clamp and drag, and persistence.
#include "SidePane.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

namespace {
const synth::theme::Theme* themeOf(const juce::Component& c) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&c.getLookAndFeel());
    return laf != nullptr ? &laf->getTheme() : nullptr;
}
} // namespace

SidePane::GrabEdge::GrabEdge(SidePane& owner)
    : owner_(owner) {
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    setWantsKeyboardFocus(false);
}

void SidePane::GrabEdge::paint(juce::Graphics& g) {
    if (!hovered_ && !isMouseButtonDown())
        return;
    const auto* theme = themeOf(*this);
    g.setColour(theme != nullptr ? theme->colors.accent : juce::Colour(0xff00D1FF));
    g.fillRect(getWidth() - 2, 0, 2, getHeight());
}

void SidePane::GrabEdge::mouseEnter(const juce::MouseEvent&) {
    hovered_ = true;
    repaint();
}

void SidePane::GrabEdge::mouseExit(const juce::MouseEvent&) {
    hovered_ = false;
    repaint();
}

// The pointer is measured in the pane's own coordinates: its left edge never moves while the width
// changes, so the reading stays stable as the owner re-lays the pane out under the drag.
void SidePane::GrabEdge::mouseDown(const juce::MouseEvent& e) {
    owner_.pressWidth_ = owner_.width_;
    owner_.pressX_ = e.getEventRelativeTo(&owner_).position.x;
    owner_.widthDragged_ = false;
    repaint();
}

void SidePane::GrabEdge::mouseDrag(const juce::MouseEvent& e) {
    const float dx = e.getEventRelativeTo(&owner_).position.x - owner_.pressX_;
    const int before = owner_.width_;
    owner_.setContentWidth(owner_.pressWidth_ + static_cast<int>(std::lround(dx)));
    owner_.widthDragged_ = owner_.widthDragged_ || owner_.width_ != before;
}

void SidePane::GrabEdge::mouseUp(const juce::MouseEvent&) {
    if (owner_.widthDragged_)
        owner_.persist();
    owner_.widthDragged_ = false;
    repaint();
}

SidePane::SidePane() {
    setWantsKeyboardFocus(false);
    addAndMakeVisible(edge_);
    setVisible(false);
}

SidePane::~SidePane() = default;

void SidePane::setContent(SidePaneContent* content) {
    if (content_ != nullptr)
        removeChildComponent(&content_->getPaneComponent());
    content_ = content;
    if (content_ != nullptr) {
        auto& component = content_->getPaneComponent();
        addAndMakeVisible(component);
        component.setTitle(content_->getPaneTitle());
        edge_.toFront(false);
    }
    setVisible(content_ != nullptr && (fraction_ > 0.0f || animating_));
    resized();
    if (onOccupiedWidthChanged)
        onOccupiedWidthChanged();
    sendSynchronousChangeMessage();
}

void SidePane::setPersistence(juce::PropertiesFile* settings, const juce::String& tabKey, bool defaultOpen) {
    settings_ = settings;
    tabKey_ = tabKey;
    if (settings_ == nullptr)
        return;
    open_ = settings_->getBoolValue(openKeyFor(tabKey_), defaultOpen);
    width_ = juce::jlimit(kMinWidth, kMaxWidth, settings_->getIntValue(widthKeyFor(tabKey_), kDefaultWidth));
    driver_.stop(updater_);
    animating_ = false;
    applyFraction(open_ ? 1.0f : 0.0f);
    sendSynchronousChangeMessage();
}

void SidePane::persist() {
    if (settings_ == nullptr)
        return;
    settings_->setValue(openKeyFor(tabKey_), open_);
    settings_->setValue(widthKeyFor(tabKey_), width_);
    settings_->saveIfNeeded();
}

int SidePane::getOccupiedWidth() const noexcept {
    if (content_ == nullptr)
        return 0;
    return static_cast<int>(std::lround(static_cast<float>(width_) * fraction_));
}

// Both directions retarget from the CURRENT fraction and take a share of the nominal duration equal to
// the distance left, so a re-toggle mid-tween reverses smoothly. A pane whose owner is not on screen gets
// no frames, so it lands at once. The owner is asked rather than the pane itself: a closed pane is hidden,
// and it must be made visible again before the opening tween starts or its first frame never paints.
void SidePane::setOpen(bool open) {
    if (open_ == open)
        return;
    open_ = open;
    persist();
    animateTo(open_ ? 1.0f : 0.0f);
    sendSynchronousChangeMessage();
}

void SidePane::animateTo(float target) {
    const float from = fraction_;
    const auto* owner = getParentComponent();
    const bool onScreen = owner != nullptr && owner->isShowing();
    if (!onScreen || std::abs(target - from) < 0.001f) {
        driver_.stop(updater_);
        animating_ = false;
        applyFraction(target);
        return;
    }
    const bool opening = target > from;
    animating_ = true;
    setVisible(content_ != nullptr);
    driver_.start(
        updater_, tweenDurationMs(opening, std::abs(target - from)), opening ? easeOutCubic : easeInCubic,
        [this, from, target](float t) { applyFraction(from + (target - from) * t); },
        [this, target] {
            animating_ = false;
            applyFraction(target);
        });
}

double SidePane::tweenDurationMs(bool opening, float distance) noexcept {
    return juce::jmax(1.0, (opening ? kOpenMs : kCloseMs) * static_cast<double>(distance));
}

void SidePane::applyFraction(float fraction) {
    const int before = getOccupiedWidth();
    fraction_ = juce::jlimit(0.0f, 1.0f, fraction);
    // Kept visible for the whole tween, even at its first frame's fraction of 0: a hidden pane loses its
    // VBlank callbacks, and the tween would stall there.
    setVisible(content_ != nullptr && (fraction_ > 0.0f || animating_));
    if (getOccupiedWidth() != before && onOccupiedWidthChanged)
        onOccupiedWidthChanged();
}

void SidePane::setContentWidth(int width) {
    const int clamped = juce::jlimit(kMinWidth, kMaxWidth, width);
    if (clamped == width_)
        return;
    width_ = clamped;
    resized();
    if (onOccupiedWidthChanged)
        onOccupiedWidthChanged();
}

// The content keeps its full width while the pane's own width tweens, so opening reveals it rather
// than squeezing and re-flowing it every frame.
void SidePane::resized() {
    if (content_ != nullptr)
        content_->getPaneComponent().setBounds(0, 0, juce::jmax(0, width_ - 1), getHeight());
    edge_.setBounds(juce::jmax(0, getWidth() - kGrabWidth), 0, kGrabWidth, getHeight());
}

void SidePane::paint(juce::Graphics& g) {
    const auto* theme = themeOf(*this);
    g.fillAll(theme != nullptr ? theme->colors.bg0 : juce::Colour(0xff0B0D10));
    g.setColour(theme != nullptr ? theme->colors.border : juce::Colour(0xff2A2F38));
    g.fillRect(getWidth() - 1, 0, 1, getHeight());
}

} // namespace synth::ui
