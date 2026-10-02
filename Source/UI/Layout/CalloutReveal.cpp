// Concern: CalloutReveal's entrance -- when it starts, where it slides from, and the landing rules.
#include "CalloutReveal.h"

#include "UI/Layout/ReducedMotion.h"

namespace synth::ui {

CalloutReveal::CalloutReveal(juce::Component& content)
    : content_(content)
    , updater_(&content) {}

CalloutReveal::~CalloutReveal() { driver_.stop(updater_); }

juce::CallOutBox* CalloutReveal::findCallout() const { return content_.findParentComponentOfClass<juce::CallOutBox>(); }

// A CallOutBox lays its content out inside a border, with a larger inset on the side its arrow sits: that side faces
// the anchor, so it is the direction the popup grows out of.
juce::Point<int> CalloutReveal::directionToAnchor(juce::BorderSize<int> insets) {
    const int top = insets.getTop(), bottom = insets.getBottom(), left = insets.getLeft(), right = insets.getRight();
    const int most = juce::jmax(top, bottom, left, right);
    const int least = juce::jmin(top, bottom, left, right);
    if (most == least)
        return {0, 0};
    if (most == top)
        return {0, -1};
    if (most == bottom)
        return {0, 1};
    return most == left ? juce::Point<int>(-1, 0) : juce::Point<int>(1, 0);
}

CalloutReveal::Frame CalloutReveal::frameAt(float t, juce::Point<int> startOffset) {
    const float clamped = juce::jlimit(0.0f, 1.0f, t);
    const float remaining = 1.0f - clamped;
    return {clamped,
            {juce::roundToInt((float)startOffset.x * remaining), juce::roundToInt((float)startOffset.y * remaining)}};
}

// The callout is hidden (alpha 0) from the moment its content is parented, so it never flashes at full opacity for
// the frame before the animation starts; the animation itself starts on the next message-loop turn, once the callout
// has been shown, because a VBlank only reaches a component that is on screen.
void CalloutReveal::startIfInCallout() {
    auto* callout = findCallout();
    if (callout == nullptr || pending_ || driver_.isRunning() || prefersReducedMotion())
        return;
    pending_ = true;
    callout->setAlpha(0.0f);
    juce::Component::SafePointer<juce::CallOutBox> safeCallout(callout);
    juce::Component::SafePointer<juce::Component> safeContent(&content_);
    juce::MessageManager::callAsync([this, safeCallout, safeContent] {
        if (safeContent == nullptr)
            return; // the popup went away first, and this object with it
        begin(safeCallout);
    });
}

void CalloutReveal::begin(juce::Component::SafePointer<juce::CallOutBox> callout) {
    pending_ = false;
    if (callout == nullptr)
        return;
    if (!callout->isShowing()) {
        callout->setAlpha(1.0f); // nothing to animate: land
        return;
    }
    const auto inner = content_.getBoundsInParent();
    const juce::BorderSize<int> insets(inner.getY(), inner.getX(), callout->getHeight() - inner.getBottom(),
                                       callout->getWidth() - inner.getRight());
    const auto direction = directionToAnchor(insets);
    startOffset_ = {direction.x * kRisePx, direction.y * kRisePx};
    finalTopLeft_ = callout->getPosition();
    applyFrame(*callout, 0.0f);
    driver_.start(
        updater_, kInMs, easeOutCubic,
        [callout, this](float t) {
            if (callout != nullptr)
                applyFrame(*callout, t);
        },
        [callout, this] {
            if (callout != nullptr)
                applyFrame(*callout, 1.0f);
        });
}

void CalloutReveal::applyFrame(juce::CallOutBox& callout, float t) {
    const auto frame = frameAt(t, startOffset_);
    callout.setAlpha(frame.alpha);
    callout.setTopLeftPosition(finalTopLeft_ + frame.offset);
}

bool CalloutReveal::applyFrameForTest(float t) {
    auto* callout = findCallout();
    if (callout == nullptr)
        return false;
    finalTopLeft_ = callout->getPosition();
    applyFrame(*callout, t);
    return true;
}

} // namespace synth::ui
