// Concern: FRO11 (P9-5) -- MixerMasterColumn's param binding and layout.
#include "MixerMasterColumn.h"

#include "AppUndoManager.h"
#include "Modules/MasterModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
juce::AudioParameterFloat* findFloatParam(juce::AudioProcessor& processor, const juce::String& paramId) {
    for (auto* param : processor.getParameters())
        if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param);
            floatParam != nullptr && floatParam->paramID == paramId)
            return floatParam;
    return nullptr;
}
} // namespace

MixerMasterColumn::MixerMasterColumn() {
    addAndMakeVisible(header_);
    header_.setDisplayName("Master");
    addAndMakeVisible(fader_);
    addAndMakeVisible(meter_);
    addAndMakeVisible(muteButton_);
    muteButton_.setClickingTogglesState(false);
}

void MixerMasterColumn::configure(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager) {
    graph_ = &graph;
    undoManager_ = &undoManager;
}

void MixerMasterColumn::setNodeId(juce::AudioProcessorGraph::NodeID nodeId) {
    nodeId_ = nodeId;
    fader_.unbind();
    if (graph_ == nullptr || undoManager_ == nullptr)
        return;
    auto* node = graph_->getNodeForId(nodeId_);
    auto* processor = node != nullptr ? node->getProcessor() : nullptr;
    if (processor == nullptr)
        return;
    if (auto* gainParam = findFloatParam(*processor, "gain"))
        fader_.bind(*graph_, *undoManager_, *gainParam);

    muteButton_.onClick = [this] {
        if (graph_ == nullptr || undoManager_ == nullptr)
            return;
        auto* n = graph_->getNodeForId(nodeId_);
        auto* m = n != nullptr ? dynamic_cast<ModuleBase*>(n->getProcessor()) : nullptr;
        if (m == nullptr)
            return;
        undoManager_->captureBeforeState(*graph_);
        m->setMuted(!m->isMuted());
        undoManager_->pushSnapshotFromCapture(*graph_);
        repaint();
    };
}

void MixerMasterColumn::unbindFromGraph() {
    fader_.unbind();
    muteButton_.onClick = nullptr;
    meter_.peakProvider = nullptr;
}

void MixerMasterColumn::refreshMeter() {
    meter_.peakProvider = [this](int leg) -> float {
        if (graph_ == nullptr)
            return 0.0f;
        auto* node = graph_->getNodeForId(nodeId_);
        if (auto* master = dynamic_cast<MasterModule*>(node != nullptr ? node->getProcessor() : nullptr))
            return master->getMeterPeak(leg);
        return 0.0f;
    };
    meter_.refresh();
}

void MixerMasterColumn::paint(juce::Graphics& g) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto bg = laf != nullptr ? laf->getTheme().colors.surfaceHi : juce::Colour(0xff232833);
    const auto border = laf != nullptr ? laf->getTheme().colors.border : juce::Colour(0xff2A2F38);
    g.setColour(bg);
    g.fillRect(getLocalBounds());
    g.setColour(border);
    g.drawRect(getLocalBounds(), 1);
}

void MixerMasterColumn::resized() {
    auto bounds = getLocalBounds().reduced(2);
    header_.setBounds(bounds.removeFromTop(24));
    muteButton_.setBounds(bounds.removeFromBottom(20).reduced(2));
    meter_.setBounds(bounds.removeFromRight(10));
    bounds.removeFromRight(2);
    fader_.setBounds(bounds);
}

} // namespace synth::ui
