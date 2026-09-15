// Concern: FRO11 (P9-5) -- MixerDirectColumn's "Make channel" resolution and enablement.
#include "MixerDirectColumn.h"

#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MasterSplice.h"
#include "Modules/MasterModule.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

MixerDirectColumn::MixerDirectColumn() {
    addAndMakeVisible(header_);
    header_.setDisplayName("Direct");
    addAndMakeVisible(makeChannelButton_);
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

void MixerDirectColumn::paint(juce::Graphics& g) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto bg = laf != nullptr ? laf->getTheme().colors.surface : juce::Colour(0xff1B1F26);
    const auto border = laf != nullptr ? laf->getTheme().colors.border : juce::Colour(0xff2A2F38);
    g.setColour(bg);
    g.fillRect(getLocalBounds());
    g.setColour(border);
    g.drawRect(getLocalBounds(), 1);
}

void MixerDirectColumn::resized() {
    auto bounds = getLocalBounds().reduced(2);
    header_.setBounds(bounds.removeFromTop(24));
    makeChannelButton_.setBounds(bounds.removeFromTop(28).reduced(4, 2));
}

} // namespace synth::ui
