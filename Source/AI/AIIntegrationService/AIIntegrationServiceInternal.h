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

/** One id an addInstrumentTrack op reserves: its instrument, its envelope or one insert. */
struct Reservation {
    static constexpr int kInstrument = -1;
    static constexpr int kEnvelope = -2;
    juce::uint32 id = 0;
    int buildIndex = 0;            // which addInstrumentTrack op of the plan, counting only those
    int insertIndex = kInstrument; // kInstrument, kEnvelope, or the insert's index in op order
};

/** The most bad details (skipped ops, ignored ids) a plan may carry and still be applied; one more refuses it. */
inline constexpr int kMaxBadDetails = 3;

/** One small mistake an edit plan was repaired around instead of being refused for. */
struct PlanNote {
    enum class Kind {
        Skipped, // an op was dropped
        Ignored, // an id that clashed was left out
        Clamped, // a value was moved into its range
        Repaired // a part was dropped because the app built a different instrument than the plan named
    };
    Kind kind = Kind::Skipped;
    juce::String error; // the rejection a model would have been given, for when the plan is refused after all
    juce::String text;  // plain words for the chat card
};

/** True when `notes` hold more bad details than a plan may carry (clamps and repairs are not bad details). */
bool exceedsBadDetailLimit(const std::vector<PlanNote>& notes);

/** The first rejection among `notes` that counts as a bad detail, worded for the model. */
juce::String firstBadDetailError(const std::vector<PlanNote>& notes);

/** A response root split into the three phases it applies in, with its id namespace checked. */
struct Plan {
    bool hasPatch = false;
    bool hasTrackOps = false;
    bool modeStated = false; // "mode" was given; false lets a replace that only validates as a merge repair
    bool merge = false;      // the mode the patch phase starts in
    juce::var trackOps;      // {"timelineOps": [addTrack/addInstrumentTrack ...]}, void when none
    juce::var otherOps; // {"timelineOps": [placeClips/writeLane/placeMidiClip/setTempo/addMarker ...]}, void when none
    std::vector<int> trackOpIndex, otherOpIndex; // each phase op's index in the response's own list
    std::vector<Reservation> reservations;
    std::set<juce::uint32> patchNodeIds;
    bool buildsInstrumentTracks = false;
    std::vector<PlanNote> notes;        // what readPlan dropped or ignored; those ops are already out of otherOps
    std::set<juce::uint32> unusableIds; // ids that clashed: a writeLane naming one is ambiguous, so it is skipped
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
    std::vector<PlanNote> notes; // every skipped, ignored and clamped detail, in plan order
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
    std::optional<InstrumentTrackBuildResult> addInstrumentTrack(const juce::String& name,
                                                                 const juce::String& instrumentType, bool poly,
                                                                 const std::vector<InstrumentTrackInsert>& inserts,
                                                                 const juce::var& envelopeParams,
                                                                 const juce::var& instrumentParams) override;
    std::optional<InstrumentTrackBuildResult>
    addInstrumentTrackWithSound(const juce::String& name, const juce::String& instrumentType, bool poly,
                                const std::vector<InstrumentTrackInsert>& inserts, const juce::var& envelopeParams,
                                const juce::var& instrumentParams, const juce::String& sound) override;
    bool recordBatch(const std::function<void()>& mutation) override;
    TimelineDoc* editableTimelineDoc() override { return &doc_; }
    bool canSetTempo() const override { return true; }
    bool setTempo(double bpm) override;
    bool canSetSongKey() const override { return true; }
    bool setSongKey(const MusicalScale& key) override;

private:
    std::optional<InstrumentTrackBuildResult>
    buildStandIns(const juce::String& name, const juce::String& instrumentType, const juce::String& envelopeShapeType,
                  const std::vector<InstrumentTrackInsert>& inserts, const juce::var& envelopeParams,
                  const juce::var& instrumentParams);
    TimelineDoc& doc_;
    juce::AudioProcessorGraph& graph_;
};

} // namespace synth::projectedit
