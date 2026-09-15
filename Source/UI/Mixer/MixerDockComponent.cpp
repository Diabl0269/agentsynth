// Concern: FRO11 (P9-5) -- MixerDockComponent's tab strip, persistence and layout.
#include "MixerDockComponent.h"

#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

namespace synth::ui {

MixerDockComponent::MixerDockComponent(TimelinePanelComponent& timelinePanel, AudioEngine& audioEngine,
                                       synth::TimelineDoc& doc, AppUndoManager& undoManager, GraphEditor& graphEditor)
    : timelinePanel_(timelinePanel) {
    addAndMakeVisible(timelineTabButton_);
    addAndMakeVisible(mixerTabButton_);
    timelineTabButton_.setClickingTogglesState(false);
    mixerTabButton_.setClickingTogglesState(false);
    timelineTabButton_.onClick = [this] { setActiveTab(Tab::Timeline); };
    mixerTabButton_.onClick = [this] { setActiveTab(Tab::Mixer); };

    addAndMakeVisible(timelinePanel_);
    addAndMakeVisible(mixer_);
    mixer_.configure(audioEngine.getGraph(), doc, graphEditor.getMacros(), undoManager, graphEditor, audioEngine);

    applyTabVisibility();
}

void MixerDockComponent::setApplicationProperties(juce::ApplicationProperties* properties) {
    appProperties_ = properties;
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    const auto saved = appProperties_->getUserSettings()->getValue(kActiveTabKey, "timeline");
    activeTab_ = saved == "mixer" ? Tab::Mixer : Tab::Timeline;
    applyTabVisibility();
}

void MixerDockComponent::setOnGraphTopologyChanged(std::function<void()> callback) {
    // Forwards straight through: MixerInsertList::onMutated -> MixerColumnComponent::onMutated ->
    // MixerPanelComponent::onGraphMutated (wired per-column in MixerPanelComponent::rebuild()) ->
    // this callback.
    mixer_.onGraphMutated = std::move(callback);
}

void MixerDockComponent::setOnMakeChannelForNode(std::function<void(juce::AudioProcessorGraph::NodeID)> callback) {
    mixer_.onMakeChannelForNode = std::move(callback);
}

void MixerDockComponent::setActiveTab(Tab tab) {
    if (activeTab_ == tab)
        return;
    activeTab_ = tab;
    applyTabVisibility();
    persistActiveTab();
}

void MixerDockComponent::applyTabVisibility() {
    const bool mixerActive = activeTab_ == Tab::Mixer;
    timelinePanel_.setVisible(!mixerActive);
    mixer_.setVisible(mixerActive);
    timelineTabButton_.setToggleState(!mixerActive, juce::dontSendNotification);
    mixerTabButton_.setToggleState(mixerActive, juce::dontSendNotification);
    if (mixerActive)
        mixer_.rebuild();
    resized();
}

void MixerDockComponent::persistActiveTab() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    appProperties_->getUserSettings()->setValue(kActiveTabKey, activeTab_ == Tab::Mixer ? "mixer" : "timeline");
    appProperties_->getUserSettings()->saveIfNeeded();
}

bool MixerDockComponent::revealColumnForStrip(juce::AudioProcessorGraph::NodeID stripId) {
    setActiveTab(Tab::Mixer);
    return mixer_.revealColumn(stripId);
}

void MixerDockComponent::resized() {
    auto bounds = getLocalBounds();
    auto tabStrip = bounds.removeFromTop(kTabStripHeight);
    timelineTabButton_.setBounds(tabStrip.removeFromLeft(tabStrip.getWidth() / 2));
    mixerTabButton_.setBounds(tabStrip);

    timelinePanel_.setBounds(bounds);
    mixer_.setBounds(bounds);
}

} // namespace synth::ui
