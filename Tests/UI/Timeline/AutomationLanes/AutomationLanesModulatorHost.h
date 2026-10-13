#pragma once

// AutomationLanesModulatorHost.h -- a stub TrackHeaderHost that answers every lane with the same modulator routings and
// records what the rows write, plus a LanesPanel wired to it. Header-only; not registered in Tests/CMakeLists.txt.

#include "AutomationLanesMenuFixture.h"
#include "AutomationLanesTestFixture.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <map>

namespace modulator_rows_test {

using synth::ui::ModulatorInfo;
using synth::ui::ParameterEditPhase;
using namespace automation_lanes_test;

inline ModulatorInfo lfoInfo(const juce::String& n) {
    ModulatorInfo info;
    info.sourceUuid = "lfo-" + n;
    info.sourceTitle = "LFO " + n;
    info.isLfo = true;
    info.attenuverterUuid = "atten-" + n;
    info.paramId = "cutoff";
    info.targetChannel = 2;
    return info;
}

// Answers every lane with the same routings (an Attenuverter's own amount lane with none), and records what the
// rows write.
struct ModulatorHost : synth::ui::TrackHeaderHost {
    std::vector<ModulatorInfo> modulators;
    std::map<juce::String, float> values;
    std::vector<ParameterEditPhase> phases;
    bool modulatable = true;

    std::vector<ModulatorInfo> getModulators(const juce::String& uuid, const juce::String&) override {
        return uuid.startsWith("atten-") ? std::vector<ModulatorInfo>{} : modulators;
    }
    bool canModulate(const juce::String&, const juce::String&) override { return modulatable; }
    float getNodeParameter(const juce::String& uuid, const juce::String& id) override {
        return values[uuid + "." + id];
    }
    void setNodeParameter(const juce::String& uuid, const juce::String& id, float v, ParameterEditPhase p) override {
        values[uuid + "." + id] = v;
        phases.push_back(p);
    }
    juce::String getParameterDisplayName(const juce::String&, const juce::String&) override { return "Cutoff"; }
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return "Filter"; }
    void bindTrackTo(synth::TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(synth::TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(synth::TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
};

// The host outlives the panel (it is declared first and so destroyed last).
struct HostedLanes {
    ModulatorHost host;
    LanesPanel f;
    HostedLanes() { f.panel.setTrackHeaderHost(&host); }
    ~HostedLanes() { f.panel.setTrackHeaderHost(nullptr); }
};

} // namespace modulator_rows_test
