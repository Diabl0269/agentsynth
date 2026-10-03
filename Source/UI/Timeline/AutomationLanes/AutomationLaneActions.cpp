// Concern: the undoable edits and the label/value reads behind an automation lane row.
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"

#include "AppUndoManager.h"
#include "Timeline/AutomationKernel.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"
#include "UI/Timeline/AutomationLanes/PointReadout/PointValueField.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include "UI/Timeline/TrackColour.h"
#include <cmath>

namespace synth::ui {

namespace {
// Runs `mutation` as one undo step, or directly with no undo manager. The lambda owns copies of
// everything it touches: a lane header that triggers a delete is destroyed by the doc notification
// while this is still on the stack, so nothing here may reach back into the caller.
bool applyEdit(synth::TimelineDoc& doc, AppUndoManager* undo, const std::function<bool()>& mutation) {
    bool changed = false;
    auto run = [&changed, mutation] { changed = mutation(); };
    if (undo != nullptr)
        undo->recordTimelineChange(doc, run);
    else
        run();
    return changed;
}
} // namespace

// The record-mode selector's write. A manual pick IS a user gesture, so unlike AutomationRecorder's
// own Write-drops-to-Touch-on-stop call it goes on the undo stack.
bool setLaneRecordModeUndoable(synth::TimelineDoc& doc, AppUndoManager* undo, synth::LaneId lane, int mode) {
    const auto* current = doc.getLane(lane);
    if (current == nullptr || current->recordMode == mode)
        return false;
    synth::TimelineDoc* target = &doc;
    return applyEdit(doc, undo, [target, lane, mode] { return target->setLaneRecordMode(lane, mode); });
}

// The Automation track exists only to hold lanes no single track owns, so the step that takes its
// last lane away also takes the track; undo brings both back together.
bool moveLaneUndoable(synth::TimelineDoc& doc, AppUndoManager* undo, synth::LaneId lane, synth::TrackId dest,
                      const std::vector<synth::LaneId>& companions) {
    synth::TimelineDoc* target = &doc;
    return applyEdit(doc, undo, [target, lane, dest, companions] {
        if (!target->moveLaneToTrack(lane, dest))
            return false;
        for (const auto companion : companions)
            target->moveLaneToTrack(companion, dest);
        target->removeEmptyAutomationTracks();
        return true;
    });
}

std::vector<synth::LaneId> amountLanesTravellingWith(const synth::TimelineDoc& doc, TrackHeaderHost* host,
                                                     synth::LaneId lane) {
    std::vector<synth::LaneId> travelling;
    const auto* moving = doc.getLane(lane);
    if (host == nullptr || moving == nullptr)
        return travelling;
    for (const auto& info : host->getModulators(moving->nodeUuid, moving->paramId))
        if (const auto* amount = amountLaneFor(doc, info.attenuverterUuid); amount != nullptr && amount->id != lane)
            travelling.push_back(amount->id);
    return travelling;
}

bool moveLaneOrderUndoable(synth::TimelineDoc& doc, AppUndoManager* undo, synth::LaneId lane, int newIndex) {
    synth::TimelineDoc* target = &doc;
    return applyEdit(doc, undo, [target, lane, newIndex] { return target->moveLane(lane, newIndex); });
}

bool deleteLaneUndoable(synth::TimelineDoc& doc, AppUndoManager* undo, synth::LaneId lane) {
    synth::TimelineDoc* target = &doc;
    return applyEdit(doc, undo, [target, lane] {
        if (!target->removeLane(lane))
            return false;
        target->removeEmptyAutomationTracks();
        return true;
    });
}

bool retargetLaneUndoable(synth::TimelineDoc& doc, AppUndoManager* undo, synth::LaneId lane, const LaneTarget& target) {
    synth::TimelineDoc* doc_ = &doc;
    return applyEdit(doc, undo, [doc_, lane, target] {
        return doc_->retargetLane(lane, target.nodeUuid, target.paramId, target.range, target.paramIndex);
    });
}

synth::LaneId duplicateLaneUndoable(synth::TimelineDoc& doc, AppUndoManager* undo, synth::LaneId lane,
                                    const LaneTarget& target) {
    synth::LaneId created;
    synth::TimelineDoc* doc_ = &doc;
    applyEdit(doc, undo, [doc_, lane, target, &created] {
        created = doc_->duplicateLane(lane, target.nodeUuid, target.paramId, target.range, target.paramIndex);
        return created.isValid();
    });
    return created;
}

std::vector<synth::TrackId> laneMoveTargets(const synth::TimelineDoc& doc, synth::LaneId lane) {
    std::vector<synth::TrackId> targets;
    const auto* owner = doc.getTrackForLane(lane);
    for (const auto& track : doc.getTracks())
        if (track.kind != synth::TrackKind::Automation && (owner == nullptr || track.id != owner->id))
            targets.push_back(track.id);
    return targets;
}

// The same fallbacks the lane picker always used: a node the graph can't name shows its uuid's
// first 8 characters, a parameter it can't name (a hosted plugin's opaque id) shows the raw id.
LaneLabels laneLabelsFor(const synth::AutomationLane& lane, TrackHeaderHost* host) {
    LaneLabels labels;
    if (host != nullptr) {
        labels.parameter = host->getParameterDisplayName(lane.nodeUuid, lane.paramId);
        labels.module = host->getNodeDisplayName(lane.nodeUuid);
    }
    if (labels.parameter.isEmpty())
        labels.parameter = lane.paramId;
    if (labels.module.isEmpty())
        labels.module = lane.nodeUuid.substring(0, 8);
    return labels;
}

// Evaluated here on the message thread from the doc's own breakpoints with a fresh cursor, the
// same way the lane editor draws its curve. Reading AutomationUiFeed instead would add a second
// reader to a feed that is not safe for one.
double laneValueAt(const synth::AutomationLane& lane, double beat) {
    if (lane.points.empty())
        return (double)lane.range.defaultValue;
    std::vector<TimelineSnapshot::Point> points;
    points.reserve(lane.points.size());
    for (const auto& bp : lane.points)
        points.push_back({bp.beat, bp.value, bp.tension, bp.curve});
    AutomationCursor cursor{};
    return AutomationKernel::evaluate(points.data(), (int)points.size(), std::max(0.0, beat),
                                      (double)lane.range.defaultValue, cursor);
}

// The same resolveTrackColour the track's header swatch and clips use, mute dimming included, so a
// lane reads as part of its track. The Automation track has no colour of its own to lend.
juce::Colour laneColourFor(const synth::TimelineDoc& doc, synth::LaneId lane, juce::Colour unassigned) {
    const auto* owner = doc.getTrackForLane(lane);
    if (owner == nullptr || owner->kind == synth::TrackKind::Automation)
        return unassigned;
    const auto& tracks = doc.getTracks();
    const int index = (int)(owner - tracks.data());
    return resolveTrackColour(owner->colourArgb, index, owner->muted);
}

juce::String laneValueText(const synth::AutomationLane& lane, double value, TrackHeaderHost* host) {
    if (host != nullptr)
        if (auto text = host->getParameterValueText(lane.nodeUuid, lane.paramId, value); text.isNotEmpty())
            return text;
    const double magnitude = std::abs(value);
    const int decimals = magnitude >= 100.0 ? 0 : (magnitude >= 10.0 ? 1 : 2);
    return juce::String(value, decimals);
}

std::optional<double> laneTextToValue(const synth::AutomationLane& lane, const juce::String& text,
                                      TrackHeaderHost* host) {
    if (host != nullptr)
        if (auto value = host->getParameterValueFromText(lane.nodeUuid, lane.paramId, text); value.has_value())
            return value;
    return PointValueField::parseNumber(text);
}

juce::String laneParameterName(const synth::AutomationLane& lane, TrackHeaderHost* host) {
    if (host != nullptr)
        if (auto name = host->getParameterDisplayName(lane.nodeUuid, lane.paramId); name.isNotEmpty())
            return name;
    return lane.paramId;
}

} // namespace synth::ui
