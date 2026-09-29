#include "ShortcutHintOverlay.h"

#include "ShortcutHintText.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

// Concern: the overlay's state machine -- when the hints appear, fade and cancel. Where each bubble
// goes is ShortcutHintOverlayEntries.cpp; the geometry rules are ShortcutHintLayout.cpp.

ShortcutHintOverlay::ShortcutHintOverlay(juce::Component& host, ShortcutManager& shortcuts)
    : host_(host)
    , shortcuts_(shortcuts)
    , clock_([] { return juce::Time::getMillisecondCounterHiRes(); }) {
    // Never a click target, never an accessibility element, never keyboard-focusable.
    setInterceptsMouseClicks(false, false);
    setWantsKeyboardFocus(false);
    setAccessible(false);
    setOpaque(false);
    setVisible(false);

    host_.addAndMakeVisible(this);
    host_.addKeyListener(this);
    host_.addComponentListener(this);
    setBounds(host_.getLocalBounds());
    shortcuts_.addChangeListener(this);
    juce::Desktop::getInstance().addFocusChangeListener(this);
    juce::Desktop::getInstance().addGlobalMouseListener(this);
}

ShortcutHintOverlay::~ShortcutHintOverlay() {
    stopTimer();
    fade_.stop(vblank_);
    juce::Desktop::getInstance().removeGlobalMouseListener(this);
    juce::Desktop::getInstance().removeFocusChangeListener(this);
    shortcuts_.removeChangeListener(this);
    host_.removeComponentListener(this);
    host_.removeKeyListener(this);
}

void ShortcutHintOverlay::addTarget(juce::Component& component, const juce::String& actionId) {
    targets_.push_back({juce::Component::SafePointer<juce::Component>(&component), actionId});
}

bool ShortcutHintOverlay::isCommandAlone(const juce::ModifierKeys& mods) noexcept {
    return (mods.getRawFlags() & juce::ModifierKeys::allKeyboardModifiers) == juce::ModifierKeys::commandModifier;
}

void ShortcutHintOverlay::sample(const juce::ModifierKeys& mods) {
    // A held mouse button is a click: cancel, like any other key.
    if (mods.isAnyMouseButtonDown()) {
        cancelNow();
        armed_ = !mods.isCommandDown();
        return;
    }

    if (!mods.isCommandDown()) {
        armed_ = true;
        if (state_ == State::Pending)
            hideNow();
        else if (state_ == State::Showing)
            beginFadeOut();
        return;
    }

    if (!isCommandAlone(mods)) {
        // Cmd plus another modifier: that is a chord in the making, not a hint request.
        cancelNow();
        return;
    }

    if (state_ == State::Idle && armed_) {
        state_ = State::Pending;
        pressedAtMs_ = clock_();
        startTimer(static_cast<int>(kShowDelayMs));
    } else if (state_ == State::Pending && clock_() - pressedAtMs_ >= kShowDelayMs) {
        showHints();
    }
}

void ShortcutHintOverlay::timerCallback() {
    stopTimer();
    // The timer is the deadline; the live modifier state confirms Cmd is still the only thing down.
    if (state_ == State::Pending)
        sample(juce::ModifierKeys::getCurrentModifiersRealtime());
}

bool ShortcutHintOverlay::keyPressed(const juce::KeyPress& key, juce::Component*) {
    if (state_ != State::Idle)
        cancelNow();
    // A chord (Cmd+S) pressed before Cmd was ever sampled still latches the hints off for this hold.
    if (key.getModifiers().isCommandDown())
        armed_ = false;
    return false;
}

void ShortcutHintOverlay::globalFocusChanged(juce::Component* focused) {
    if (focused == nullptr)
        cancelNow();
}

void ShortcutHintOverlay::mouseDown(const juce::MouseEvent&) { cancelNow(); }

void ShortcutHintOverlay::componentMovedOrResized(juce::Component& component, bool, bool) {
    if (&component != &host_)
        return;
    setBounds(host_.getLocalBounds());
    if (areHintsShowing())
        rebuildEntries();
}

void ShortcutHintOverlay::changeListenerCallback(juce::ChangeBroadcaster*) {
    if (areHintsShowing())
        rebuildEntries();
}

void ShortcutHintOverlay::cancelNow() {
    const bool wasActive = state_ != State::Idle;
    hideNow();
    // Stay quiet until Cmd is released, so one hold never hints twice.
    if (wasActive)
        armed_ = false;
}

void ShortcutHintOverlay::hideNow() {
    stopTimer();
    fade_.stop(vblank_);
    state_ = State::Idle;
    opacity_ = 0.0f;
    entries_.clear();
    setVisible(false);
}

void ShortcutHintOverlay::showHints() {
    stopTimer();
    // A modal dialog or menu is up: its own key handling owns the keyboard, so stay out of it.
    if (juce::Component::getCurrentlyModalComponent() != nullptr) {
        hideNow();
        armed_ = false;
        return;
    }

    setBounds(host_.getLocalBounds());
    rebuildEntries();
    if (entries_.empty()) {
        hideNow();
        armed_ = false;
        return;
    }

    state_ = State::Showing;
    setVisible(true);
    toFront(false);
    if (!isShowing()) {
        setOpacity(1.0f); // nothing to animate against (headless): land immediately
        return;
    }
    setOpacity(0.0f);
    fade_.start(vblank_, kFadeInMs, easeOutCubic, [this](float t) { setOpacity(t); });
}

void ShortcutHintOverlay::beginFadeOut() {
    if (!isShowing() || opacity_ <= 0.0f) {
        hideNow();
        return;
    }
    state_ = State::FadingOut;
    const float from = opacity_;
    fade_.start(
        vblank_, kFadeOutMs, easeOutCubic, [this, from](float t) { setOpacity(from * (1.0f - t)); },
        [this] { hideNow(); });
}

void ShortcutHintOverlay::setOpacity(float value) {
    opacity_ = juce::jlimit(0.0f, 1.0f, value);
    repaint();
}

void ShortcutHintOverlay::paint(juce::Graphics& g) {
    const auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf == nullptr || entries_.empty() || opacity_ <= 0.0f)
        return;

    const bool layer = opacity_ < 1.0f;
    if (layer)
        g.beginTransparencyLayer(opacity_);

    const auto& theme = lf->getTheme();
    for (const auto& entry : entries_) {
        if (entry.isPill) {
            const auto pill = entry.bounds.toFloat().reduced(0.5f);
            g.setColour(theme.colors.surface);
            g.fillRoundedRectangle(pill, theme.metrics.pillRadius);
            g.setColour(theme.colors.border);
            g.drawRoundedRectangle(pill, theme.metrics.pillRadius, theme.metrics.borderWidth);

            g.setColour(theme.colors.textPrimary);
            g.setFont(juce::Font(juce::FontOptions(theme.type.uiFamily, theme.type.label, juce::Font::plain)));
            auto text =
                entry.bounds.withTrimmedLeft(entry.cap.getRight() - entry.bounds.getX() + 6).withTrimmedRight(8);
            g.drawText(entry.label, text, juce::Justification::centredLeft, false);
        }
        lf->drawShortcutKeyCap(g, entry.cap, entry.keyText);
    }

    if (layer)
        g.endTransparencyLayer();
}

} // namespace synth::ui
