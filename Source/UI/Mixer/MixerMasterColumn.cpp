// Concern: FRO11 (P9-5) -- MixerMasterColumn's param binding and layout.
#include "MixerMasterColumn.h"

#include "AppUndoManager.h"
#include "Mixer/PeakMeterLatch.h"
#include "Modules/MasterModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>

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
    fader_.setChannelName("Master");
    addAndMakeVisible(meter_);
    meter_.setTitle("Master meter");
    addAndMakeVisible(meterReadout_);
    meterReadout_.onResetAllRequested = [this] {
        if (onResetAllMetersRequested)
            onResetAllMetersRequested();
    };
    addAndMakeVisible(muteButton_);
    muteButton_.setClickingTogglesState(false);
    // FRO18: MixerPanelComponent is the single focusable leaf -- see
    // MixerColumnComponent.cpp's ctor comment for why every child control does this.
    muteButton_.setWantsKeyboardFocus(false);
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

    muteButton_.onClick = [this] { toggleMuted(); };
    refreshMuteAccessibility();
}

void MixerMasterColumn::refreshMuteAccessibility() {
    auto* n = graph_ != nullptr ? graph_->getNodeForId(nodeId_) : nullptr;
    auto* m = n != nullptr ? dynamic_cast<ModuleBase*>(n->getProcessor()) : nullptr;
    const bool muted = m != nullptr && m->isMuted();
    muteButton_.setToggleState(muted, juce::dontSendNotification);
    muteButton_.setTitle(juce::String("Master mute, ") + (muted ? "on" : "off"));
}

void MixerMasterColumn::toggleMuted() {
    if (graph_ == nullptr || undoManager_ == nullptr)
        return;
    auto* n = graph_->getNodeForId(nodeId_);
    auto* m = n != nullptr ? dynamic_cast<ModuleBase*>(n->getProcessor()) : nullptr;
    if (m == nullptr)
        return;
    undoManager_->captureBeforeState(*graph_);
    m->setMuted(!m->isMuted());
    undoManager_->pushSnapshotFromCapture(*graph_);
    refreshMuteAccessibility();
    repaint();
}

void MixerMasterColumn::setKeyboardFocused(bool focused) {
    if (keyboardFocused_ == focused)
        return;
    keyboardFocused_ = focused;
    repaint();
}

void MixerMasterColumn::unbindFromGraph() {
    fader_.unbind();
    muteButton_.onClick = nullptr;
    meter_.peakProvider = nullptr;
}

void MixerMasterColumn::refreshMeter(float elapsedSeconds) {
    meter_.peakProvider = [this](int leg) -> float {
        if (graph_ == nullptr)
            return 0.0f;
        auto* node = graph_->getNodeForId(nodeId_);
        if (auto* master = dynamic_cast<MasterModule*>(node != nullptr ? node->getProcessor() : nullptr))
            return master->takeMeterPeak(synth::MeterReader::Mixer, leg);
        return 0.0f;
    };
    meter_.refresh(elapsedSeconds);
    meterReadout_.updatePeak(std::max(meter_.getDisplayedDbForTest(0), meter_.getDisplayedDbForTest(1)));
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

void MixerMasterColumn::paintOverChildren(juce::Graphics& g) {
    if (!keyboardFocused_)
        return;
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto accent = laf != nullptr ? laf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
    g.setColour(accent.withAlpha(0.85f));
    g.drawRect(getLocalBounds(), 2);
}

void MixerMasterColumn::resized() {
    // FRO146: same meter width/readout-row budget as MixerColumnComponent -- the Master column
    // stays visually consistent with every strip column's meter (docs/mixer.md meters section).
    constexpr int kMeterWidth = 32;
    constexpr int kMeterReadoutHeight = 12;

    auto bounds = getLocalBounds().reduced(2);
    header_.setBounds(bounds.removeFromTop(24));
    muteButton_.setBounds(bounds.removeFromBottom(20).reduced(2));
    meterReadout_.setBounds(bounds.removeFromTop(kMeterReadoutHeight));
    meter_.setBounds(bounds.removeFromRight(kMeterWidth));
    bounds.removeFromRight(2);
    fader_.setBounds(bounds);
}

} // namespace synth::ui
