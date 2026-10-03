#include "AutomatedMarker.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

namespace {
std::optional<double> clockOverride;
} // namespace

juce::Rectangle<int> automatedMarkerRect(juce::Rectangle<int> controlBounds) {
    return {controlBounds.getX(), controlBounds.getY(), kAutomatedMarkerWidth, kAutomatedMarkerHeight};
}

// Two 2 px dots joined by a 1.5 px line that climbs left to right, like a curve segment on a timeline lane.
void paintAutomatedMarker(juce::Graphics& g, juce::Rectangle<int> controlBounds, juce::Colour colour) {
    constexpr float kDot = 2.0f;
    const auto box = automatedMarkerRect(controlBounds).toFloat();
    const juce::Point<float> start(box.getX() + kDot * 0.5f, box.getBottom() - kDot * 0.5f);
    const juce::Point<float> end(box.getRight() - kDot * 0.5f, box.getY() + kDot * 0.5f);
    g.setColour(colour);
    g.drawLine({start, end}, 1.5f);
    g.fillEllipse(juce::Rectangle<float>(kDot, kDot).withCentre(start));
    g.fillEllipse(juce::Rectangle<float>(kDot, kDot).withCentre(end));
}

juce::Colour automatedMarkerColour(juce::Colour textPrimary, float level) {
    return textPrimary.withAlpha(kAutomatedMarkerAlpha * juce::jlimit(0.0f, 1.0f, level));
}

double AutomatedMarkerFade::nowMs() {
    return clockOverride.has_value() ? *clockOverride : juce::Time::getMillisecondCounterHiRes();
}

void AutomatedMarkerFade::setClockForTest(std::optional<double> ms) { clockOverride = ms; }

void AutomatedMarkerFade::setAutomated(bool on, double now, bool reducedMotion) {
    if (on == target_)
        return;
    from_ = level(now);
    target_ = on;
    startMs_ = now;
    durationMs_ = reducedMotion ? kReducedMs : (on ? kInMs : kOutMs);
}

float AutomatedMarkerFade::level(double now) const noexcept {
    const float to = target_ ? 1.0f : 0.0f;
    if (durationMs_ <= 0.0)
        return to;
    const auto t = static_cast<float>(juce::jlimit(0.0, 1.0, (now - startMs_) / durationMs_));
    return from_ + (to - from_) * (target_ ? easeOutCubic(t) : easeInCubic(t));
}

bool AutomatedMarkerFade::isSettled(double now) const noexcept {
    return durationMs_ <= 0.0 || now - startMs_ >= durationMs_;
}

namespace {
const juce::Identifier kBaseTooltipKey("automatedBaseTooltip");
const juce::Identifier kBaseDescriptionKey("automatedBaseDescription");

// The control's own text is parked on the control itself, so an owner that rebuilds its marker state (a mixer column
// rebinding to another node) still restores the right tooltip and description when the lane goes.
void restoreText(juce::Component& control) {
    if (!control.getProperties().contains(kBaseTooltipKey))
        return;
    if (auto* client = dynamic_cast<juce::SettableTooltipClient*>(&control))
        client->setTooltip(control.getProperties()[kBaseTooltipKey].toString());
    control.setDescription(control.getProperties()[kBaseDescriptionKey].toString());
    control.getProperties().remove(kBaseTooltipKey);
    control.getProperties().remove(kBaseDescriptionKey);
}

void applyAutomatedText(juce::Component& control, const juce::String& name) {
    auto* client = dynamic_cast<juce::SettableTooltipClient*>(&control);
    if (!control.getProperties().contains(kBaseTooltipKey)) {
        control.getProperties().set(kBaseTooltipKey, client != nullptr ? client->getTooltip() : juce::String());
        control.getProperties().set(kBaseDescriptionKey, control.getDescription());
    }
    const juce::String baseTip = control.getProperties()[kBaseTooltipKey].toString();
    const juce::String baseDesc = control.getProperties()[kBaseDescriptionKey].toString();
    if (client != nullptr) {
        const juce::String line = "Automated: " + name;
        client->setTooltip(baseTip.isNotEmpty() ? baseTip + "\n" + line : line);
    }
    control.setDescription(baseDesc.isNotEmpty() ? baseDesc + ". Automated" : juce::String("Automated"));
}
} // namespace

bool AutomatedControlState::update(juce::Component& control, bool automated, const juce::String& name, double now,
                                   bool reducedMotion) {
    if (automated == fade.isAutomated()) {
        if (!automated)
            restoreText(control); // a stale flag from a state this owner has since rebuilt
        return false;
    }
    fade.setAutomated(automated, now, reducedMotion);
    if (automated)
        applyAutomatedText(control, name);
    else
        restoreText(control);
    return true;
}

struct AutomatedMarkerTicker::Impl {
    explicit Impl(juce::Component& c)
        : updater(&c) {}
    juce::VBlankAnimatorUpdater updater;
    AnimationDriver driver;
};

AutomatedMarkerTicker::AutomatedMarkerTicker(juce::Component& owner)
    : impl_(std::make_unique<Impl>(owner)) {}

AutomatedMarkerTicker::~AutomatedMarkerTicker() = default;

void AutomatedMarkerTicker::run(std::function<void()> onFrame) {
    impl_->driver.start(
        impl_->updater, AutomatedMarkerFade::kInMs, [](float t) { return t; }, [onFrame](float) { onFrame(); },
        onFrame);
}

} // namespace synth::ui
