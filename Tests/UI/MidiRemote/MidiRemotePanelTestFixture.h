#pragma once

// MidiRemotePanelTestFixture.h -- the shared fixture for the MIDI Remote panel's headless tests
// (MidiRemotePanelTests.cpp, ControllerSurfaceDetectTests.cpp, MidiRemotePanelControllersTests.cpp):
// AudioEngine (Hosted, so the host source key is the "device"), GraphEditor, MidiLearnController over
// a temp ControllerProfileStore, and the panel itself, wired the way MainComponent::
// wireMidiRemoteEngine() wires them -- a full MainComponent is not needed for these seams.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/MidiLearnController.h"
#include "Modules/FilterModule.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/MidiRemote/MidiRemotePanel/MidiRemotePanelComponent.h"

#include <gtest/gtest.h>
#include <memory>

namespace midiremote_test {

class MidiRemotePanelLiveRefreshTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("agentsynth-midiremotepanel-tests-" + juce::Uuid().toString());
        root_.deleteRecursively();

        engine_ = std::make_unique<AudioEngine>(AudioEngine::HostMode::Hosted);
        graphEditor_ = std::make_unique<GraphEditor>(*engine_);
        controller_ = std::make_unique<synth::midi::MidiLearnController>(
            *engine_, *graphEditor_, remoteEngine_, doc_, undo_, statusBar_, synth::ControllerProfileStore(root_));
        remoteEngine_.setClock([this] { return fakeNowMs_; });

        node_ = engine_->getGraph().addNode(std::make_unique<FilterModule>());
        graphEditor_->updateComponents();

        panel_.configure(*engine_, remoteEngine_, *controller_, doc_, *graphEditor_);
        // The wiring MainComponent::wireMidiRemoteEngine() sets up between the two, reproduced
        // directly since this test has no MainComponent of its own.
        controller_->onChanged = [this] { panel_.scheduleLiveRefresh(); };
    }

    void TearDown() override {
        // remoteEngine_ outlives engine_ (declared first): end any open gesture while its parameter
        // still exists -- RemoteEngine::endAllGestures()'s lifetime contract.
        remoteEngine_.endAllGestures();
        root_.deleteRecursively();
    }

    void send(const juce::MidiMessage& message) { remoteEngine_.handleMessage(synth::midi::hostSourceKey(), message); }
    void settle() {
        remoteEngine_.drain();
        fakeNowMs_ += 600.0; // past RemoteEngine's learn settle window, mirrors MidiLearnControllerTests.cpp
        remoteEngine_.drain();
    }
    void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }

    juce::File root_;
    double fakeNowMs_ = 0.0;
    synth::midi::RemoteEngine remoteEngine_;
    synth::MidiRemoteProjectDoc doc_;
    AppUndoManager undo_;
    StatusBarComponent statusBar_;
    std::unique_ptr<AudioEngine> engine_;
    std::unique_ptr<GraphEditor> graphEditor_;
    std::unique_ptr<synth::midi::MidiLearnController> controller_;
    juce::AudioProcessorGraph::Node::Ptr node_;
    synth::ui::MidiRemotePanelComponent panel_;
};

} // namespace midiremote_test

using midiremote_test::MidiRemotePanelLiveRefreshTest;
