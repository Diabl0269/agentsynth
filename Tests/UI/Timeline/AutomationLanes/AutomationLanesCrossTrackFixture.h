#pragma once

// AutomationLanesCrossTrackFixture.h -- the real saved project (SavedProjectFixture) with a Compressor lane on each of
// its three tracks and an LFO added from track 1's Ratio lane, for the tests that move a modulator row to another lane
// or track (by the host, the keyboard or a drag). KeyMove opens every track's lanes in a laid-out panel. Header-only;
// not registered in Tests/CMakeLists.txt.
#include "../../../fixtures/saved-project/SavedProjectFixture.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>

namespace cross_track_test {
using synth::test::SavedProjectFixture;
using synth::ui::ModulatorInfo;

inline juce::AudioProcessorGraph::Node* nodeByUuid(MainComponent& mc, const juce::String& uuid) {
    for (auto* node : mc.getAudioEngine().getGraph().getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return node;
    return nullptr;
}

// The saved project opened the way the app opens a file, with a lane on each track's Compressor and an LFO added
// from track 1's Ratio lane (the LFO joins track 1's macro, as "Add modulator" does). Track 1's Threshold already
// carries the project's own LFO, so its lanes here are Ratio and Attack.
struct CrossTrack {
    SavedProjectFixture project;
    std::unique_ptr<MainComponent> mc = project.open();
    juce::String lfoUuid;
    juce::String compressor[3];
    synth::LaneId ratioA, attackA, thresholdB, thresholdC;

    synth::ui::TrackHeaderHost& host() { return mc->trackHeaderHostForTest(); }
    synth::TimelineDoc& doc() { return mc->getTimelineDoc(); }

    CrossTrack() {
        if (mc == nullptr)
            return;
        for (int t = 0; t < 3; ++t)
            compressor[t] =
                SavedProjectFixture::memberUuidOfType(*mc, SavedProjectFixture::trackMacroId(*mc, t), "Compr");
        const auto& tracks = doc().getTracks();
        const synth::AutomationLane::RangeSnapshot range{0.0f, 1.0f, 0.5f};
        ratioA = doc().addLane(tracks[0].id, compressor[0], "ratio", range);
        attackA = doc().addLane(tracks[0].id, compressor[0], "attack", range);
        thresholdB = doc().addLane(tracks[1].id, compressor[1], "threshold", range);
        thresholdC = doc().addLane(tracks[2].id, compressor[2], "threshold", range);
        lfoUuid = host().addNewModulator(compressor[0], "ratio", "LFO", 0);
        mc->getUndoManager().clearUndoHistory();
    }
    std::vector<ModulatorInfo> rows(int track, const juce::String& paramId) {
        return host().getModulators(compressor[track], paramId);
    }
    juce::String macroName(int track) {
        return mc->getGraphEditor().getMacros().find(SavedProjectFixture::trackMacroId(*mc, track))->name;
    }
};

// The CrossTrack project with every track's lanes open in the timeline, laid out.
struct KeyMove : CrossTrack {
    KeyMove() {
        if (mc == nullptr)
            return;
        mc->setSize(1400, 900);
        for (const auto& track : doc().getTracks())
            panel().setTrackAutomationExpanded(track.id, true);
        panel().refreshModulators();
    }
    synth::ui::TimelinePanelComponent& panel() { return mc->getTimelinePanel(); }
    synth::ui::TimelineAutomationLanes& lanes() { return panel().getAutomationLanes(); }
    // Every lane on screen with a modulator row, in on-screen order, and how many rows each has.
    std::string layout() {
        std::string out;
        for (const auto& track : doc().getTracks())
            for (const auto& lane : track.lanes)
                if (lanes().modulatorCount(lane.id) > 0)
                    out += lane.paramId.toStdString() + "@" + std::to_string(lane.id.value) + "x" +
                           std::to_string(lanes().modulatorCount(lane.id)) + " ";
        return out;
    }
    // The key of the LFO's row on `lane`, empty when that lane has none.
    juce::String lfoKeyOn(synth::LaneId lane) {
        for (int i = 0; i < lanes().modulatorCount(lane); ++i)
            if (auto* row = lanes().modulatorRowFor(lane, i); row->getInfo().sourceUuid == lfoUuid)
                return synth::ui::modulatorOrderKey(row->getInfo());
        return {};
    }
};

} // namespace cross_track_test
