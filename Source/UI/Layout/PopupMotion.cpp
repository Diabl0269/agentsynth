#include "UI/Layout/PopupMotion.h"

#include "UI/Layout/ReducedMotion.h"

namespace synth::ui {

#if JUCE_MAC
// PopupMotionMac.mm: a picture of a whole native window, title bar included. `topInset` is how
// many points the picture reaches above the content area.
juce::Image captureNativeWindowImage(void* nativeView, int& topInset);
#endif

namespace {

using popup_motion::Phase;

constexpr const char* kAttachedProperty = "synthPopupMotion";
constexpr int kWatchdogSlackMs = 250;

bool& enabledFlag() {
    static bool enabled = true;
    return enabled;
}

int roundToInt(float v) { return (int)std::lround(v); }

// ----------------------------------------------------------------------------------------------
// Ghost: JUCE hides (or deletes) a popup window the instant it is dismissed, so the leaving
// animation runs on a picture of it in a click-through window of its own.
// ----------------------------------------------------------------------------------------------
class Ghost final
    : public juce::Component
    , private juce::Timer {
public:
    Ghost(juce::Image picture, juce::Rectangle<int> screenBounds, int styleFlags, juce::Point<int> dir,
          float startAlpha, bool reduceMotion)
        : picture_(std::move(picture))
        , restPos_(screenBounds.getPosition())
        , dir_(dir)
        , startAlpha_(startAlpha)
        , reduce_(reduceMotion)
        , updater_(this) {
        setOpaque(false);
        setInterceptsMouseClicks(false, false);
        setAccessible(false);
        setBounds(screenBounds);
        addToDesktop(juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresKeyPresses |
                     juce::ComponentPeer::windowIgnoresMouseClicks |
                     (styleFlags & juce::ComponentPeer::windowHasDropShadow));
        setAlpha(startAlpha_);
        setVisible(true);

        driver_.start(
            updater_, popup_motion::durationMs(Phase::Out, reduce_),
            [](float t) { return popup_motion::ease(Phase::Out, t); }, [this](float e) { applyFrame(e); },
            [this] { finish(); });
        startTimer((int)popup_motion::durationMs(Phase::Out, reduce_) + kWatchdogSlackMs);
    }

    void paint(juce::Graphics& g) override {
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(picture_, getLocalBounds().toFloat());
    }

private:
    void applyFrame(float eased) {
        const auto f = popup_motion::frameAt(Phase::Out, eased, dir_, reduce_);
        setAlpha(startAlpha_ * f.alpha);
        setTopLeftPosition(restPos_ + juce::Point<int>(roundToInt(f.offset.x), roundToInt(f.offset.y)));
    }

    void finish() {
        if (finished_)
            return;
        finished_ = true;
        stopTimer();
        setVisible(false);
        // Never delete from inside the animator's own callback.
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<Component>(this)] { delete safe.getComponent(); });
    }

    void timerCallback() override { finish(); }

    juce::Image picture_;
    juce::Point<int> restPos_;
    juce::Point<int> dir_;
    float startAlpha_;
    bool reduce_;
    bool finished_ = false;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver driver_;
};

// ----------------------------------------------------------------------------------------------
// Impl: one per attached window; deletes itself with the window.
// ----------------------------------------------------------------------------------------------
class Impl final
    : public juce::ComponentListener
    , private juce::Timer {
public:
    explicit Impl(juce::Component& window)
        : window_(&window)
        , updater_(&window)
        , snapshotTask_(*this) {
        window.addComponentListener(this);
        if (window.isVisible())
            startIn();
    }

    ~Impl() override {
        snapshotTask_.cancelPendingUpdate();
        if (window_ != nullptr)
            window_->removeComponentListener(this);
    }

    void componentVisibilityChanged(juce::Component&) override {
        if (window_->isVisible())
            startIn();
        else
            onHidden();
    }

    void componentBeingDeleted(juce::Component&) override {
        // Dismissed menus, alerts and dialogs are usually deleted while still flagged visible, with
        // nothing left to draw: the picture taken while they were open is all there is.
        if (window_->isVisible() && !ghosted_ && PopupMotion::isEnabled())
            startOut(/*fresh=*/false);
        window_ = nullptr;
        delete this;
    }

private:
    struct SnapshotTask final : juce::AsyncUpdater {
        explicit SnapshotTask(Impl& o)
            : owner(o) {}
        void handleAsyncUpdate() override { owner.capture(); }
        Impl& owner;
    };

    bool canAnimate() const { return PopupMotion::isEnabled() && window_->isOnDesktop(); }

    bool hasNativeTitleBar() const {
        auto* peer = window_->getPeer();
        return peer != nullptr && (peer->getStyleFlags() & juce::ComponentPeer::windowHasTitleBar) != 0;
    }

    void applyFrame(Phase phase, float eased) {
        const auto f = popup_motion::frameAt(phase, eased, dir_, reduce_);
        window_->setAlpha(f.alpha);
        window_->setTopLeftPosition(restPos_ + juce::Point<int>(roundToInt(f.offset.x), roundToInt(f.offset.y)));
    }

    void startIn() {
        if (!canAnimate())
            return;
        if (animating_) {
            driver_.stop(updater_);
            stopTimer();
        } else {
            restPos_ = window_->getPosition();
        }
        reduce_ = prefersReducedMotion();
        dir_ = popup_motion::slideDirection(window_->getScreenBounds(), juce::Desktop::getMousePosition());
        ghosted_ = false;
        cache_ = {};
        animating_ = true;

        applyFrame(Phase::In, 0.0f); // frame 0 before the first VBlank can show the window whole
        driver_.start(
            updater_, popup_motion::durationMs(Phase::In, reduce_),
            [](float t) { return popup_motion::ease(Phase::In, t); },
            [this](float e) {
                if (animating_)
                    applyFrame(Phase::In, e);
            },
            [this] { finishIn(); });
        // If no VBlank ever arrives the window must not stay invisible.
        startTimer((int)popup_motion::durationMs(Phase::In, reduce_) + kWatchdogSlackMs);

        if (!hasNativeTitleBar())
            snapshotTask_.triggerAsyncUpdate(); // a picture to leave with, even if dismissed at once
    }

    void finishIn() {
        stopTimer();
        if (!animating_)
            return;
        animating_ = false;
        window_->setAlpha(1.0f);
        window_->setTopLeftPosition(restPos_);
        if (cache_.isNull())
            capture();
    }

    void timerCallback() override { finishIn(); }

    void onHidden() {
        if (!canAnimate()) {
            animating_ = false;
            return;
        }
        startOut(/*fresh=*/true);
    }

    // The window's bounds at its resting place, whatever slide frame it is on right now.
    juce::Rectangle<int> restScreenBounds() const {
        return window_->getScreenBounds().translated(restPos_.x - window_->getX(), restPos_.y - window_->getY());
    }

    // Takes the picture the leaving ghost shows.
    void capture() {
        if (window_ != nullptr && window_->isVisible() && window_->isOnDesktop())
            takePicture();
    }

    void takePicture() {
        const auto bounds = restScreenBounds();
#if JUCE_MAC
        if (hasNativeTitleBar()) {
            int topInset = 0;
            auto image = captureNativeWindowImage(window_->getPeer()->getNativeHandle(), topInset);
            if (image.isValid()) {
                cache_ = std::move(image);
                cacheBounds_ = bounds.withTrimmedTop(-topInset);
            }
            return;
        }
#endif
        if (hasNativeTitleBar())
            return; // no way to picture the title bar on this platform: leave without a ghost
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(bounds);
        const float scale = display != nullptr ? (float)display->scale : 1.0f;
        cache_ = window_->createComponentSnapshot(window_->getLocalBounds(), true, scale);
        cacheBounds_ = bounds;
    }

    // `fresh`: the window was just hidden and is still intact. Otherwise it is being deleted and
    // must not be touched, only pictured from what was cached while it was open.
    void startOut(bool fresh) {
        if (!PopupMotion::isEnabled() || ghosted_)
            return;
        const float startAlpha = animating_ ? window_->getAlpha() : 1.0f;
        const auto slideOffset = window_->getPosition() - restPos_;
        if (animating_) {
            driver_.stop(updater_);
            animating_ = false;
            stopTimer();
        }
        if (fresh) {
            window_->setAlpha(1.0f);
            window_->setTopLeftPosition(restPos_);
            // A native-title window can only be pictured while it is shown, so it leaves with the
            // picture taken then; a plain one is pictured again now, as it is.
            if (window_->isOnDesktop() && (cache_.isNull() || !hasNativeTitleBar()))
                takePicture();
        }
        if (cache_.isNull())
            return;
        ghosted_ = true;
        const int flags = window_->getPeer() != nullptr ? window_->getPeer()->getStyleFlags() : 0;
        new Ghost(cache_, cacheBounds_.translated(slideOffset.x, slideOffset.y), flags, dir_, startAlpha,
                  prefersReducedMotion()); // self-deleting
    }

    juce::Component* window_;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver driver_;
    SnapshotTask snapshotTask_;
    juce::Point<int> dir_{0, 1};
    juce::Point<int> restPos_;
    bool reduce_ = false;
    bool animating_ = false;
    bool ghosted_ = false;
    juce::Image cache_;
    juce::Rectangle<int> cacheBounds_;
};

} // namespace

void PopupMotion::attach(juce::Component& window) {
    if (window.getProperties().contains(kAttachedProperty))
        return;
    window.getProperties().set(kAttachedProperty, true);
    new Impl(window); // owns itself: deleted by componentBeingDeleted
}

void PopupMotion::setEnabled(bool enabled) { enabledFlag() = enabled; }

bool PopupMotion::isEnabled() { return enabledFlag(); }

} // namespace synth::ui
