#pragma once

// MixerZonesTestRig.h -- shared by the Mixer side-pane tests (header-only, not registered in
// Tests/CMakeLists.txt): a real off-screen MainComponent with a few audio tracks, plus small helpers
// for hand-built mouse events on the zones list and the column headers.
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "../Timeline/TimelinePanel/TimelinePanelTestEvents.h"
#include "AI/AIProvider.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerColumnComponent.h"

class MockProviderMZT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMZT"; }
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

struct MixerZonesRig {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc{std::make_unique<MockProviderMZT>()};
    synth::ui::MixerPanelComponent* panel = nullptr;

    explicit MixerZonesRig(int tracks, int panelWidth = 1400) {
        mc.setSize(1400, 900);
        mc.newPatchForTest();
        for (int i = 0; i < tracks; ++i)
            mc.simulateAddAudioTrackClick();
        panel = &mc.getBottomDock().getMixerPanel();
        panel->setSize(panelWidth, 300);
        panel->rebuild();
    }

    /** The channel id of the Nth track strip (mixer order). */
    juce::String stripId(int index) {
        int seen = 0;
        for (int i = 0; i < panel->getZonesPaneForTest().getRowCountForTest(); ++i) {
            const auto& channel = panel->getZonesPaneForTest().getRowForTest(i)->getChannel();
            if (channel.kind == synth::ui::MixerZoneChannelKind::Track && seen++ == index)
                return channel.id;
        }
        return {};
    }

    synth::ui::MixerZonesRow* row(const juce::String& id) { return panel->getZonesPaneForTest().findRowForTest(id); }

    /** Lets a test pick a menu item by its text. */
    void hookMenuToPick(const juce::String& text) {
        panel->setShowZoneMenuHookForTest([text](juce::PopupMenu& menu) {
            juce::PopupMenu::MenuItemIterator it(menu);
            while (it.next())
                if (it.getItem().text == text && it.getItem().isEnabled && it.getItem().action)
                    it.getItem().action();
        });
    }

    std::vector<synth::TrackId> trackOrder() {
        std::vector<synth::TrackId> ids;
        for (const auto& track : mc.getTimelineDoc().getTracks())
            ids.push_back(track.id);
        return ids;
    }
};

/** Whether `component` lives (at any depth) under `ancestor`. */
inline bool isUnder(const juce::Component* component, const juce::Component& ancestor) {
    return component != nullptr && ancestor.isParentOf(component);
}
