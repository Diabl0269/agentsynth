// TimelinePanelClipClipboard.cpp
//
// Clip clipboard: copy/paste/cut/duplicate/repeat/select-all for the clip selection.
// TimelinePanelComponent is declared in TimelinePanelComponent.h; sibling
// TimelinePanel*.cpp files in this directory hold the rest of the class.

#include "TimelinePanelComponent.h"

#include "AppUndoManager.h"
#include "UI/Timeline/AutomationFollowsClips.h"
#include <algorithm>
#include <map>

namespace synth::ui {

namespace {

// "Automation follows clips" for a paste (docs/timeline/track-automation.md#automation-follows-events):
// the lane a captured lane's points land on when its clip is pasted onto `target` -- the same lane
// when it still lives on that track, else the target track's one lane with the same paramId (lane
// identity is unique doc-wide, so another track can only ever match by parameter id), else none.
const synth::AutomationLane* pasteLaneFor(const synth::TimelineDoc& doc, synth::LaneId sourceLane,
                                          synth::TrackId target) {
    const auto* source = doc.getLane(sourceLane);
    const auto* owner = doc.getTrackForLane(sourceLane);
    if (source != nullptr && owner != nullptr && owner->id == target)
        return source;
    const auto* track = doc.getTrack(target);
    if (track == nullptr || source == nullptr)
        return nullptr;
    const synth::AutomationLane* found = nullptr;
    for (const auto& lane : track->lanes)
        if (lane.paramId == source->paramId) {
            if (found != nullptr)
                return nullptr; // ambiguous
            found = &lane;
        }
    return found;
}

// Writes one pasted clip's captured automation: each carried lane replaces the span it lands on,
// through editBreakpoints (validated and clamped like any other lane write). Runs inside the
// paste's own recordTimelineChange, so clip and automation undo together.
void pasteCapturedAutomation(
    synth::TimelineDoc& doc, synth::TrackId target, double startBeat, double lengthBeats,
    const std::vector<std::pair<synth::LaneId, std::vector<synth::AutomationLane::Breakpoint>>>& captured) {
    for (const auto& [sourceLane, points] : captured) {
        const auto* lane = pasteLaneFor(doc, sourceLane, target);
        if (lane == nullptr || points.empty())
            continue;
        std::vector<double> removeBeats;
        for (const auto& p : lane->points)
            if (p.beat >= startBeat && p.beat < startBeat + lengthBeats)
                removeBeats.push_back(p.beat);
        auto shifted = points;
        for (auto& p : shifted)
            p.beat += startBeat;
        doc.editBreakpoints(lane->id, removeBeats, shifted);
    }
}

using CapturedAutomation = std::vector<std::pair<synth::LaneId, std::vector<synth::AutomationLane::Breakpoint>>>;

// The paste's automation half, simulated on copies of the target lanes in the SAME order the paste
// writes them (erase the landing span, insert the carried points, last one wins per beat): false
// when any lane would end up over kMaxBreakpointsPerLane, in which case the whole paste is refused —
// a clip must never land without the automation it carries.
bool pasteAutomationFits(const synth::TimelineDoc& doc, const std::vector<synth::TrackId>& targets,
                         const std::vector<double>& startBeats, const std::vector<CapturedAutomation>& perEntry,
                         const std::vector<double>& lengths) {
    std::map<std::int64_t, std::vector<synth::AutomationLane::Breakpoint>> simulated;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (!targets[i].isValid())
            continue;
        for (const auto& [sourceLane, points] : perEntry[i]) {
            const auto* lane = pasteLaneFor(doc, sourceLane, targets[i]);
            if (lane == nullptr || points.empty())
                continue;
            auto& plan = simulated.try_emplace(lane->id.value, lane->points).first->second;
            const double from = startBeats[i];
            const double to = startBeats[i] + lengths[i];
            plan.erase(std::remove_if(plan.begin(), plan.end(),
                                      [from, to](const auto& p) { return p.beat >= from && p.beat < to; }),
                       plan.end());
            for (auto p : points) {
                p.beat += from;
                const auto pos = std::lower_bound(plan.begin(), plan.end(), p.beat,
                                                  [](const auto& a, double beat) { return a.beat < beat; });
                if (pos != plan.end() && pos->beat == p.beat)
                    *pos = p;
                else
                    plan.insert(pos, p);
            }
            if ((int)plan.size() > synth::TimelineDoc::kMaxBreakpointsPerLane)
                return false;
        }
    }
    return true;
}

} // namespace

bool TimelinePanelComponent::refuseForAutomation() {
    if (clipLaneArea_.onStatusMessage)
        clipLaneArea_.onStatusMessage(kAutomationSpanRefusedMessage);
    return false;
}

//==============================================================================
// ---- Clip clipboard ----
//
// This panel owns the clipboard because it already owns the selection it copies from -- see
// MainComponent::resolveEditSurface()/perform(), which delegate here exactly the way GraphEditor
// owns its own module clipboard.

synth::TrackId TimelinePanelComponent::firstTrackOfKind(synth::TrackKind kind) const {
    if (doc_ == nullptr)
        return {};
    for (const auto& track : doc_->getTracks())
        if (track.kind == kind)
            return track.id;
    return {};
}

double TimelinePanelComponent::currentBeatsPerBarForPaste() const {
    double beatsPerBar = 4.0;
    if (transport_ != nullptr) {
        const auto snap = transport_->getPositionSnapshot();
        const double tsBeatsPerBar = (double)snap.timeSigNumerator * 4.0 / (double)std::max(1, snap.timeSigDenominator);
        if (tsBeatsPerBar > 0.0)
            beatsPerBar = tsBeatsPerBar;
    }
    return beatsPerBar;
}

// Copies WHOLE clips: notes (each with its own muted flag), CC lanes, name, length, muted flag and every
// audio field (assetRef, gainDb, the two fades, sourceStartSeconds) -- see the header for the
// return-value contract.
bool TimelinePanelComponent::copySelectedClips() {
    if (doc_ == nullptr)
        return false;

    const auto selected = clipSelection_.getSelected(); // ascending id order
    if (selected.empty())
        return false;

    // Pass 1: the earliest selected clip's start — every captured entry is stored relative to it.
    bool haveEarliest = false;
    double earliestStart = 0.0;
    for (auto id : selected) {
        const auto* clip = doc_->getClip(id);
        if (clip == nullptr)
            continue;
        if (!haveEarliest || clip->startBeat < earliestStart) {
            earliestStart = clip->startBeat;
            haveEarliest = true;
        }
    }
    if (!haveEarliest)
        return false; // every selected id was stale

    // Pass 2: capture each clip relative to that start.
    std::vector<ClipboardClip> captured;
    for (auto id : selected) {
        const auto* clip = doc_->getClip(id);
        const auto* track = doc_->getTrackForClip(id);
        if (clip == nullptr || track == nullptr)
            continue;
        ClipboardClip entry;
        entry.originalTrack = track->id;
        // The payload, not the source row, decides where this can be pasted — see
        // ClipboardClip::requiredKind.
        entry.requiredKind = clip->assetRef.isNotEmpty() ? synth::TrackKind::Audio : synth::TrackKind::Midi;
        entry.relativeStartBeat = clip->startBeat - earliestStart;
        entry.lengthBeats = clip->lengthBeats;
        entry.name = clip->name;
        entry.notes = clip->notes;             // MidiNote copies carry each note's own muted flag
        entry.controllers = clip->controllers; // CC lanes are part of the clip
        entry.muted = clip->muted;
        entry.assetRef = clip->assetRef;
        entry.gainDb = clip->gainDb;
        entry.fadeInBeats = clip->fadeInBeats;
        entry.fadeOutBeats = clip->fadeOutBeats;
        entry.sourceStartSeconds = clip->sourceStartSeconds;
        if (clipLaneArea_.isAutomationFollowsClips())
            for (const auto& lane : track->lanes) {
                std::vector<synth::AutomationLane::Breakpoint> points;
                for (auto p : lane.points)
                    if (p.beat >= clip->startBeat && p.beat < clip->startBeat + clip->lengthBeats) {
                        p.beat -= clip->startBeat;
                        points.push_back(p);
                    }
                if (!points.empty())
                    entry.automation.emplace_back(lane.id, std::move(points));
            }
        captured.push_back(std::move(entry));
    }
    if (captured.empty())
        return false;

    clipClipboard_ = std::move(captured);
    return true;
}

// The target track for each entry is KIND-AWARE: the original track is used only if it still
// exists AND still plays the clip's payload (an audio clip needs a TrackKind::Audio row, a MIDI
// clip a Midi one -- TimelineDoc::moveClipToTrack's rule); otherwise the doc's first track of the
// required kind; otherwise that clip is skipped. Pasting an audio clip onto a MIDI row would park
// an asset somewhere nothing will ever play it.
//
// Audio fields go back through setClipAsset/setClipGainDb/setClipFades rather than being written
// into the struct, so the clipboard's assetRef passes the SAME bundle-relative validation a loaded
// file's does -- a clipboard is only as trustworthy as whatever filled it. The position is snapped
// via the shared view-state snap and the transport's live time signature.
bool TimelinePanelComponent::pasteClipsAtPlayhead() {
    if (doc_ == nullptr || clipClipboard_.empty())
        return false;

    double playheadBeat = 0.0;
    if (transport_ != nullptr)
        playheadBeat = transport_->getPositionSnapshot().ppq;
    const double snappedPlayhead = viewState_.snapBeat(playheadBeat, currentBeatsPerBarForPaste());

    // Resolved ONCE, before the mutation: the target row for every entry, so the loop below does
    // no lookups against a doc it is halfway through mutating. The original track only counts if
    // it still plays this clip's payload (see ClipboardClip::requiredKind); otherwise the first
    // track of the required kind, and otherwise nothing at all.
    std::vector<synth::TrackId> targets;
    targets.reserve(clipClipboard_.size());
    for (const auto& entry : clipClipboard_) {
        const auto* original = doc_->getTrack(entry.originalTrack);
        targets.push_back(original != nullptr && original->kind == entry.requiredKind
                              ? entry.originalTrack
                              : firstTrackOfKind(entry.requiredKind));
    }

    {
        std::vector<double> starts, lengths;
        std::vector<CapturedAutomation> automation;
        for (const auto& entry : clipClipboard_) {
            starts.push_back(std::max(0.0, snappedPlayhead + entry.relativeStartBeat));
            lengths.push_back(entry.lengthBeats);
            automation.push_back(entry.automation);
        }
        if (!pasteAutomationFits(*doc_, targets, starts, automation, lengths))
            return refuseForAutomation();
    }

    std::vector<synth::ClipId> newIds;
    auto mutate = [this, snappedPlayhead, &targets, &newIds] {
        for (std::size_t i = 0; i < clipClipboard_.size(); ++i) {
            const auto& entry = clipClipboard_[i];
            if (!targets[i].isValid())
                continue; // nowhere this clip could play — skip it rather than park it

            const double startBeat = std::max(0.0, snappedPlayhead + entry.relativeStartBeat);
            const auto newId = doc_->addClip(targets[i], startBeat, entry.lengthBeats, entry.name);
            if (!newId.isValid())
                continue;
            for (const auto& note : entry.notes)
                doc_->addNote(newId, note);
            // CC lanes go back through the mutation API too (its validation, its sort/dedupe).
            for (const auto& lane : entry.controllers) {
                doc_->addControllerLane(newId, lane.ccNumber);
                doc_->setControllerLanePoints(newId, lane.ccNumber, lane.points);
            }
            // Through the setters, not into the struct: setClipAsset is the gate that rejects an
            // assetRef that is not bundle-relative, and a clipboard is not a trusted source.
            if (entry.assetRef.isNotEmpty() || entry.sourceStartSeconds != 0.0)
                doc_->setClipAsset(newId, entry.assetRef, entry.sourceStartSeconds);
            doc_->setClipGainDb(newId, entry.gainDb);
            doc_->setClipFades(newId, entry.fadeInBeats, entry.fadeOutBeats);
            doc_->setClipMuted(newId, entry.muted);
            pasteCapturedAutomation(*doc_, targets[i], startBeat, entry.lengthBeats, entry.automation);
            newIds.push_back(newId);
        }
    };

    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    if (newIds.empty())
        return false;

    // The pasted clips become the selection, and a selection never coexists with a range (see
    // TimelineClipLaneRange.cpp) — so a paste made from the Range tool drops the range.
    clipLaneArea_.clearRange();
    clipSelection_.setSelection(newIds);
    return true;
}

bool TimelinePanelComponent::duplicateSelectedClips() {
    if (doc_ == nullptr)
        return false;

    const auto selected = clipSelection_.getSelected();
    if (selected.empty())
        return false;

    // "Automation follows clips": each copy lands right after its source (duplicateClip's rule).
    std::vector<AutomationSpanEdit> spanEdits;
    if (clipLaneArea_.isAutomationFollowsClips())
        for (auto id : selected)
            if (const auto* clip = doc_->getClip(id))
                if (auto spanEdit =
                        automationSpanEditForClip(*doc_, id, AutomationSpanEdit::Kind::Copy,
                                                  doc_->getTrackForClip(id)->id, clip->startBeat + clip->lengthBeats))
                    spanEdits.push_back(*spanEdit);

    std::vector<synth::ClipId> newIds;
    auto mutate = [this, &selected, &newIds, &spanEdits] {
        for (auto id : selected) {
            const auto newId = doc_->duplicateClip(id);
            if (newId.isValid())
                newIds.push_back(newId);
        }
        applyAutomationSpanEdits(*doc_, spanEdits);
    };

    if (!automationSpanEditsFit(*doc_, spanEdits))
        return refuseForAutomation();
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    if (newIds.empty())
        return false;

    clipSelection_.setSelection(newIds);
    return true;
}

bool TimelinePanelComponent::canCutClips() const noexcept { return doc_ != nullptr && !clipSelection_.isEmpty(); }

bool TimelinePanelComponent::hasClipSelection() const noexcept { return !clipSelection_.isEmpty(); }

bool TimelinePanelComponent::cutSelectedClips() {
    // The copy half also validates (no doc / nothing selected / every id stale all fail there), so
    // nothing is deleted unless something was actually captured.
    if (!copySelectedClips())
        return false;

    const auto selected = clipSelection_.getSelected();
    std::vector<AutomationSpanEdit> spanEdits; // "automation follows clips": the cut takes its automation
    if (clipLaneArea_.isAutomationFollowsClips())
        for (auto id : selected)
            if (auto spanEdit = automationSpanEditForClip(*doc_, id, AutomationSpanEdit::Kind::Remove))
                spanEdits.push_back(*spanEdit);
    auto mutate = [this, selected, spanEdits] {
        applyAutomationSpanEdits(*doc_, spanEdits);
        for (auto id : selected)
            doc_->removeClip(id);
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    clipSelection_.clear();
    clipLaneArea_.repaint();
    return true;
}

bool TimelinePanelComponent::selectAllClips() {
    if (doc_ == nullptr)
        return false;

    std::vector<synth::ClipId> all;
    for (const auto& track : doc_->getTracks())
        for (const auto& clip : track.clips)
            all.push_back(clip.id);
    if (all.empty())
        return false;

    clipLaneArea_.clearRange(); // a selection never coexists with a range
    clipSelection_.setSelection(all);
    clipLaneArea_.repaint();
    return true;
}

// The first copy starts one block-length after the selection's own start, so the copies tile
// forward without overlapping the source. The block length is the selection's span (max end - min
// start), not each clip's own length -- that is what keeps a multi-clip rhythm intact instead of
// collapsing it. duplicateClip + moveClipToTrack per copy -- see the header for the undo contract.
bool TimelinePanelComponent::repeatSelectedClips(int count) {
    if (doc_ == nullptr || count < 1)
        return false;

    // Snapshot the source geometry BEFORE mutating: every duplicate re-seats its track's clip
    // vector, so a Clip pointer (or a re-read of the selection) taken mid-loop would be stale.
    struct Source {
        synth::ClipId id;
        synth::TrackId track;
        double startBeat = 0.0;
    };
    std::vector<Source> sources;
    bool haveSpan = false;
    double spanStart = 0.0, spanEnd = 0.0;
    for (auto id : clipSelection_.getSelected()) {
        const auto* clip = doc_->getClip(id);
        const auto* track = doc_->getTrackForClip(id);
        if (clip == nullptr || track == nullptr)
            continue;
        sources.push_back({id, track->id, clip->startBeat});
        const double end = clip->startBeat + clip->lengthBeats;
        spanStart = haveSpan ? std::min(spanStart, clip->startBeat) : clip->startBeat;
        spanEnd = haveSpan ? std::max(spanEnd, end) : end;
        haveSpan = true;
    }
    if (!haveSpan || !(spanEnd > spanStart))
        return false;

    const double blockLength = spanEnd - spanStart;

    // "Automation follows clips": every tiled copy carries its source's span, one batched transfer.
    std::vector<AutomationSpanEdit> spanEdits;
    if (clipLaneArea_.isAutomationFollowsClips())
        for (int repeat = 1; repeat <= count; ++repeat)
            for (const auto& source : sources)
                if (auto spanEdit =
                        automationSpanEditForClip(*doc_, source.id, AutomationSpanEdit::Kind::Copy, source.track,
                                                  source.startBeat + (double)repeat * blockLength))
                    spanEdits.push_back(*spanEdit);

    std::vector<synth::ClipId> newIds;
    auto mutate = [this, sources, blockLength, count, spanEdits, &newIds] {
        for (int repeat = 1; repeat <= count; ++repeat) {
            for (const auto& source : sources) {
                const auto dup = doc_->duplicateClip(source.id);
                if (!dup.isValid())
                    continue;
                // duplicateClip drops the copy one clip-length after its source; moving it to
                // (its own start + n block lengths) is what tiles the whole selection forward.
                // Same track by construction, so the kind check never engages.
                doc_->moveClipToTrack(dup, source.track, source.startBeat + (double)repeat * blockLength);
                newIds.push_back(dup);
            }
        }
        applyAutomationSpanEdits(*doc_, spanEdits);
    };

    if (!automationSpanEditsFit(*doc_, spanEdits))
        return refuseForAutomation();
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    if (newIds.empty())
        return false;

    clipSelection_.setSelection(newIds);
    clipLaneArea_.repaint();
    return true;
}

//==============================================================================
// ---- Range clipboard ----
//
// A range copy fills the SAME clipClipboard_ a clip copy does, so paste (kind-aware target rows,
// snapping, the one undo step, the pasted clips ending up selected) is shared rather than written
// twice. Each entry is the part of a clip inside the range (TimelineDoc::clipToRange — notes
// clipped, an audio fragment's sourceStartSeconds advanced by what was cut off its left, a fade at
// a cut edge zeroed), and its relative start is measured from the RANGE start rather than from the
// earliest clip: leading empty space in the range is part of what was copied, so a paste lands
// the content where it sat inside the range.

bool TimelinePanelComponent::hasRangeSelection() const { return clipLaneArea_.getRangeSpan().has_value(); }

bool TimelinePanelComponent::copyRange() {
    if (doc_ == nullptr)
        return false;
    const auto span = clipLaneArea_.getRangeSpan();
    if (!span)
        return false;
    const double spb = clipLaneArea_.secondsPerBeat();

    std::vector<ClipboardClip> captured;
    for (const auto trackId : span->tracks) {
        const auto* track = doc_->getTrack(trackId);
        if (track == nullptr)
            continue;
        for (const auto& clip : track->clips) {
            const auto fragment = synth::TimelineDoc::clipToRange(clip, span->startBeat, span->endBeat, spb);
            if (!fragment)
                continue;
            ClipboardClip entry;
            entry.originalTrack = track->id;
            entry.requiredKind = fragment->assetRef.isNotEmpty() ? synth::TrackKind::Audio : synth::TrackKind::Midi;
            entry.relativeStartBeat = fragment->startBeat - span->startBeat;
            entry.lengthBeats = fragment->lengthBeats;
            entry.name = fragment->name;
            entry.notes = fragment->notes;
            entry.muted = fragment->muted;
            entry.assetRef = fragment->assetRef;
            entry.gainDb = fragment->gainDb;
            entry.fadeInBeats = fragment->fadeInBeats;
            entry.fadeOutBeats = fragment->fadeOutBeats;
            entry.sourceStartSeconds = fragment->sourceStartSeconds;
            captured.push_back(std::move(entry));
        }
    }
    if (captured.empty())
        return false; // a range over empty time copies nothing, and leaves the clipboard alone

    clipClipboard_ = std::move(captured);
    return true;
}

// The delete half is the lane area's own verb, so a cut and a Delete of the same range can never
// remove different things. applyRangeChoice records exactly one undo step; the clipboard (filled
// before it) survives an undo of the cut.
bool TimelinePanelComponent::cutRange() {
    if (!copyRange())
        return false;
    clipLaneArea_.applyRangeChoice(TimelineClipLaneArea::RangeChoice::Delete);
    return true;
}

} // namespace synth::ui
