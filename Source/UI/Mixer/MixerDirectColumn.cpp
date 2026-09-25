// Concern: FRO11 (P9-5) -- MixerDirectColumn's "Make channel" resolution and enablement.
#include "MixerDirectColumn.h"

#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MasterSplice.h"
#include "Modules/MasterModule.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

MixerDirectColumn::MixerDirectColumn() {
    // FRO18 review fix: grabAccessibilityFocus() grabs focus on `this` (Direct has no fader to
    // target) -- without a title, its default unspecified-role AccessibilityHandler reads nothing.
    setTitle("Direct");
    addAndMakeVisible(header_);
    header_.setDisplayName("Direct");
    header_.setRenameEnabled(false); // FRO225: Direct has no node, so no strip/macro name to write a rename to
    addAndMakeVisible(makeChannelButton_);
    // FRO228: explicit, rather than relying on Button::getButtonText()'s fallback (the ctor's
    // "Make channel" argument already IS the button text, but a title makes the AX handler's
    // getTitle() resolve directly instead of falling through ButtonAccessibilityHandler's own
    // getButtonText() fallback).
    makeChannelButton_.setTitle("Make channel");
    // FRO18: MixerPanelComponent is the single focusable leaf -- see
    // MixerColumnComponent.cpp's ctor comment for why every child control does this.
    makeChannelButton_.setWantsKeyboardFocus(false);
    makeChannelButton_.onClick = [this] {
        const auto source = resolveDirectFeeder();
        if (source != juce::AudioProcessorGraph::NodeID{} && onMakeChannelRequested)
            onMakeChannelRequested(source);
    };
}

void MixerDirectColumn::configure(juce::AudioProcessorGraph& graph) {
    graph_ = &graph;
    refreshEnablement();
}

juce::AudioProcessorGraph::NodeID MixerDirectColumn::resolveDirectFeeder() const {
    if (graph_ == nullptr)
        return {};
    auto* master = synth::findMasterNode(*graph_);
    if (master == nullptr)
        return {};
    for (const auto& c : graph_->getConnections())
        if (c.destination.nodeID == master->nodeID && c.destination.channelIndex == MasterModule::kDirectLeft)
            return synth::resolveChannelSource(*graph_, {c.source.nodeID});
    return {};
}

void MixerDirectColumn::refreshEnablement() {
    makeChannelButton_.setEnabled(resolveDirectFeeder() != juce::AudioProcessorGraph::NodeID{});
}

void MixerDirectColumn::setKeyboardFocused(bool focused) {
    if (keyboardFocused_ == focused)
        return;
    keyboardFocused_ = focused;
    repaint();
}

void MixerDirectColumn::paint(juce::Graphics& g) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto bg = laf != nullptr ? laf->getTheme().colors.surface : juce::Colour(0xff1B1F26);
    const auto border = laf != nullptr ? laf->getTheme().colors.border : juce::Colour(0xff2A2F38);
    g.setColour(bg);
    g.fillRect(getLocalBounds());
    g.setColour(border);
    g.drawRect(getLocalBounds(), 1);
}

void MixerDirectColumn::paintOverChildren(juce::Graphics& g) {
    if (!keyboardFocused_)
        return;
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto accent = laf != nullptr ? laf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
    g.setColour(accent.withAlpha(0.85f));
    g.drawRect(getLocalBounds(), 2);
}

void MixerDirectColumn::resized() {
    auto bounds = getLocalBounds().reduced(2);
    header_.setBounds(bounds.removeFromTop(24));
    makeChannelButton_.setBounds(bounds.removeFromTop(28).reduced(4, 2));
}

} // namespace synth::ui
