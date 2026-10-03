#include "UI/Layout/AppTooltipWindow.h"

#include "UI/Layout/PopupMotion.h"
#include "UI/Layout/ReducedMotion.h"

namespace synth::ui {

namespace pm = popup_motion;

// The leaving tooltip: juce has already hidden the real one, so a click-through copy fades out in its place.
class AppTooltipWindow::Ghost final : public juce::Component {
public:
    Ghost(juce::String text, juce::Rectangle<int> bounds, float startAlpha)
        : text_(std::move(text))
        , startAlpha_(startAlpha)
        , updater_(this) {
        setInterceptsMouseClicks(false, false);
        setAccessible(false);
        setOpaque(false);
        setAlwaysOnTop(true);
        setBounds(bounds);
        setAlpha(startAlpha_);
    }

    ~Ghost() override { driver_.stop(updater_); }

    /** Adds the ghost beside the window and starts it leaving. */
    void leave(juce::Component& parent) {
        parent.addAndMakeVisible(this);
        toFront(false);
        driver_.start(
            updater_, pm::durationMs(pm::Phase::Out, prefersReducedMotion()),
            [](float t) { return pm::ease(pm::Phase::Out, t); },
            [this](float e) { setAlpha(startAlpha_ * pm::frameAt(pm::Phase::Out, e, {0, 0}, true).alpha); },
            [this] { finish(); });
        leaving_ = true;
    }

    /** Takes the ghost off screen at once (a new tip is arriving, or the window is going). */
    void stop() {
        driver_.stop(updater_);
        finish();
    }

    bool isLeaving() const noexcept { return leaving_; }

    void paint(juce::Graphics& g) override { getLookAndFeel().drawTooltip(g, text_, getWidth(), getHeight()); }

private:
    // Never stops the driver: the animator calls this from inside its own completion callback.
    void finish() {
        leaving_ = false;
        setVisible(false);
        if (auto* parent = getParentComponent())
            parent->removeChildComponent(this);
    }

    juce::String text_;
    float startAlpha_;
    bool leaving_ = false;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver driver_;
};

AppTooltipWindow::AppTooltipWindow(juce::Component* parent, juce::ApplicationProperties* appProperties,
                                   int millisecondsBeforeTipAppears)
    : juce::TooltipWindow(parent, millisecondsBeforeTipAppears)
    , appProperties_(appProperties)
    , updater_(this) {
    setOpaque(false); // it fades, and its corners are rounded: nothing behind it may be assumed
}

AppTooltipWindow::~AppTooltipWindow() {
    destroying_ = true; // the base class hides the tip as it goes: no ghost for that
    driver_.stop(updater_);
    if (ghost_ != nullptr)
        ghost_->stop();
}

bool AppTooltipWindow::areInfoTooltipsOn() const {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return true;
    return appProperties_->getUserSettings()->getBoolValue(kShowInfoTooltipsKey, true);
}

bool AppTooltipWindow::suppresses(const juce::Component& c) const {
    return !isHelperTooltip(c) && !areInfoTooltipsOn();
}

juce::String AppTooltipWindow::getTipFor(juce::Component& c) {
    auto tip = juce::TooltipWindow::getTipFor(c);
    if (tip.isEmpty())
        return tip;
    if (suppresses(c))
        return {};
    lastTip_ = tip;
    return tip;
}

bool AppTooltipWindow::isFading() const noexcept { return fadingIn_ || hasLeavingGhostForTest(); }

bool AppTooltipWindow::hasLeavingGhostForTest() const noexcept { return ghost_ != nullptr && ghost_->isLeaving(); }

bool AppTooltipWindow::shouldAnimate() const { return PopupMotion::isEnabled() && (animateOffScreen_ || isShowing()); }

void AppTooltipWindow::visibilityChanged() {
    if (destroying_)
        return;
    if (isVisible())
        beginFadeIn();
    else
        beginFadeOut();
}

void AppTooltipWindow::applyFadeInFrameForTest(float eased) {
    setAlpha(pm::frameAt(pm::Phase::In, juce::jlimit(0.0f, 1.0f, eased), {0, 0}, true).alpha);
}

void AppTooltipWindow::beginFadeIn() {
    if (ghost_ != nullptr)
        ghost_->stop(); // the previous tip is still leaving: this one replaces it
    driver_.stop(updater_);
    fadingIn_ = false;
    if (!shouldAnimate()) {
        setAlpha(1.0f);
        return;
    }
    fadingIn_ = true;
    setAlpha(0.0f); // frame 0 before the first VBlank can show the tip whole
    driver_.start(
        updater_, pm::durationMs(pm::Phase::In, prefersReducedMotion()),
        [](float t) { return pm::ease(pm::Phase::In, t); },
        [this](float e) { setAlpha(pm::frameAt(pm::Phase::In, e, {0, 0}, true).alpha); },
        [this] {
            setAlpha(1.0f);
            fadingIn_ = false;
        });
}

void AppTooltipWindow::beginFadeOut() {
    const float startAlpha = getAlpha();
    driver_.stop(updater_);
    fadingIn_ = false;
    setAlpha(1.0f); // hidden now; the next showing starts from its own frame 0
    auto* parent = getParentComponent();
    if (parent == nullptr || lastTip_.isEmpty() || startAlpha <= 0.0f || !shouldAnimate())
        return;
    ghost_ = std::make_unique<Ghost>(lastTip_, getBounds(), startAlpha);
    ghost_->leave(*parent);
}

} // namespace synth::ui
