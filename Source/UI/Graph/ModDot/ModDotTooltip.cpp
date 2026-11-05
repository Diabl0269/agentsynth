#include "ModDotTooltip.h"

#include "UI/Graph/ModuleComponent/ModuleComponentModChip.h"
#include "UI/Layout/AppTooltipWindow.h"
#include "UI/Layout/ReducedMotion.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

ModDotTooltip::ModDotTooltip(juce::Component& canvas)
    : canvas_(canvas)
    , vblank_(&canvas) {}

ModDotTooltip::~ModDotTooltip() {
    fade_.stop(vblank_);
    if (wanted_)
        AppTooltipWindow::blockHoverTips(false);
}

juce::String ModDotTooltip::getText() const {
    return content_ ? formatModHoverChipText(content_->name, content_->amount) : juce::String();
}

bool ModDotTooltip::animates() const { return canvas_.isShowing(); }

void ModDotTooltip::show(juce::AudioProcessorGraph::NodeID card, int destChannel,
                         juce::AudioProcessorGraph::NodeID attenuverter, const juce::String& name, float amount) {
    const auto previousCard = content_ ? content_->card : juce::AudioProcessorGraph::NodeID();
    content_ = ModDotReadout{card, destChannel, attenuverter, name, amount};
    const bool appeared = !wanted_;
    wanted_ = true;
    if (appeared) {
        AppTooltipWindow::blockHoverTips(true); // the readout is the one tip of this gesture
        fadeTo(1.0f, kFadeInMs, easeOutCubic);
    }
    if (onChanged) { // a re-text repaints the card it was on too, in case it moved
        if (previousCard.uid != 0 && previousCard != card)
            onChanged(previousCard);
        onChanged(card);
    }
    if (appeared && onShownChanged)
        onShownChanged();
}

void ModDotTooltip::hide() {
    if (!wanted_)
        return;
    wanted_ = false;
    AppTooltipWindow::blockHoverTips(false);
    fadeTo(0.0f, kFadeOutMs, easeInCubic);
    if (onShownChanged)
        onShownChanged();
}

// Headless or hidden there is nothing to animate: it lands at once. Under Reduce Motion it is a plain,
// short, linear fade rather than the eased one.
void ModDotTooltip::fadeTo(float target, double fullMs, std::function<float(float)> easing) {
    if (!animates()) {
        fade_.stop(vblank_);
        setOpacity(target);
        return;
    }
    const float from = opacity_;
    if (prefersReducedMotion()) {
        fullMs = kReducedFadeMs;
        easing = [](float t) { return t; };
    }
    const double ms = std::max(1.0, std::abs((double)target - (double)from) * fullMs);
    fade_.start(vblank_, ms, std::move(easing),
                [this, from, target](float e) { setOpacity(from + (target - from) * e); });
}

void ModDotTooltip::setOpacity(float value) {
    opacity_ = juce::jlimit(0.0f, 1.0f, value);
    if (!content_)
        return;
    const auto card = content_->card;
    if (opacity_ <= 0.0f && !wanted_)
        content_.reset(); // fully gone: nothing left to draw
    if (onChanged)
        onChanged(card);
}

} // namespace synth::ui
