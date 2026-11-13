#include "ToastComponent.h"

#include "TestMode/TestModeStateSource.h"
#include "UI/Layout/ActionGlyphs.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kPadX = 14;
constexpr int kGap = 12;
constexpr int kActionW = 56;
constexpr float kFontSize = 12.0f;
constexpr int kTextSlack = 2;
constexpr float kBarWidth = 4.0f;

// The screen reader hears the severity first; an info toast reads as the bare message.
juce::String severityName(ToastComponent::Severity severity) {
    switch (severity) {
    case ToastComponent::Severity::warning:
        return "Warning: ";
    case ToastComponent::Severity::error:
        return "Error: ";
    case ToastComponent::Severity::info:
        break;
    }
    return {};
}
} // namespace

// A hidden 1x1 child (id "toastState") so the toast's header need not include the test-mode state interface.
class ToastComponent::StateProbe
    : public juce::Component
    , public synth::testmode::StateSource {
public:
    explicit StateProbe(const ToastComponent& toast)
        : toast_(toast) {
        setComponentID("toastState");
        setInterceptsMouseClicks(false, false);
        setWantsKeyboardFocus(false);
        setAccessible(false);
    }
    void addTestModeState(juce::DynamicObject& into) const override {
        const auto severity = toast_.severity_;
        into.setProperty("severity", severity == Severity::error     ? "error"
                                     : severity == Severity::warning ? "warning"
                                                                     : "info");
        into.setProperty("message", toast_.message_);
    }

private:
    const ToastComponent& toast_;
};

ToastComponent::ToastComponent() {
    setVisible(false);
    setOpaque(false);
    setWantsKeyboardFocus(false);
    setTitle("Notification");
    addChildComponent(action_);
    stateProbe_ = std::make_unique<StateProbe>(*this);
    addChildComponent(*stateProbe_);
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
    swapAnim_.stop(updater_);
}

int ToastComponent::preferredWidth() const {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(juce::Font(juce::FontOptions(kFontSize)), message_, 0.0f, 0.0f);
    // Rounded up with a pixel of slack: drawText curtails a line that is even a fraction wider than its box, so a width
    // rounded down cut the end of the message to an ellipsis in a window with plenty of room.
    const int textW = (int)std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth()) + kTextSlack;
    return kPadX * 2 + ToastComponent::kIconSlot + textW +
           (action_.getButtonText().isNotEmpty() && hasLiveAction() ? kGap + kActionW : 0);
}

juce::Rectangle<int> ToastComponent::restingBounds() const {
    int w = juce::jmin(preferredWidth(), juce::jmax(160, area_.getWidth() - 40));
    if (swapping_ && !prefersReducedMotion())
        w = juce::roundToInt((float)widthFrom_ + (float)(w - widthFrom_) * swapMix_);
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
                          const juce::String& actionTooltip, Severity severity) {
    // A toast already on screen is replaced in place: what it showed fades into the new content, never swapped in
    // one frame.
    const bool replacing = shown_ && isVisible() && progress_ > 0.0f &&
                           FadeVisibility::canAnimateIn(getParentComponent()) &&
                           (message != message_ || severity != severity_);
    const Face before{message_, severity_};
    const bool hadAction = hasLiveAction();
    const int widthBefore = getWidth();
    endSwap();
    message_ = message;
    severity_ = severity;
    onAction_ = std::move(action);
    const bool hasAction = actionLabel.isNotEmpty() && onAction_ != nullptr;
    const bool actionLeaves = replacing && hadAction && !hasAction;
    if (!actionLeaves) {
        action_.setButtonText(actionLabel);
        action_.setTitle(actionLabel);
    }
    action_.setTooltip(actionTooltip);
    action_.setVisible(hasAction || actionLeaves);
    action_.setInterceptsMouseClicks(!actionLeaves, !actionLeaves);
    actionLeaving_ = actionLeaves;
    setTitle(severityName(severity_) + message);
    resized();
    if (auto* parent = getParentComponent())
        toFront(false), parent->repaint();
    if (replacing) {
        widthFrom_ = widthBefore;
        startSwap(before, actionLeaves, hasAction && !hadAction);
    }
    animateTo(true);
    startTimer(kAutoHideMs);
    repaint();
}

// The content that was on screen fades out while the new fades in, and the pill's width glides between the two (a
// fade alone under Reduce Motion). The geometry is final at once; only what is drawn trails it.
void ToastComponent::startSwap(const Face& leaving, bool actionLeaving, bool actionArriving) {
    leaving_ = leaving;
    swapping_ = true;
    swapMix_ = 0.0f;
    actionArriving_ = actionArriving;
    actionLeaving_ = actionLeaving;
    action_.setAlpha(actionArriving ? 0.0f : 1.0f);
    swapAnim_.start(
        updater_, motionMs(kSwapMs, kReducedMs), easeOutCubic,
        [this](float t) {
            swapMix_ = t;
            if (actionArriving_)
                action_.setAlpha(t);
            else if (actionLeaving_)
                action_.setAlpha(1.0f - t);
            applyProgress(progress_);
            repaint();
        },
        [this] { settleSwap(); });
}

void ToastComponent::endSwap() {
    swapAnim_.stop(updater_);
    settleSwap();
}

void ToastComponent::settleSwap() {
    const bool wasLeaving = actionLeaving_;
    swapping_ = false;
    swapMix_ = 1.0f;
    actionArriving_ = false;
    actionLeaving_ = false;
    action_.setAlpha(1.0f);
    action_.setInterceptsMouseClicks(true, true);
    if (wasLeaving) {
        action_.setVisible(false);
        action_.setButtonText({});
        action_.setTitle({});
    }
    if (isVisible())
        applyProgress(progress_);
    repaint();
}

void ToastComponent::finishMotionForTest() {
    swapAnim_.completeForTest();
    driver_.completeForTest();
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
    endSwap();
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

juce::Rectangle<int> ToastComponent::iconBounds() const {
    return {kPadX, (getHeight() - kIconSize) / 2, kIconSize, kIconSize};
}

// The text runs from after the icon to just before the action button, or to the right padding without one.
juce::Rectangle<int> ToastComponent::textBounds() const {
    const int left = kPadX + kIconSlot;
    const int right = action_.isVisible() ? action_.getX() - 4 : getWidth() - kPadX;
    return {left, 0, juce::jmax(0, right - left), getHeight()};
}

void ToastComponent::paint(juce::Graphics& g) {
    const auto& c = synth::theme::themeOf(*this).colors;
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(c.surfaceHi);
    g.fillRoundedRectangle(bounds, 8.0f);
    // A warning or an error is told by its theme colour on the border and a bar down the left edge, so it does not
    // rest on the text alone. The border changes colour through a swap; the rest of a face fades.
    auto borderOf = [&](Severity severity) {
        return severity == Severity::info ? c.border : severity == Severity::error ? c.error : c.warning;
    };
    g.setColour(swapping_ ? borderOf(leaving_.severity).interpolatedWith(borderOf(severity_), swapMix_)
                          : borderOf(severity_));
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
    if (swapping_)
        paintFace(g, leaving_, 1.0f - swapMix_);
    paintFace(g, Face{message_, severity_}, swapping_ ? swapMix_ : 1.0f);
}

void ToastComponent::paintFace(juce::Graphics& g, const Face& face, float alpha) const {
    if (alpha <= 0.0f)
        return;
    const auto& c = synth::theme::themeOf(*this).colors;
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const bool flagged = face.severity != Severity::info;
    const auto flag = face.severity == Severity::error ? c.error : c.warning;
    if (flagged) {
        juce::Graphics::ScopedSaveState state(g);
        juce::Path pill;
        pill.addRoundedRectangle(bounds, 8.0f);
        g.reduceClipRegion(pill);
        g.setColour(flag.withMultipliedAlpha(alpha));
        g.fillRect(bounds.withWidth(kBarWidth));
    }
    const auto accent = face.severity == Severity::info ? c.accent : flag;
    const auto glyph = face.severity == Severity::error     ? SeverityGlyph::error
                       : face.severity == Severity::warning ? SeverityGlyph::warning
                                                            : SeverityGlyph::info;
    paintSeverityGlyph(g, iconBounds().toFloat(), accent.withMultipliedAlpha(alpha), glyph);
    g.setColour(c.textPrimary.withMultipliedAlpha(alpha));
    g.setFont(juce::Font(juce::FontOptions(kFontSize)));
    g.drawText(face.message, textBounds(), juce::Justification::centredLeft, true);
}

} // namespace synth::ui
