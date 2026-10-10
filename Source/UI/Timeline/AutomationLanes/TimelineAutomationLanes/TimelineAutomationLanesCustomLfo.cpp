// Concern: TimelineAutomationLanes' "Create custom LFO": the lane menu's pick, run through the host as one undo step
// with the lane's old curve melting into its flat replacement.
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"

#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"

namespace synth::ui {

// The editor is told first so that the doc change the command makes reads as a melt and not a jump; if the command
// changed nothing, the melt is forgotten. The range is spent once the LFO exists.
bool TimelineAutomationLanes::createCustomLfoFromRange(synth::LaneId lane, double startBeat, double endBeat) {
    if (host_ == nullptr)
        return false;
    if (auto* editor = editorFor(lane))
        editor->meltRangeNext(startBeat, endBeat);
    const bool created = host_->createCustomLfoFromRange(lane, startBeat, endBeat);
    if (auto* editor = editorFor(lane))
        editor->disarmMelt();
    if (created)
        laneRange_.clear();
    return created;
}

} // namespace synth::ui
