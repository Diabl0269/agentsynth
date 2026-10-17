// MainViewSwapMotion.cpp -- the leaving and arriving pictures of a main-view swap, the two opaque layers that draw
// them and the 240 ms tween (80 ms fade under Reduce Motion) that moves them.
#include "UI/Layout/MainViewSwapMotion.h"

#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <utility>

namespace synth::ui {

namespace {
juce::Rectangle<float> lerpRect(juce::Rectangle<int> from, juce::Rectangle<int> to, float t) {
    const auto a = from.toFloat();
    const auto b = to.toFloat();
    return {a.getX() + (b.getX() - a.getX()) * t, a.getY() + (b.getY() - a.getY()) * t,
            a.getWidth() + (b.getWidth() - a.getWidth()) * t, a.getHeight() + (b.getHeight() - a.getHeight()) * t};
}
} // namespace

// A layer paints nothing itself: both draw the same pair of pictures, each clipped to its own area, so a picture that
// crosses from the main area to the panel body is cut by the tab strip between them.
MainViewSwapMotion::Layer::Layer(MainViewSwapMotion& owner)
    : owner_(owner) {
    setInterceptsMouseClicks(false, false);
    setWantsKeyboardFocus(false);
    setAccessible(false);
    setVisible(false);
}

void MainViewSwapMotion::Layer::paint(juce::Graphics& g) { owner_.paintLayer(g, *this); }

MainViewSwapMotion::MainViewSwapMotion(juce::Component& host)
    : host_(host) {
    host_.addChildComponent(mainLayer_);
    host_.addChildComponent(bodyLayer_);
}

juce::Image MainViewSwapMotion::pictureOf(juce::Rectangle<int> area) const {
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const float scale = displays.getPrimaryDisplay() != nullptr
                            ? std::clamp(static_cast<float>(displays.getPrimaryDisplay()->scale), 1.0f, 2.0f)
                            : 1.0f;
    return host_.createComponentSnapshot(area, true, scale);
}

// Nothing is pictured unless the motion would play: off screen no VBlank frame ever arrives, and under Animations Off
// the swap lands at once (FadeVisibility::canAnimateIn answers both, and the test override).
bool MainViewSwapMotion::begin(juce::Rectangle<int> mainArea) {
    finishNow();
    discardPending();
    if (mainArea.isEmpty() || !FadeVisibility::canAnimateIn(&host_))
        return false;
    mainArea_ = mainArea;
    leaving_ = pictureOf(mainArea);
    return leaving_.isValid();
}

void MainViewSwapMotion::discardPending() noexcept {
    leaving_ = {};
    arriving_ = {};
}

// The real components are already in their final places; the layers go over them and come off on the last frame. The
// move needs the arriving view's picture (taken now, laid out in the main area) and an opaque layer over each place;
// the fade needs neither, the real arriving view showing through the leaving picture as it fades.
bool MainViewSwapMotion::play(juce::Rectangle<int> panelBody) {
    if (!leaving_.isValid() || !FadeVisibility::canAnimateIn(&host_))
        return false;
    fade_ = prefersReducedMotion();
    if (!fade_ && panelBody.isEmpty()) {
        discardPending();
        return false;
    }
    panelBody_ = panelBody;
    arriving_ = fade_ ? juce::Image{} : pictureOf(mainArea_);
    progress_ = 0.0f;
    running_ = true;

    mainLayer_.setOpaque(!fade_);
    mainLayer_.setBounds(mainArea_);
    bodyLayer_.setOpaque(true);
    bodyLayer_.setBounds(panelBody_);
    mainLayer_.setVisible(true);
    bodyLayer_.setVisible(!fade_);
    mainLayer_.toFront(false);
    bodyLayer_.toFront(false);
    if (onRunningChanged)
        onRunningChanged(true);

    driver_.start(
        updater_, durationMs(),
        fade_ ? std::function<float(float)>([](float t) { return t; }) : std::function<float(float)>(easeOutCubic),
        [this](float t) { setProgress(t); }, [this] { end(false); });
    return true;
}

void MainViewSwapMotion::setProgress(float t) {
    progress_ = std::clamp(t, 0.0f, 1.0f);
    mainLayer_.repaint();
    if (!fade_)
        bodyLayer_.repaint();
}

// The completion callback runs inside the animator, which must not be stopped from there: only a stop from outside
// (a second swap, a layout pass) takes the driver down.
void MainViewSwapMotion::end(bool stopDriver) {
    if (stopDriver)
        driver_.stop(updater_);
    const bool wasRunning = std::exchange(running_, false);
    mainLayer_.setVisible(false);
    bodyLayer_.setVisible(false);
    discardPending();
    progress_ = 1.0f;
    if (wasRunning && onRunningChanged)
        onRunningChanged(false);
}

void MainViewSwapMotion::finishNow() {
    if (running_)
        end(true);
}

void MainViewSwapMotion::stepForTest(float t) {
    if (!running_)
        return;
    driver_.stop(updater_);
    setProgress(t);
    if (t >= 1.0f)
        end(true);
}

juce::Rectangle<float> MainViewSwapMotion::leavingRect() const noexcept {
    return fade_ ? mainArea_.toFloat() : lerpRect(mainArea_, panelBody_, progress_);
}

juce::Rectangle<float> MainViewSwapMotion::arrivingRect() const noexcept {
    return fade_ ? juce::Rectangle<float>{} : lerpRect(panelBody_, mainArea_, progress_);
}

void MainViewSwapMotion::paintLayer(juce::Graphics& g, const Layer& layer) const {
    if (!running_)
        return;
    g.addTransform(juce::AffineTransform::translation((float)-layer.getX(), (float)-layer.getY()));
    g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    if (fade_) {
        g.setOpacity(1.0f - progress_);
        g.drawImage(leaving_, leavingRect());
        return;
    }
    g.setColour(synth::theme::themeOf(layer).colors.bg0);
    g.fillRect(layer.getBounds());
    g.setOpacity(1.0f);
    g.drawImage(leaving_, leavingRect());
    g.drawImage(arriving_, arrivingRect());
}

} // namespace synth::ui
