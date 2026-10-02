#pragma once

// MixerHeaderTestRig.h -- the off-screen MainComponent rig shared by the mixer header and row tests
// (MixerSourcesBadgeTests, MixerColourDotTests, MixerRowBypassTests). Header-only; not registered in
// Tests/CMakeLists.txt.
//
// Two audio tracks and a bus, the first track sending to the bus, every section shown: the first strip's column holds
// its default insert chain (with a Parametric EQ) and one send row; the bus column has the first strip as its source.

#include "../../TestSettingsHelpers.h"
#include "../Timeline/TimelinePanel/TimelinePanelTestEvents.h"
#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Layout/BottomDockComponent.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include <juce_events/juce_events.h>

namespace mixer_header_test {

using NodeID = juce::AudioProcessorGraph::NodeID;

class MockProviderMHT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMHT"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

struct HeaderRig {
    synth::test::PersistedKeysGuard guard{{"bottomDockVisible", "bottomDockActiveTab", "bottomDockTabOrder",
                                           "mixerSectionInsertsHidden", "mixerSectionSendsHidden",
                                           "mixerSectionEqHidden"}};
    MainComponent mc{std::make_unique<MockProviderMHT>()};
    NodeID bus;

    HeaderRig() {
        mc.setSize(1400, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        mc.newPatchForTest();
        mc.simulateAddAudioTrackClick();
        mc.simulateAddAudioTrackClick();
        bus = panel().createBus();
        panel().rebuild();
        panel().getStripColumnForTest(0)->getSendList().addSendTo(bus);
        panel().rebuild();
        for (const auto section :
             {synth::ui::MixerSection::Inserts, synth::ui::MixerSection::Sends, synth::ui::MixerSection::Eq})
            panel().getSectionLayout().setHidden(section, false);
        panel().setSize(1400, 700);
        panel().resized();
    }

    synth::ui::MixerPanelComponent& panel() { return mc.getBottomDock().getMixerPanel(); }
    synth::ui::MixerColumnComponent& firstColumn() { return *panel().getStripColumnForTest(0); }
    synth::TimelineDoc& doc() { return mc.getTimelineDoc(); }

    /** The strip column whose node is `id`, or null. */
    synth::ui::MixerColumnComponent* columnFor(NodeID id) {
        for (int i = 0; panel().getStripColumnForTest(i) != nullptr; ++i)
            if (panel().getStripColumnForTest(i)->getNodeId() == id)
                return panel().getStripColumnForTest(i);
        return nullptr;
    }

    /** The bus's column: the strip column that is not one of the two track channels. */
    synth::ui::MixerColumnComponent* busColumn() { return columnFor(bus); }

    ChannelStripModule* strip(NodeID id) {
        auto* node = mc.getAudioEngine().getGraph().getNodeForId(id);
        return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
    }

    /** Runs what a button queued: juce::Button::triggerClick posts its click as a command message. */
    static void pumpMessages() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }
};

/** A press and release on the middle of `button`, as the live mouse would deliver them (hit component first). */
inline void clickThroughMouse(juce::Component& button) {
    const auto centre = button.getLocalBounds().toFloat().getCentre();
    button.mouseEnter(makeClickEvent(button, centre));
    button.mouseDown(makeClickEvent(button, centre));
    button.mouseUp(makeClickEvent(button, centre));
}

} // namespace mixer_header_test
