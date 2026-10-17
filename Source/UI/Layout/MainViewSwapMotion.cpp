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

// A picture of `size` fitted into `box` at its own aspect, centred.
juce::Rectangle<float> fitted(juce::Rectangle<float> box, juce::Rectangle<int> size) {
    if (size.isEmpty() || box.isEmpty())
        return box;
    const float scale = std::min(box.getWidth() / (float)size.getWidth(), box.getHeight() / (float)size.getHeight());
    return juce::Rectangle<float>(0.0f, 0.0f, size.getWidth() * scale, size.getHeight() * scale)
        .withCentre(box.getCentre());
}

float ramp(float t, float from, float to) { return std::clamp((t - from) / (to - from), 0.0f, 1.0f); }
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
bool MainViewSwapMotion::begin(juce::Rectangle<int> mainArea, juce::Rectangle<int> panelBody) {
    finishNow();
    discardPending();
    if (mainArea.isEmpty() || !FadeVisibility::canAnimateIn(&host_))
        return false;
    mainArea_ = mainArea;
    leaving_ = pictureOf(mainArea);
    oldPanelBody_ = panelBody;
    departingBody_ = panelBody.isEmpty() ? juce::Image{} : pictureOf(panelBody);
    return leaving_.isValid();
}

void MainViewSwapMotion::discardPending() noexcept {
    leaving_ = {};
    arriving_ = {};
    departingBody_ = {};
    settledBody_ = {};
}

// The real components are already in their final places; the layers go over them and come off on the last frame. The
// move needs the arriving view's picture (taken now, laid out in the main area), the finished panel body's picture and
// an opaque layer over each place;
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
    settledBody_ = fade_ ? juce::Image{} : pictureOf(panelBody_);
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

// Eased progress is the clock for both crossfades: the old body gives way early, the finished one arrives late.
float MainViewSwapMotion::settledBodyAlpha() const noexcept { return fade_ ? 0.0f : ramp(progress_, 0.5f, 1.0f); }

float MainViewSwapMotion::departingBodyAlpha() const noexcept {
    return fade_ ? 0.0f : 1.0f - ramp(progress_, 0.0f, 0.35f);
}

// Back to front: the leaving view with the finished panel body fading in over it, then the arriving view with the old
// panel body fading out over it. Each picture is drawn at its own aspect inside its moving box, so nothing squashes;
// the two body pictures sit exactly on the panel body.
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
    g.drawImage(leaving_, fitted(leavingRect(), mainArea_));
    g.setOpacity(settledBodyAlpha());
    g.drawImage(settledBody_, panelBody_.toFloat());
    g.setOpacity(1.0f);
    g.drawImage(arriving_, fitted(arrivingRect(), mainArea_));
    g.setOpacity(departingBodyAlpha());
    g.drawImage(departingBody_, oldPanelBody_.toFloat());
}

} // namespace synth::ui
