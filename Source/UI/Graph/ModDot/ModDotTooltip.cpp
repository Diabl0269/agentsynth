#include "ModDotTooltip.h"

#include "UI/Graph/ModuleComponent/ModuleComponentModChip.h"
#include "UI/Layout/AppTooltipWindow.h"
#include "UI/Layout/ReducedMotion.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

ModDotTooltip::ModDotTooltip(juce::Component& canvas)
    : fade_(canvas) {
    fade_.onFrame = [this] { setOpacity(fade_.alpha()); };
}

ModDotTooltip::~ModDotTooltip() {
    if (wanted_)
        AppTooltipWindow::blockHoverTips(false);
}

juce::String ModDotTooltip::getText() const {
    return content_ ? formatModHoverChipText(content_->name, content_->amount) : juce::String();
}

void ModDotTooltip::show(juce::AudioProcessorGraph::NodeID card, int destChannel,
                         juce::AudioProcessorGraph::NodeID attenuverter, const juce::String& name, float amount) {
    const auto previousCard = content_ ? content_->card : juce::AudioProcessorGraph::NodeID();
    content_ = ModDotReadout{card, destChannel, attenuverter, name, amount};
    const bool appeared = !wanted_;
    wanted_ = true;
    if (appeared) {
        AppTooltipWindow::blockHoverTips(true); // the readout is the one tip of this gesture
        fade_.fadeTo(1.0f, kFadeInMs, easeOutCubic);
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
    fade_.fadeTo(0.0f, kFadeOutMs, easeInCubic);
    if (onShownChanged)
        onShownChanged();
}

void ModDotTooltip::setOpacity(float value) {
    const float opacity = juce::jlimit(0.0f, 1.0f, value);
    if (!content_)
        return;
    const auto card = content_->card;
    if (opacity <= 0.0f && !wanted_)
        content_.reset(); // fully gone: nothing left to draw
    if (onChanged)
        onChanged(card);
}

} // namespace synth::ui
