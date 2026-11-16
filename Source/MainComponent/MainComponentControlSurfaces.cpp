// Concern: wiring the protocol control surfaces (Mackie Control, docs/control/midi-remote-mcu-hui.md) into the app:
// the router becomes AudioEngine's one MIDI Remote sink, the controller hears every profile publish,
// reads the mixer's columns and selects through the same calls a click on a column does.
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"

// Called last from wireMidiRemoteEngine(): nothing may reach the sink before the engine and the
// surfaces have their profiles. The feedback sink is standalone only, like RemoteEngine's own.
void MainComponent::wireControlSurfaces() {
    auto& surfaces = controlSurfaces_.controller;
    if (!audioEngine.isHosted())
        surfaces.setFeedbackSink(&remoteFeedbackOutputs_);
    surfaces.setUndoManager(&undoManager);
    surfaces.setColumnsProvider([this] {
        return synth::buildMixerSnapshot(audioEngine.getGraph(), timelineDoc, graphEditor.getMacros()).columns;
    });
    surfaces.onSelectNode = [this](juce::AudioProcessorGraph::NodeID node) {
        graphEditor.selectModule(node, /*additive=*/false);
        bottomDock.getMixerPanel().revealColumn(node); // scrolls an open mixer; never opens the dock
    };
    midiLearnController_.onProfilesPublished = [this](const std::vector<synth::ControllerProfile>& profiles) {
        controlSurfaces_.controller.setProfiles(profiles);
    };
    surfaces.setProfiles(midiLearnController_.getProfiles());
    surfaces.refreshLayout();
    audioEngine.setRemoteMessageSink(&controlSurfaces_.router);
}
