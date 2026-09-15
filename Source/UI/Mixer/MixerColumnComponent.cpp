// Concern: FRO11 (P9-5) -- MixerColumnComponent's layout, param binding (pan/mute/solo -- fader
// binding lives in MixerFader itself) and the column-click-selects-macro gesture.
#include "MixerColumnComponent.h"

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
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

MixerColumnComponent::MixerColumnComponent() {
    addAndMakeVisible(header_);
    header_.onHeaderClicked = [this] {
        if (onColumnClicked)
            onColumnClicked();
    };
    addAndMakeVisible(sourceLineLabel_);
    sourceLineLabel_.setFont(juce::Font(juce::FontOptions(10.0f)));
    sourceLineLabel_.setJustificationType(juce::Justification::centredLeft);
    sourceLineLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8A93A0));

    addAndMakeVisible(insertList_);
    insertList_.onEditOnCanvas = [this](const juce::String& uuid) {
        if (onEditOnCanvas)
            onEditOnCanvas(uuid);
    };
    insertList_.onMutated = [this] {
        if (onMutated)
            onMutated();
    };

    panSlider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    panSlider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible(panSlider_);

    addAndMakeVisible(fader_);
    addAndMakeVisible(meter_);

    addAndMakeVisible(muteButton_);
    muteButton_.setClickingTogglesState(false);
    addAndMakeVisible(soloButton_);
    soloButton_.setClickingTogglesState(false);
}

void MixerColumnComponent::configure(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager,
                                     synth::MacroSet& macros, GraphEditor& graphEditor, AudioEngine& audioEngine) {
    graph_ = &graph;
    undoManager_ = &undoManager;
    audioEngine_ = &audioEngine;
    insertList_.configure(graph, undoManager, macros, graphEditor);
}

void MixerColumnComponent::setColumn(const synth::MixerColumn& column, const juce::String& sourceLine) {
    nodeId_ = column.nodeId;
    uuid_ = column.uuid;
    sourceLine_ = sourceLine;

    header_.setColour(column.colour);
    header_.setDisplayName(column.name);
    header_.setLinkedBadgeVisible(column.linkedToTrack);

    // Doubles as §5.10's "the tracks that play into it" row -- `sourceLine` is the caller-resolved
    // (comma-joined) names of column.feedingTracks, the same tracks a "source line" names for a
    // single-source column.
    sourceLineLabel_.setText(sourceLine_, juce::dontSendNotification);
    insertList_.setEntries(column.inserts, column.insertChainIsLinear, column.editOnCanvasTargetUuid,
                           column.sourceNodeId, column.nodeId);

    rebindControls();
    resized();
}

void MixerColumnComponent::rebindControls() {
    panAttachment_.reset();
    fader_.unbind();

    if (graph_ == nullptr)
        return;
    auto* node = graph_->getNodeForId(nodeId_);
    auto* processor = node != nullptr ? node->getProcessor() : nullptr;
    if (processor == nullptr)
        return;

    if (auto* gainParam = findFloatParam(*processor, "gain"); gainParam != nullptr && undoManager_ != nullptr)
        fader_.bind(*graph_, *undoManager_, *gainParam);
    if (auto* panParam = findFloatParam(*processor, "pan")) {
        const auto floatRange = panParam->getNormalisableRange();
        panSlider_.setNormalisableRange(juce::NormalisableRange<double>(
            (double)floatRange.start, (double)floatRange.end, (double)floatRange.interval, (double)floatRange.skew,
            floatRange.symmetricSkew));
        panAttachment_ = std::make_unique<juce::SliderParameterAttachment>(*panParam, panSlider_);
    }

    auto* module = dynamic_cast<ModuleBase*>(processor);
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
    soloButton_.setVisible(dynamic_cast<ChannelStripModule*>(processor) != nullptr);
    soloButton_.onClick = [this] {
        if (graph_ == nullptr || undoManager_ == nullptr || audioEngine_ == nullptr)
            return;
        auto* n = graph_->getNodeForId(nodeId_);
        auto* strip = n != nullptr ? dynamic_cast<ChannelStripModule*>(n->getProcessor()) : nullptr;
        if (strip == nullptr)
            return;
        undoManager_->captureBeforeState(*graph_);
        // Never strip->setSoloed() directly -- the engine's soloed-strip count would go stale
        // (root CLAUDE.md / Source/CLAUDE.md).
        audioEngine_->setChannelStripSoloed(nodeId_, !strip->isSoloed());
        undoManager_->pushSnapshotFromCapture(*graph_);
        repaint();
    };
    juce::ignoreUnused(module);
}

void MixerColumnComponent::refreshMeter() {
    meter_.peakProvider = [this](int leg) -> float {
        if (graph_ == nullptr)
            return 0.0f;
        auto* node = graph_->getNodeForId(nodeId_);
        auto* processor = node != nullptr ? node->getProcessor() : nullptr;
        if (auto* strip = dynamic_cast<ChannelStripModule*>(processor))
            return strip->getMeterPeak(leg);
        return 0.0f;
    };
    meter_.refresh();
}

void MixerColumnComponent::setSelected(bool selected) {
    if (selected_ == selected)
        return;
    selected_ = selected;
    repaint();
}

void MixerColumnComponent::paint(juce::Graphics& g) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto bg = laf != nullptr ? laf->getTheme().colors.surface : juce::Colour(0xff1B1F26);
    const auto border = laf != nullptr ? laf->getTheme().colors.border : juce::Colour(0xff2A2F38);
    const auto accent = laf != nullptr ? laf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
    g.setColour(bg);
    g.fillRect(getLocalBounds());
    g.setColour(selected_ ? accent : border);
    g.drawRect(getLocalBounds(), selected_ ? 2 : 1);
}

void MixerColumnComponent::resized() {
    auto bounds = getLocalBounds().reduced(2);
    header_.setBounds(bounds.removeFromTop(24));
    sourceLineLabel_.setBounds(bounds.removeFromTop(14));
    insertList_.setBounds(bounds.removeFromTop(juce::jmin(bounds.getHeight() / 2, insertList_.getPreferredHeight())));

    auto controls = bounds;
    panSlider_.setBounds(controls.removeFromTop(28).reduced(8, 0));

    auto msRow = controls.removeFromBottom(20);
    muteButton_.setBounds(msRow.removeFromLeft(msRow.getWidth() / 2).reduced(2));
    soloButton_.setBounds(msRow.reduced(2));

    meter_.setBounds(controls.removeFromRight(10));
    controls.removeFromRight(2);
    fader_.setBounds(controls);
}

void MixerColumnComponent::mouseUp(const juce::MouseEvent&) {
    // See MixerColumnHeader::mouseUp's comment on why this isn't gated on mouseWasClicked().
    if (onColumnClicked)
        onColumnClicked();
}

} // namespace synth::ui
