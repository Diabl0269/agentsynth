#pragma once

// AutomationLanesMenuFixture.h -- a lane panel with a stub host that offers a fixed parameter list, plus capture
// guards for the lane menu and the change-parameter picker. Shared by the lane menu, retarget and duplicate tests.
// Header-only; not registered in Tests/CMakeLists.txt.

#include "AutomationLanesTestFixture.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Timeline/AutomationLanes/AddAutomation/AddAutomationPicker.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"
#include "UI/Timeline/AutomationLanes/LaneMenuHook.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <map>
#include <optional>

namespace lane_menu_test {

using synth::ui::TrackHeaderHost;

// Offers the same parameters for every track and resolves each to a fixed range, like MainComponent's graph would.
struct PickHost : TrackHeaderHost {
    std::vector<AutomatableParameter> offered;
    std::map<juce::String, synth::AutomationLane::RangeSnapshot> ranges; // by paramId

    void offer(const juce::String& module, const juce::String& name, const juce::String& id,
               synth::AutomationLane::RangeSnapshot range) {
        AutomatableParameter p;
        p.nodeUuid = "node-" + id;
        p.paramId = id;
        p.moduleTitle = module;
        p.parameterName = name;
        offered.push_back(p);
        ranges[id] = range;
    }

    std::vector<AutomatableParameter> getAutomatableParameters(synth::TrackId) override { return offered; }
    std::optional<synth::ui::LaneTarget> prepareLaneTarget(const AutomatableParameter& p) override {
        const auto it = ranges.find(p.paramId);
        if (it == ranges.end())
            return std::nullopt;
        return synth::ui::LaneTarget{p.nodeUuid, p.paramId, -1, it->second};
    }

    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
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

// Keeps the last menu a lane surface would have shown, and the options placing it; always cleared on exit.
struct MenuCapture {
    int count = 0;
    juce::PopupMenu menu;
    juce::PopupMenu::Options options;

    MenuCapture() {
        synth::ui::test_hooks::laneMenuHookForTest() = [this](const juce::PopupMenu& m,
                                                              const juce::PopupMenu::Options& o) {
            ++count;
            menu = m;
            options = o;
        };
    }
    ~MenuCapture() { synth::ui::test_hooks::laneMenuHookForTest() = nullptr; }

    juce::StringArray itemTexts() const {
        juce::StringArray texts;
        juce::PopupMenu::MenuItemIterator it(menu, true);
        while (it.next())
            texts.add(it.getItem().text);
        return texts;
    }
};

// Keeps the picker a lane header would have launched; always cleared on exit.
struct PickerCapture {
    std::unique_ptr<synth::ui::ModMatrixPicker> picker;
    PickerCapture() {
        synth::ui::test_hooks::laneParameterPickerHookForTest() = [this](auto p) { picker = std::move(p); };
    }
    ~PickerCapture() { synth::ui::test_hooks::laneParameterPickerHookForTest() = nullptr; }
};

// Track "Bass" with an open lane on "cutoff" (range 0..100) holding three points; the host offers cutoff, res and
// detune. Undo is wired, so every edit is a step.
struct MenuPanel : automation_lanes_test::LanesPanel {
    PickHost host;
    synth::TrackId track;
    synth::LaneId lane;

    MenuPanel() {
        track = doc.addTrack(synth::TrackKind::Midi, "Bass");
        lane = addLane(track, "cutoff");
        doc.addBreakpoint(lane, 0.0, 0.0);
        doc.addBreakpoint(lane, 2.0, 25.0);
        doc.addBreakpoint(lane, 4.0, 100.0);
        host.offer("Filter 1", "Cutoff", "cutoff", {0.0f, 100.0f, 50.0f});
        host.offer("Filter 1", "Resonance", "res", {0.0f, 1.0f, 0.5f});
        host.offer("Oscillator 1", "Detune", "detune", {-100.0f, 100.0f, 0.0f});
        panel.setTrackHeaderHost(&host);
        panel.setTrackAutomationExpanded(track, true);
        panel.getViewState().firstVisibleBeat = 0.0;
    }
    ~MenuPanel() { panel.setTrackHeaderHost(nullptr); } // the host dies first

    synth::ui::AutomationLaneHeaderComponent* header(synth::LaneId id) { return panel.laneHeaderForTest(id); }
    synth::ui::AutomationLaneEditor* editor(synth::LaneId id) { return panel.laneEditorForTest(id); }
    const synth::AutomationLane& laneOf(synth::LaneId id) { return *doc.getLane(id); }
};

inline juce::ModifierKeys rightButton() { return juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier); }

} // namespace lane_menu_test
