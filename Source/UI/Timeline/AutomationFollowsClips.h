#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <optional>
#include <vector>

// "Automation follows events" (docs/timeline/track-automation.md#automation-follows-events): the
// span edit that carries one clip's automation along with a clip edit, fed to
// TimelineDoc::transferAutomationSpans inside the SAME recordTimelineChange as the clip edit itself.
// Header-only and pure; shared by TimelineClipLaneArea (drag, Delete, context menu) and
// TimelinePanelComponent's clipboard verbs (paste, duplicate, cut, repeat).
namespace synth::ui {

using AutomationSpanEdit = synth::TimelineDoc::AutomationSpanEdit;

// Build it BEFORE the clip edit runs: it reads the clip's current track and span. nullopt when the
// clip does not resolve. `destTrack` / `destStartBeat` are ignored for Kind::Remove.
inline std::optional<AutomationSpanEdit> automationSpanEditForClip(const synth::TimelineDoc& doc, synth::ClipId clipId,
                                                                   AutomationSpanEdit::Kind kind,
                                                                   synth::TrackId destTrack = {},
                                                                   double destStartBeat = 0.0) {
    const auto* clip = doc.getClip(clipId);
    const auto* track = doc.getTrackForClip(clipId);
    if (clip == nullptr || track == nullptr)
        return std::nullopt;
    AutomationSpanEdit edit;
    edit.kind = kind;
    edit.sourceTrack = track->id;
    edit.startBeat = clip->startBeat;
    edit.endBeat = clip->startBeat + clip->lengthBeats;
    edit.destTrack = kind == AutomationSpanEdit::Kind::Remove ? synth::TrackId{} : destTrack;
    edit.destStartBeat = destStartBeat;
    return edit;
}

// Applies `edits` when there are any -- call from INSIDE the clip edit's mutation lambda.
inline void applyAutomationSpanEdits(synth::TimelineDoc& doc, const std::vector<AutomationSpanEdit>& edits) {
    if (!edits.empty())
        doc.transferAutomationSpans(edits);
}

} // namespace synth::ui
