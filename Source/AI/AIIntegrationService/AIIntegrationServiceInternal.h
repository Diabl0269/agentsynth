#pragma once

// Private to the AIIntegrationService units: the edit plan ("project edit") reader and runner that
// AIIntegrationServiceProjectEdit.cpp drives for both preview and apply. Never included outside
// this directory. The mechanism is documented in docs/ai/timeline-ops.md#one-edit-plan.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Timeline/TimelineOps.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <optional>
#include <set>
#include <vector>

namespace synth::projectedit {

/** One id an addInstrumentTrack op reserves: its instrument (insertIndex -1) or one insert. */
struct Reservation {
    juce::uint32 id = 0;
    int buildIndex = 0;   // which addInstrumentTrack op of the plan, counting only those
    int insertIndex = -1; // -1 = the instrument
};

/** A response root split into the three phases it applies in, with its id namespace checked. */
struct Plan {
    bool hasPatch = false;
    bool hasTrackOps = false;
    bool modeStated = false; // "mode" was given; false lets a replace that only validates as a merge repair
    bool merge = false;      // the mode the patch phase starts in
    juce::var trackOps;      // {"timelineOps": [addTrack/addInstrumentTrack ...]}, void when none
    juce::var otherOps;      // {"timelineOps": [placeClips/writeLane/placeMidiClip ...]}, void when none
    std::vector<int> trackOpIndex, otherOpIndex; // each phase op's index in the response's own list
    std::vector<Reservation> reservations;
    std::set<juce::uint32> patchNodeIds;
    bool buildsInstrumentTracks = false;
};

/** Reads `root` into `out`; empty on success, else the rejection. `liveIds` = every node uid now live. */
juce::String readPlan(const juce::var& root, const std::set<juce::uint32>& liveIds, Plan& out);

/** What one run of the three phases produced. */
struct RunResult {
    bool ok = true;
    juce::String message;
    juce::String opsPreview; // the TimelineOps sentences of phases 1 and 3, joined
    bool merge = false;      // the mode the patch phase ran in
    juce::var patchBefore;   // graphToJSON before / after the patch phase (void without a patch)
    juce::var patchAfter;
};

/** Runs phases 1-3 of `plan` against `doc`/`graph`, building through `buildHost`. `root` is the patch. */
RunResult runPlan(const Plan& plan, const juce::var& root, TimelineDoc& doc, juce::AudioProcessorGraph& graph,
                  TimelineOpsHost& buildHost, bool checkStructure);

/** A TimelineOpsHost that builds stand-ins on a scratch doc and graph, for previewing a plan. */
class StandInHost final : public TimelineOpsHost {
public:
    StandInHost(TimelineDoc& doc, juce::AudioProcessorGraph& graph)
        : doc_(doc)
        , graph_(graph) {}
    std::optional<InstrumentTrackBuildResult>
    addInstrumentTrack(const juce::String& name, const juce::String& instrumentType, bool poly,
                       const std::vector<InstrumentTrackInsert>& inserts) override;
    bool recordBatch(const std::function<void()>& mutation) override;
    TimelineDoc* editableTimelineDoc() override { return &doc_; }

private:
    TimelineDoc& doc_;
    juce::AudioProcessorGraph& graph_;
};

} // namespace synth::projectedit
