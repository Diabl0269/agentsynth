#include "ToastComponent.h"

#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kPadX = 14;
constexpr int kGap = 12;
constexpr int kActionW = 56;
constexpr float kFontSize = 12.0f;
} // namespace

ToastComponent::ToastComponent() {
    setVisible(false);
    setOpaque(false);
    setWantsKeyboardFocus(false);
    setTitle("Notification");
    addChildComponent(action_);
    action_.setWantsKeyboardFocus(true);
    action_.setMouseClickGrabsKeyboardFocus(false);
    action_.onClick = [this] {
        auto action = std::move(onAction_);
        onAction_ = nullptr;
        dismiss();
        if (action)
            action();
    };
}

ToastComponent::~ToastComponent() {
    stopTimer();
    driver_.stop(updater_);
}

int ToastComponent::preferredWidth() const {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(juce::Font(juce::FontOptions(kFontSize)), message_, 0.0f, 0.0f);
    const int textW = juce::roundToInt(glyphs.getBoundingBox(0, -1, true).getWidth());
    return kPadX * 2 + textW + (action_.getButtonText().isNotEmpty() && action_.isVisible() ? kGap + kActionW : 0);
}

juce::Rectangle<int> ToastComponent::restingBounds() const {
    const int w = juce::jmin(preferredWidth(), juce::jmax(160, area_.getWidth() - 40));
    return {area_.getCentreX() - w / 2, area_.getBottom() - kBottomMargin - kHeight, w, kHeight};
}

void ToastComponent::placeIn(juce::Rectangle<int> area) {
    area_ = area;
    applyProgress(progress_);
}

// The slide is an offset from the resting spot, so a layout pass mid-animation lands where the animation is going.
void ToastComponent::applyProgress(float progress) {
    progress_ = progress;
    auto bounds = restingBounds();
    if (!prefersReducedMotion())
        bounds.translate(0, juce::roundToInt((1.0f - progress) * (float)kSlidePx));
    setBounds(bounds);
    setAlpha(progress);
}

void ToastComponent::animateTo(bool shown) {
    const float from = progress_;
    const float to = shown ? 1.0f : 0.0f;
    shown_ = shown;
    if (!FadeVisibility::canAnimateIn(getParentComponent())) {
        driver_.stop(updater_);
        applyProgress(to);
        setVisible(shown);
        return;
    }
    setVisible(true);
    applyProgress(from); // frame 0 is already the first animation frame
    const double ms = motionMs(shown ? kEnterMs : kExitMs, kReducedMs);
    driver_.start(
        updater_, ms, shown ? std::function<float(float)>(easeOutCubic) : std::function<float(float)>(easeInCubic),
        [this, from, to](float t) { applyProgress(from + (to - from) * t); },
        [this, shown] {
            if (!shown)
                setVisible(false);
        });
}

void ToastComponent::show(const juce::String& message, const juce::String& actionLabel, std::function<void()> action,
                          const juce::String& actionTooltip) {
    message_ = message;
    onAction_ = std::move(action);
    const bool hasAction = actionLabel.isNotEmpty() && onAction_ != nullptr;
    action_.setButtonText(actionLabel);
    action_.setTitle(actionLabel);
    action_.setTooltip(actionTooltip);
    action_.setVisible(hasAction);
    setTitle(message);
    resized();
    if (auto* parent = getParentComponent())
        toFront(false), parent->repaint();
    animateTo(true);
    startTimer(kAutoHideMs);
    repaint();
}

void ToastComponent::dismiss() {
    stopTimer();
    if (!shown_)
        return;
    if (action_.hasKeyboardFocus(false))
        action_.giveAwayKeyboardFocus();
    animateTo(false);
}

void ToastComponent::dismissNow() {
    stopTimer();
    shown_ = false;
    driver_.stop(updater_);
    applyProgress(0.0f);
    setVisible(false);
}

void ToastComponent::timerCallback() {
    // Reading it takes time: it stays while the pointer rests on it or its action holds the keyboard.
    if (isMouseOver(true) || action_.hasKeyboardFocus(false)) {
        startTimer(kAutoHideMs);
        return;
    }
    dismiss();
}

bool ToastComponent::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey && shown_) {
        dismiss();
        return true;
    }
    return false;
}

void ToastComponent::resized() {
    action_.setBounds(getWidth() - kPadX - kActionW + 6, 4, kActionW, getHeight() - 8);
    if (shown_ || isVisible())
        applyProgress(progress_);
}

void ToastComponent::paint(juce::Graphics& g) {
    const auto& c = synth::theme::themeOf(*this).colors;
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(c.surfaceHi);
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(c.border);
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
    g.setColour(c.textPrimary);
    g.setFont(juce::Font(juce::FontOptions(kFontSize)));
    const int right = action_.isVisible() ? action_.getX() - 4 : getWidth() - kPadX;
    g.drawText(message_, kPadX, 0, juce::jmax(0, right - kPadX), getHeight(), juce::Justification::centredLeft, true);
}

} // namespace synth::ui
