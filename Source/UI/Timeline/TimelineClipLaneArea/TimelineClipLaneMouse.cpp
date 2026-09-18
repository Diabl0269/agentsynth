// TimelineClipLaneMouse.cpp
//
// Mouse handling: mouseDown/mouseDrag/mouseUp/mouseDoubleClick, drag-preview and
// auto-scroll updates, double-click clip creation, and file drag/drop. TimelineClipLaneArea
// is declared in TimelineClipLaneArea.h; sibling TimelineClipLane*.cpp files in this
// directory hold the rest of the class.

#include "TimelineClipLaneArea.h"
#include "TimelineClipLaneInternal.h"

#include "AppUndoManager.h"
#include "Transport/TransportService.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
// The fastest an edge-drag can scroll the view, in pixels per kEdgeScrollHz tick (at the pointer
// pinned to or beyond the component's edge). Chosen to feel brisk without outrunning the eye at a
// typical zoom — the same "fast but still readable" target TimelinePlayheadOverlay's 30 Hz line
// aims for.
constexpr double kEdgeAutoScrollMaxPxPerTick = 18.0;

// Whether a double-click inside the loop locators authors a clip spanning them instead of the
// one-bar default. Duplicated as a string rather than shared with PreferencesSettingsTab (which
// WRITES it) for the same reason "timelineLoopSelectionArms" is: a one-line string constant is not
// worth a header dependency between a settings tab and a lanes view. DEFAULT TRUE — see
// TimelineClipLaneArea::locatorSpanForDoubleClick.
constexpr const char* kTimelineDoubleClickSpansLocatorsKey = "timelineDoubleClickSpansLocators";
} // namespace

using namespace detail;

//==============================================================================
void TimelineClipLaneArea::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    dragMode_ = DragMode::None;
    pendingEmptyClick_ = false;
    // The beat under the pointer right now — every Move/Resize drag anchors its delta to THIS,
    // not to the pixel it was pressed at, so the drag stays correct if the view scrolls mid-drag
    // (see mouseDownBeat_'s comment).
    mouseDownBeat_ = viewState_.xToBeat((double)e.getPosition().x);
    lastDragPointer_ = e.getPosition();

    if (e.mods.isPopupMenu()) {
        auto hit = hitTestClip(e.getPosition());
        if (!hit)
            return; // empty-space right-click: no menu, selection untouched (GraphEditor's rule)

        if (!selection_.contains(hit->id))
            selection_.setSelection({hit->id});
        repaint();
        showClipContextMenu(hit->id, e.getPosition());
        return;
    }

    if (!e.mods.isLeftButtonDown())
        return;

    // Every non-Select tool is a click action (Draw's drag included): none of them selects,
    // marquees or trims, so they never reach the pointer logic below.
    if (activeTool_ != EditTool::Select) {
        handleToolMouseDown(e);
        return;
    }

    auto hit = hitTestClip(e.getPosition());
    if (!hit) {
        mouseDownPos_ = e.getPosition();
        if (e.mods.isShiftDown())
            beginMarquee(e.getPosition(), true);
        else
            pendingEmptyClick_ = true; // deferred — see class comment
        return;
    }

    const bool additive = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
    if (additive) {
        selection_.toggle(hit->id);
        repaint();
        return; // a modifier-click edits the selection; it never begins a drag
    }

    // A plain click on an already-selected clip keeps the whole group intact so it can be dragged
    // together; clicking anything else collapses the selection onto it (ModuleComponent's rule).
    if (!selection_.contains(hit->id))
        selection_.setSelection({hit->id});

    mouseDownPos_ = e.getPosition();
    activeClip_ = hit->id;

    // hitTestClip() only ever returns a hit when doc_ is non-null, so doc_ is guaranteed live here.
    if (hit->zone == ClipHit::Zone::RightEdge) {
        if (const auto* clip = doc_->getClip(hit->id)) {
            dragMode_ = DragMode::ResizeRight;
            resizeOriginalStart_ = clip->startBeat;
            resizeOriginalLength_ = clip->lengthBeats;
            previewLength_ = resizeOriginalLength_;
        }
    } else if (hit->zone == ClipHit::Zone::LeftEdge) {
        if (const auto* clip = doc_->getClip(hit->id)) {
            dragMode_ = DragMode::ResizeLeft;
            resizeOriginalStart_ = clip->startBeat;
            resizeOriginalLength_ = clip->lengthBeats;
            previewStart_ = resizeOriginalStart_;
            previewLength_ = resizeOriginalLength_;
        }
    } else {
        // Move: a shared horizontal beat offset PLUS a shared track-row offset (see mouseDrag).
        // Snapshot every SELECTED clip's origin (not just the one grabbed) so a multi-selection
        // moves together by one delta rather than each clip snapping independently.
        //
        // Alt turns the whole gesture into a copy-drag: the originals stay exactly where they are
        // (in the doc AND on screen) and the release commits duplicates at the destination. There
        // is deliberately no Alt-click action — a copy of a clip on top of itself is not something
        // anyone asks for by clicking.
        dragMode_ = DragMode::Move;
        copyDrag_ = e.mods.isAltDown();
        dragClips_.clear();
        const auto& tracks = doc_->getTracks();
        for (auto id : selection_.getSelected()) {
            const auto* clip = doc_->getClip(id);
            if (clip == nullptr)
                continue;
            int trackIndex = 0;
            for (int i = 0; i < (int)tracks.size(); ++i)
                for (const auto& candidate : tracks[(std::size_t)i].clips)
                    if (candidate.id == id)
                        trackIndex = i;
            dragClips_.push_back({id, clip->startBeat, clip->lengthBeats, trackIndex});
        }
        previewDeltaBeats_ = 0.0;
        previewRowDelta_ = 0;
    }

    repaint();
}

void TimelineClipLaneArea::mouseDrag(const juce::MouseEvent& e) {
    if (dragMode_ == DragMode::Draw) {
        updateDrawGesture(e);
        return;
    }
    // Split/Glue/Erase/Mute already acted on the press; dragging one of them does nothing at all
    // (no marquee, no move) rather than something the icon never promised.
    if (activeTool_ != EditTool::Select)
        return;

    if (pendingEmptyClick_) {
        // A plain press on empty space that becomes a drag turns into a (non-additive) marquee —
        // there is no drag-to-pan gesture here (scrolling is wheel-only).
        pendingEmptyClick_ = false;
        beginMarquee(mouseDownPos_, false);
    }

    if (dragMode_ == DragMode::Marquee) {
        updateMarquee(e.getPosition());
        return;
    }

    if (dragMode_ == DragMode::None || doc_ == nullptr)
        return;

    lastDragPointer_ = e.getPosition();
    updateDragPreviewFromLastPointer();
    updateAutoScrollArming();
    repaint();
}

// Runs against the (possibly just-scrolled) view state, factored out of mouseDrag() so a real
// pointer move and an auto-scroll tick (which has no MouseEvent of its own) can't drift apart.
void TimelineClipLaneArea::updateDragPreviewFromLastPointer() {
    if (dragMode_ == DragMode::None || doc_ == nullptr)
        return;

    // BEAT-anchored, not pixel-anchored: the delta is xToBeat(current x) - xToBeat(the beat the
    // pointer was over at mouseDown), so a view scroll that happens mid-drag (an edge-scroll tick,
    // or in principle any other scroll) is baked into xToBeat's OWN firstVisibleBeat term rather
    // than silently invalidating a pixel-space delta computed against the OLD scroll position. It
    // also happens to fix the same latent drift under a mid-drag zoom change, for the same reason.
    const double deltaBeats = viewState_.xToBeat((double)lastDragPointer_.x) - mouseDownBeat_;
    const double beatsPerBar = currentBeatsPerBar();

    if (dragMode_ == DragMode::Move) {
        double anchorOriginal = 0.0;
        for (const auto& origin : dragClips_)
            if (origin.id == activeClip_)
                anchorOriginal = origin.originalStart;

        const double snappedAnchorStart = viewState_.snapBeat(anchorOriginal + deltaBeats, beatsPerBar);
        double delta = snappedAnchorStart - anchorOriginal;

        // Clamp so no dragged clip's start goes negative — the whole group is held back together
        // rather than letting the front of the pack clip at 0 while the rest keep sliding.
        double minOriginal = 0.0;
        bool first = true;
        for (const auto& origin : dragClips_) {
            if (first || origin.originalStart < minOriginal)
                minOriginal = origin.originalStart;
            first = false;
        }
        delta = std::max(delta, -minOriginal);

        previewDeltaBeats_ = delta;

        // Vertical: one row delta for the WHOLE selection, legal only if every clip's destination
        // row exists and accepts its payload (TimelineDoc::moveClipToTrack's kind rule — an audio
        // clip onto an Audio row, a MIDI clip onto a Midi one, neither onto Automation). An
        // illegal drop clamps back to 0 — a same-lane move, i.e. exactly what this drag did before
        // it could cross tracks — rather than dropping the clips that would have fitted.
        // Clamping the whole group rather than dropping just the clips that would fit is
        // deliberate: a partial drop would silently tear a selection apart.
        const int rowHeight = getRowHeight();
        int rowDelta =
            rowHeight > 0 ? (int)std::llround((double)(lastDragPointer_.y - mouseDownPos_.y) / (double)rowHeight) : 0;
        if (rowDelta != 0) {
            const auto& tracks = doc_->getTracks();
            for (const auto& origin : dragClips_) {
                const int destRow = origin.trackIndex + rowDelta;
                const auto* clip = doc_->getClip(origin.id);
                if (clip == nullptr || !juce::isPositiveAndBelow(destRow, (int)tracks.size())) {
                    rowDelta = 0;
                    break;
                }
                const auto destKind = tracks[(std::size_t)destRow].kind;
                const auto neededKind = clip->assetRef.isNotEmpty() ? synth::TrackKind::Audio : synth::TrackKind::Midi;
                if (destKind != neededKind) {
                    rowDelta = 0;
                    break;
                }
            }
        }
        previewRowDelta_ = rowDelta;
    } else if (dragMode_ == DragMode::ResizeRight) {
        const double rawEnd = resizeOriginalStart_ + resizeOriginalLength_ + deltaBeats;
        const double snappedEnd = viewState_.snapBeat(rawEnd, beatsPerBar);
        previewLength_ = std::max(snappedEnd - resizeOriginalStart_, kMinClipLengthBeats);
    } else if (dragMode_ == DragMode::ResizeLeft) {
        const double end = resizeOriginalStart_ + resizeOriginalLength_;
        const double rawStart = resizeOriginalStart_ + deltaBeats;
        const double snappedStart = viewState_.snapBeat(rawStart, beatsPerBar);
        previewStart_ = juce::jlimit(0.0, end - kMinClipLengthBeats, snappedStart);
        previewLength_ = end - previewStart_;
    }
}

// Starts the timer only while a Move/Resize drag is live AND the pointer sits inside an edge zone
// of this component's width; stopped the moment either condition stops holding — mouseUp (see
// mouseUp), a tool switch cancelling the drag (see setActiveTool), or the pointer dragging back
// into the dead middle band (see autoScrollTick).
void TimelineClipLaneArea::updateAutoScrollArming() {
    const bool dragging =
        dragMode_ == DragMode::Move || dragMode_ == DragMode::ResizeLeft || dragMode_ == DragMode::ResizeRight;
    // maxPerTick=1.0 here only to probe zero-vs-nonzero — the real magnitude is read again inside
    // autoScrollTick() (which needs the SIGNED value, not just "is it armed").
    const bool insideEdgeZone =
        dragging && edgeScrollVelocity(lastDragPointer_.x, 0, getWidth(), kEdgeZonePx, 1.0) != 0.0;
    if (insideEdgeZone && !isTimerRunning())
        startTimer(1000 / kEdgeScrollHz);
    else if (!insideEdgeZone && isTimerRunning())
        stopTimer();
}

// One tick scrolls viewState_ by edgeScrollVelocity(...)/pixelsPerBeat beats, re-derives the drag
// preview from the LAST known pointer position (mouseDrag never re-fires on its own), and repaints
// — mirroring TimelinePlayheadOverlay::timerCallback's protected-for-tests pattern.
void TimelineClipLaneArea::autoScrollTick() {
    // The drag can have ended (mouseUp) or moved out of the zone since the last arming check
    // without another tick having run updateAutoScrollArming() itself — re-check both here rather
    // than trusting the timer's own "it was armed a tick ago" state.
    const bool dragging =
        dragMode_ == DragMode::Move || dragMode_ == DragMode::ResizeLeft || dragMode_ == DragMode::ResizeRight;
    if (!dragging) {
        stopTimer();
        return;
    }

    const double velocityPxPerTick =
        edgeScrollVelocity(lastDragPointer_.x, 0, getWidth(), kEdgeZonePx, kEdgeAutoScrollMaxPxPerTick);
    if (velocityPxPerTick == 0.0) {
        stopTimer(); // the pointer drifted back into the dead middle band
        return;
    }
    if (viewState_.pixelsPerBeat <= 0.0)
        return;

    viewState_.scrollBeats(velocityPxPerTick / viewState_.pixelsPerBeat);
    // The pointer hasn't moved (no MouseEvent fired this tick) — the view did, so the preview has
    // to be re-derived against the NEW firstVisibleBeat from the same last-known pointer position.
    updateDragPreviewFromLastPointer();
    if (onViewScrolledByDrag)
        onViewScrolledByDrag();
    repaint();
}

void TimelineClipLaneArea::mouseUp(const juce::MouseEvent& e) {
    // The release always ends whatever drag was in flight, so the edge-scroll timer never outlives
    // it — stopped unconditionally rather than only from the Move/Resize branches below, since a
    // marquee/pendingEmptyClick release reaches this point too and the timer must not care which.
    stopTimer();

    if (dragMode_ == DragMode::Draw) {
        commitDrawGesture();
        return;
    }
    // The four click tools committed on the press; there is nothing left for the release to do
    // (and nothing of theirs to reset — they never entered a drag mode).
    if (activeTool_ != EditTool::Select)
        return;

    if (dragMode_ == DragMode::Marquee) {
        endMarquee();
        dragMode_ = DragMode::None;
        pendingEmptyClick_ = false;
        return;
    }

    if (pendingEmptyClick_) {
        pendingEmptyClick_ = false;
        if (!e.mods.isPopupMenu()) // right-click keeps the selection (GraphEditor's rule)
            selection_.clear();
        repaint();
        return;
    }

    if (doc_ != nullptr) {
        const bool moved = std::abs(previewDeltaBeats_) > 1e-9 || previewRowDelta_ != 0;
        if (dragMode_ == DragMode::Move && moved) {
            // Destination track ids are resolved BEFORE the mutation: track ids and their order
            // are stable across the clip moves below, but the clip vectors they hold are not.
            const auto clips = dragClips_;
            const double delta = previewDeltaBeats_;
            const int rowDelta = previewRowDelta_;
            const bool copying = copyDrag_;
            std::vector<synth::TrackId> destTracks;
            destTracks.reserve(clips.size());
            const auto& tracks = doc_->getTracks();
            for (const auto& origin : clips) {
                const int destRow = origin.trackIndex + rowDelta;
                destTracks.push_back(juce::isPositiveAndBelow(destRow, (int)tracks.size())
                                         ? tracks[(std::size_t)destRow].id
                                         : synth::TrackId{});
            }

            std::vector<synth::ClipId> newIds;
            auto mutate = [this, clips, destTracks, delta, copying, &newIds] {
                for (std::size_t i = 0; i < clips.size(); ++i) {
                    const auto& origin = clips[i];
                    const double newStart = origin.originalStart + delta;
                    if (!destTracks[i].isValid())
                        continue;
                    if (!copying) {
                        // moveClipToTrack onto the clip's OWN track is documented to behave
                        // exactly like moveClip, so the same-lane drag is unchanged by this path.
                        doc_->moveClipToTrack(origin.id, destTracks[i], newStart);
                        continue;
                    }
                    // duplicateClip drops the copy immediately after its source; the move is what
                    // puts it where the user actually dropped it.
                    const auto dup = doc_->duplicateClip(origin.id);
                    if (!dup.isValid())
                        continue;
                    doc_->moveClipToTrack(dup, destTracks[i], newStart);
                    newIds.push_back(dup);
                }
            };
            if (undoManager_)
                undoManager_->recordTimelineChange(*doc_, mutate);
            else
                mutate();

            // A copy-drag ends with the COPIES selected — the user's attention is on what they
            // just made, and the next drag should move it rather than the original.
            if (copying && !newIds.empty())
                selection_.setSelection(newIds);
        } else if (dragMode_ == DragMode::ResizeRight && std::abs(previewLength_ - resizeOriginalLength_) > 1e-9) {
            const auto id = activeClip_;
            const double newLength = previewLength_;
            auto mutate = [this, id, newLength] { doc_->resizeClip(id, newLength); };
            if (undoManager_)
                undoManager_->recordTimelineChange(*doc_, mutate);
            else
                mutate();
        } else if (dragMode_ == DragMode::ResizeLeft && std::abs(previewStart_ - resizeOriginalStart_) > 1e-9) {
            const auto id = activeClip_;
            const double newStart = previewStart_;
            const double newLength = previewLength_;
            // Two doc calls, ONE undo step: the left edge moves the clip's start (its
            // clip-relative notes travel with it — a divergence from per-note-anchored trimming,
            // deferred) and resizes it so the end stays fixed.
            auto mutate = [this, id, newStart, newLength] {
                doc_->moveClip(id, newStart);
                doc_->resizeClip(id, newLength);
            };
            if (undoManager_)
                undoManager_->recordTimelineChange(*doc_, mutate);
            else
                mutate();
        }
    }

    dragMode_ = DragMode::None;
    dragClips_.clear();
    previewDeltaBeats_ = 0.0;
    previewRowDelta_ = 0;
    copyDrag_ = false;
    repaint();
}

// Double-clicking a clip opens the piano roll for it (onClipDoubleClicked with the hit clip's id).
// Double-clicking EMPTY lane space authors content on the row under the pointer instead: a Midi
// track gets a clip at the floor-snapped beat, selected, and fires onClipDoubleClicked for it too
// (so "double-click empty space" lands straight in the note editor); an Audio track asks for a
// file through audioFileChooser_ and reports the choice as onAudioFileDropped, the same seam a
// file drop uses. An Automation row, and a double-click below the last row, do nothing.
//
// The MIDI clip's span is one bar, EXCEPT when the click lands inside a real loop-locator span and
// the "double-click spans locators" preference is on (default) — it then spans the locators
// exactly. See locatorSpanForDoubleClick for the full set of conditions.
void TimelineClipLaneArea::mouseDoubleClick(const juce::MouseEvent& e) {
    // Authoring double-clicks belong to the pointer. With a tool active the first click already
    // did the tool's job (and Draw already created a clip), so a second one must not also open a
    // roll or a file chooser.
    if (activeTool_ != EditTool::Select)
        return;

    if (auto hit = hitTestClip(e.getPosition())) {
        if (onClipDoubleClicked)
            onClipDoubleClicked(hit->id);
        return;
    }

    // Empty lane space: author content on the row under the pointer (see the per-kind contract
    // above).
    if (doc_ == nullptr)
        return;
    const auto row = trackIndexAt(e.getPosition());
    if (!row)
        return;

    const auto& track = doc_->getTracks()[(std::size_t)*row];
    const double rawBeat = viewState_.xToBeat((double)e.getPosition().x);
    const double startBeat = floorSnappedBeatAt(rawBeat);

    switch (track.kind) {
    case synth::TrackKind::Midi:
        // Preference ON + a real locator span the click landed inside => the new clip spans the
        // locators exactly. Otherwise the one-bar default, unchanged (see
        // locatorSpanForDoubleClick).
        if (const auto span = locatorSpanForDoubleClick(rawBeat))
            createMidiClipAt(track.id, span->first, span->second - span->first);
        else
            createMidiClipAt(track.id, startBeat);
        break;
    case synth::TrackKind::Audio:
        requestAudioFileFor(track.id, startBeat);
        break;
    case synth::TrackKind::Automation:
        break; // no-op: an automation row's content is breakpoints, authored in the lane editor
    }
}

// `clickedBeat` is the RAW (unsnapped) beat under the pointer: snapping first could push a click
// that landed outside the locator span into it (or the reverse), and the question being asked is
// where the user actually clicked.
std::optional<std::pair<double, double>> TimelineClipLaneArea::locatorSpanForDoubleClick(double clickedBeat) const {
    if (transport_ == nullptr)
        return std::nullopt;

    // Read at use time, defaulting to ON: an install that never opens the Preferences tab gets the
    // new behaviour, which is the point of shipping it as the default. Duplicated string key rather
    // than a shared header, exactly like "timelineLoopSelectionArms".
    bool enabled = true;
    if (appProperties_ != nullptr)
        if (auto* settings = appProperties_->getUserSettings())
            enabled = settings->getBoolValue(kTimelineDoubleClickSpansLocatorsKey, true);
    if (!enabled)
        return std::nullopt;

    const auto snap = transport_->getPositionSnapshot();
    // A degenerate span (end <= start) is also what "no locators set yet" looks like — either way
    // there is nothing to span, so the one-bar default stands.
    if (!(snap.loopEndPpq > snap.loopStartPpq))
        return std::nullopt;
    // Half-open on purpose: a click exactly ON the right locator is a click in the bar AFTER the
    // loop, and authoring a locator-length clip there would run past where the user pointed.
    if (!(clickedBeat >= snap.loopStartPpq && clickedBeat < snap.loopEndPpq))
        return std::nullopt;

    return std::make_pair(snap.loopStartPpq, snap.loopEndPpq);
}

// `lengthOverride` unset means the historical ONE BAR at the transport's current time signature (4
// beats with no transport) — the same beatsPerBar the Snap::Bar grid uses, so a bar-snapped clip
// fills exactly one grid cell.
void TimelineClipLaneArea::createMidiClipAt(synth::TrackId track, double startBeat,
                                            std::optional<double> lengthOverride) {
    if (doc_ == nullptr)
        return;
    const auto* trackPtr = doc_->getTrack(track);
    if (trackPtr == nullptr)
        return;

    // A non-positive override is ignored rather than passed to addClip, which would reject it and
    // author nothing.
    const double lengthBeats =
        lengthOverride.has_value() && *lengthOverride > 0.0 ? *lengthOverride : currentBeatsPerBar();
    const juce::String name = "Clip " + juce::String((int)trackPtr->clips.size() + 1);

    synth::ClipId newId;
    auto mutate = [this, track, startBeat, lengthBeats, name, &newId] {
        newId = doc_->addClip(track, startBeat, lengthBeats, name);
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    if (!newId.isValid())
        return; // rejected (the track is at kMaxClipsPerTrack): nothing to select or open

    selection_.setSelection({newId});
    repaint();
    // Straight into the note editor — the whole point of the gesture is that the user does not have
    // to find a second one to start drawing notes.
    if (onClipDoubleClicked)
        onClipDoubleClicked(newId);
}

void TimelineClipLaneArea::requestAudioFileFor(synth::TrackId track, double startBeat) {
    if (!audioFileChooser_ || !onAudioFileDropped)
        return;

    // The real chooser is async, so the callback may outlive this component (a theme reload or a
    // panel rebuild while the dialog is open) — SafePointer, not a raw `this`.
    juce::Component::SafePointer<TimelineClipLaneArea> safe(this);
    audioFileChooser_([safe, track, startBeat](const juce::File& file) {
        if (safe == nullptr || file == juce::File())
            return;
        if (safe->onAudioFileDropped)
            safe->onAudioFileDropped(track, startBeat, file);
    });
}

void TimelineClipLaneArea::launchAudioFileChooser(std::function<void(const juce::File&)> onChosen) {
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Add Audio Clip", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        audioFormats_.getWildcardForAllFormats());
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [onChosen = std::move(onChosen)](const juce::FileChooser& fc) {
                                  const auto file = fc.getResult();
                                  if (file != juce::File() && onChosen)
                                      onChosen(file);
                              });
}

//==============================================================================
bool TimelineClipLaneArea::isReadableAudioFile(const juce::File& file) const {
    return audioFormats_.findFormatForFileExtension(file.getFileExtension()) != nullptr;
}

juce::File TimelineClipLaneArea::firstAudioFileIn(const juce::StringArray& files) const {
    for (const auto& path : files) {
        const juce::File file(path);
        if (isReadableAudioFile(file))
            return file;
    }
    return {};
}

int TimelineClipLaneArea::dropRowFor(const juce::StringArray& files, int x, int y) const {
    if (doc_ == nullptr || firstAudioFileIn(files) == juce::File())
        return -1;
    const auto row = trackIndexAt({x, y});
    if (!row)
        return -1;
    // Audio rows only: a MIDI/Automation row cannot hold an asset, so it neither highlights nor
    // accepts a drop.
    return doc_->getTracks()[(std::size_t)*row].kind == synth::TrackKind::Audio ? *row : -1;
}

void TimelineClipLaneArea::setFileDropRow(int row) {
    if (row == fileDropRow_)
        return; // repaint ONLY on a row change — a drag reports every pixel of movement
    const int rowHeight = getRowHeight();
    const int previous = fileDropRow_;
    fileDropRow_ = row;
    if (previous >= 0)
        repaint(rowBounds(previous, rowHeight));
    if (fileDropRow_ >= 0)
        repaint(rowBounds(fileDropRow_, rowHeight));
}

bool TimelineClipLaneArea::isInterestedInFileDrag(const juce::StringArray& files) {
    return firstAudioFileIn(files) != juce::File();
}

void TimelineClipLaneArea::fileDragMove(const juce::StringArray& files, int x, int y) {
    setFileDropRow(dropRowFor(files, x, y));
}

void TimelineClipLaneArea::fileDragExit(const juce::StringArray& files) {
    juce::ignoreUnused(files);
    setFileDropRow(-1);
}

void TimelineClipLaneArea::filesDropped(const juce::StringArray& files, int x, int y) {
    const int row = dropRowFor(files, x, y);
    setFileDropRow(-1);
    if (row < 0 || doc_ == nullptr || !onAudioFileDropped)
        return;

    const auto file = firstAudioFileIn(files);
    if (file == juce::File())
        return;

    onAudioFileDropped(doc_->getTracks()[(std::size_t)row].id, floorSnappedBeatAt(viewState_.xToBeat((double)x)), file);
}

void TimelineClipLaneArea::mouseMove(const juce::MouseEvent& e) {
    if (activeTool_ != EditTool::Select) {
        // The tool cursor was set once, on the tool change / on entry — a move never rebuilds or
        // re-sets it. The only per-move work any tool does is the Split preview, and even that
        // only repaints when the snapped beat or the hovered clip actually changes.
        if (activeTool_ == EditTool::Split)
            updateSplitPreview(e.getPosition());
        return;
    }

    auto hit = hitTestClip(e.getPosition());
    if (hit && (hit->zone == ClipHit::Zone::LeftEdge || hit->zone == ClipHit::Zone::RightEdge))
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

// mouseEnter re-applies the tool cursor because it is NOT set per mouse-move (see setActiveTool).
void TimelineClipLaneArea::mouseEnter(const juce::MouseEvent&) { applyToolCursor(); }

// mouseExit drops the Split tool's hover preview so a line never survives the pointer leaving the
// lanes.
void TimelineClipLaneArea::mouseExit(const juce::MouseEvent&) { clearToolPreviews(); }

// A theme switch is the only thing that changes what a tool cursor looks like, so it is the only
// thing that pays for rebuilding them.
void TimelineClipLaneArea::lookAndFeelChanged() {
    toolCursorsBuilt_ = false; // re-tinted icons -> different cursor images
    applyToolCursor();
}

} // namespace synth::ui
