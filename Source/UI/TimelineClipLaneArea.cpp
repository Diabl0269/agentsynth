#include "TimelineClipLaneArea.h"
#include "../AppUndoManager.h"
#include "../Transport/TransportService.h"
#include "Theme/AppLookAndFeel.h"
#include "TimelineTrackHeaderComponent.h"
#include "TrackColour.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace synth::ui {

namespace {
// Edge-drag zones (right = resize length, left = move+resize keeping the end fixed).
constexpr int kEdgeZonePx = 6;

// TimelineDoc has no explicit minimum clip length; this mirrors TimelineViewState::Snap's own
// finest grid (Sixteenth = 1/16 beat, the same unit TransportService's kMinLoopLengthBeats uses)
// so a right-edge trim can never collapse a clip past what the grid itself can represent.
constexpr double kMinClipLengthBeats = 0.0625;

constexpr int kMinWidthForName = 40;
constexpr int kMinWidthForNotePreview = 24;
} // namespace

//==============================================================================
TimelineClipLaneArea::TimelineClipLaneArea(TimelineViewState& viewState, ClipSelectionModel& selection)
    : viewState_(viewState)
    , selection_(selection) {
    setComponentID("timelineClipLaneArea");
    setInterceptsMouseClicks(true, false);
}

//==============================================================================
void TimelineClipLaneArea::setTimelineDoc(synth::TimelineDoc* doc) {
    doc_ = doc;
    refreshFromDoc();
}

void TimelineClipLaneArea::refreshFromDoc() {
    std::vector<synth::ClipId> alive;
    if (doc_ != nullptr) {
        for (const auto& track : doc_->getTracks())
            for (const auto& clip : track.clips)
                alive.push_back(clip.id);
    }
    selection_.retainOnly(alive);
    repaint();
}

int TimelineClipLaneArea::getRowHeight() const {
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        return lf->getTheme().metrics.timelineTrackRowHeight;
    return TimelineTrackHeaderComponent::kRowHeight;
}

double TimelineClipLaneArea::currentBeatsPerBar() const {
    double beatsPerBar = 4.0;
    if (transport_ != nullptr) {
        const auto snap = transport_->getPositionSnapshot();
        const double tsBeatsPerBar = (double)snap.timeSigNumerator * 4.0 / (double)std::max(1, snap.timeSigDenominator);
        if (tsBeatsPerBar > 0.0)
            beatsPerBar = tsBeatsPerBar;
    }
    return beatsPerBar;
}

double TimelineClipLaneArea::snappedBeatAt(double rawBeat) const {
    return viewState_.snapBeat(rawBeat, currentBeatsPerBar());
}

//==============================================================================
juce::Rectangle<int> TimelineClipLaneArea::computeClipRect(const TimelineViewState& viewState, int trackIndex,
                                                           double startBeat, double lengthBeats, int rowHeight) {
    const double x0 = viewState.beatToX(startBeat);
    const double x1 = viewState.beatToX(startBeat + lengthBeats);
    const int left = (int)std::llround(x0);
    const int right = (int)std::llround(x1);
    return {left, trackIndex * rowHeight, std::max(right - left, 1), rowHeight};
}

juce::Rectangle<int> TimelineClipLaneArea::getClipRect(synth::ClipId id) const {
    if (doc_ == nullptr)
        return {};
    const int rowHeight = getRowHeight();
    const auto& tracks = doc_->getTracks();
    for (int trackIndex = 0; trackIndex < (int)tracks.size(); ++trackIndex)
        for (const auto& clip : tracks[(size_t)trackIndex].clips)
            if (clip.id == id)
                return computeClipRect(viewState_, trackIndex, clip.startBeat, clip.lengthBeats, rowHeight);
    return {};
}

std::vector<std::pair<synth::ClipId, juce::Rectangle<int>>> TimelineClipLaneArea::collectClipRects() const {
    std::vector<std::pair<synth::ClipId, juce::Rectangle<int>>> rects;
    if (doc_ == nullptr)
        return rects;
    const int rowHeight = getRowHeight();
    const auto& tracks = doc_->getTracks();
    for (int trackIndex = 0; trackIndex < (int)tracks.size(); ++trackIndex)
        for (const auto& clip : tracks[(size_t)trackIndex].clips)
            rects.emplace_back(clip.id,
                               computeClipRect(viewState_, trackIndex, clip.startBeat, clip.lengthBeats, rowHeight));
    return rects;
}

std::optional<TimelineClipLaneArea::ClipHit> TimelineClipLaneArea::hitTestClip(juce::Point<int> pos) const {
    if (doc_ == nullptr)
        return std::nullopt;

    const int rowHeight = getRowHeight();
    const auto& tracks = doc_->getTracks();
    for (int trackIndex = 0; trackIndex < (int)tracks.size(); ++trackIndex) {
        for (const auto& clip : tracks[(size_t)trackIndex].clips) {
            const auto rect = computeClipRect(viewState_, trackIndex, clip.startBeat, clip.lengthBeats, rowHeight);
            if (!rect.contains(pos))
                continue;

            ClipHit hit{clip.id, rect, ClipHit::Zone::Body};
            if (pos.x <= rect.getX() + kEdgeZonePx)
                hit.zone = ClipHit::Zone::LeftEdge;
            else if (pos.x >= rect.getRight() - kEdgeZonePx)
                hit.zone = ClipHit::Zone::RightEdge;
            return hit;
        }
    }
    return std::nullopt;
}

TimelineClipLaneArea::Geometry TimelineClipLaneArea::effectiveGeometryFor(const synth::Clip& clip) const {
    if (dragMode_ == DragMode::Move) {
        for (const auto& origin : dragClips_)
            if (origin.id == clip.id)
                return {origin.originalStart + previewDeltaBeats_, origin.lengthBeats};
    } else if (dragMode_ == DragMode::ResizeRight && activeClip_ == clip.id) {
        return {resizeOriginalStart_, previewLength_};
    } else if (dragMode_ == DragMode::ResizeLeft && activeClip_ == clip.id) {
        return {previewStart_, previewLength_};
    }
    return {clip.startBeat, clip.lengthBeats};
}

//==============================================================================
void TimelineClipLaneArea::paint(juce::Graphics& g) {
    if (doc_ == nullptr)
        return;

    const int rowHeight = getRowHeight();
    const auto& tracks = doc_->getTracks();
    for (int trackIndex = 0; trackIndex < (int)tracks.size(); ++trackIndex) {
        const auto& track = tracks[(size_t)trackIndex];
        for (const auto& clip : track.clips)
            paintClip(g, clip, track, trackIndex, rowHeight);
    }

    if (dragMode_ == DragMode::Marquee)
        paintMarquee(g);
}

void TimelineClipLaneArea::paintClip(juce::Graphics& g, const synth::Clip& clip, const synth::Track& track,
                                     int trackIndex, int rowHeight) {
    const auto geometry = effectiveGeometryFor(clip);
    const auto rect = computeClipRect(viewState_, trackIndex, geometry.start, geometry.length, rowHeight);
    if (rect.getRight() < 0 || rect.getX() > getWidth())
        return; // cheap offscreen cull — same reasoning as the panel's own bar-line loop

    const bool selected = selection_.contains(clip.id);
    const juce::Colour base = synth::ui::resolveTrackColour(track.colourArgb, trackIndex, track.muted);
    const juce::Colour fill = selected ? base.brighter(0.15f) : base; // "slight fill lift" when selected

    const auto bodyBounds = rect.toFloat().reduced(1.0f);
    g.setColour(fill.withAlpha(selected ? 0.85f : 0.65f));
    g.fillRoundedRectangle(bodyBounds, 3.0f);

    g.setColour(selected ? base.brighter(0.6f) : base.darker(0.3f));
    g.drawRoundedRectangle(bodyBounds, 3.0f, selected ? 2.0f : 1.0f);

    if (rect.getWidth() > kMinWidthForNotePreview) {
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        for (const auto& note : clip.notes) {
            const double noteStartBeat = geometry.start + note.startBeat; // notes are clip-relative
            const double x0 = viewState_.beatToX(noteStartBeat);
            const double x1 = viewState_.beatToX(noteStartBeat + note.lengthBeats);
            const auto nx0 = (float)juce::jlimit((double)rect.getX(), (double)rect.getRight(), x0);
            const auto nx1 = (float)juce::jlimit((double)rect.getX(), (double)rect.getRight(), x1);
            if (nx1 <= nx0)
                continue;

            const float pitchFrac = 1.0f - (float)juce::jlimit(0, 127, note.pitch) / 127.0f;
            const float y = (float)rect.getY() + 2.0f + pitchFrac * (float)(rowHeight - 4);
            g.drawLine(nx0, y, nx1, y, 1.2f);
        }
    }

    if (rect.getWidth() > kMinWidthForName && clip.name.isNotEmpty()) {
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.setFont(juce::Font(11.0f));
        g.drawText(clip.name, rect.reduced(4, 2), juce::Justification::topLeft, true);
    }
}

void TimelineClipLaneArea::paintMarquee(juce::Graphics& g) {
    if (marqueeRect_.isEmpty())
        return;
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.fillRect(marqueeRect_);
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.drawRect(marqueeRect_, 1);
}

//==============================================================================
void TimelineClipLaneArea::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    dragMode_ = DragMode::None;
    pendingEmptyClick_ = false;

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
        // Move: same-track only in v1 — the drag is a pure horizontal beat offset, and nothing
        // here ever reassigns a clip's track. Snapshot every SELECTED clip's origin (not just the
        // one grabbed) so a multi-selection moves together by one shared delta.
        dragMode_ = DragMode::Move;
        dragClips_.clear();
        for (auto id : selection_.getSelected())
            if (const auto* clip = doc_->getClip(id))
                dragClips_.push_back({id, clip->startBeat, clip->lengthBeats});
        previewDeltaBeats_ = 0.0;
    }

    repaint();
}

void TimelineClipLaneArea::mouseDrag(const juce::MouseEvent& e) {
    if (pendingEmptyClick_) {
        // A plain press on empty space that becomes a drag turns into a (non-additive) marquee —
        // there is no drag-to-pan gesture here (scrolling is wheel-only, TL5-2).
        pendingEmptyClick_ = false;
        beginMarquee(mouseDownPos_, false);
    }

    if (dragMode_ == DragMode::Marquee) {
        updateMarquee(e.getPosition());
        return;
    }

    if (dragMode_ == DragMode::None || doc_ == nullptr)
        return;

    const double deltaBeats = (double)(e.getPosition().x - mouseDownPos_.x) / viewState_.pixelsPerBeat;
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

    repaint();
}

void TimelineClipLaneArea::mouseUp(const juce::MouseEvent& e) {
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
        if (dragMode_ == DragMode::Move && std::abs(previewDeltaBeats_) > 1e-9) {
            const auto clips = dragClips_;
            const double delta = previewDeltaBeats_;
            auto mutate = [this, clips, delta] {
                for (const auto& origin : clips)
                    doc_->moveClip(origin.id, origin.originalStart + delta);
            };
            if (undoManager_)
                undoManager_->recordTimelineChange(*doc_, mutate);
            else
                mutate();
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
            // deferred; see the class-level TL5-7 note) and resizes it so the end stays fixed.
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
    repaint();
}

void TimelineClipLaneArea::mouseMove(const juce::MouseEvent& e) {
    auto hit = hitTestClip(e.getPosition());
    if (hit && (hit->zone == ClipHit::Zone::LeftEdge || hit->zone == ClipHit::Zone::RightEdge))
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

//==============================================================================
bool TimelineClipLaneArea::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (selection_.isEmpty())
            return false;
        selection_.clear();
        repaint();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        auto ids = selection_.getSelected();
        if (ids.empty() || doc_ == nullptr)
            return false;

        auto mutate = [this, ids] {
            for (auto id : ids)
                doc_->removeClip(id);
        };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();

        selection_.clear();
        repaint();
        return true;
    }

    return false;
}

//==============================================================================
void TimelineClipLaneArea::beginMarquee(juce::Point<int> anchor, bool additive) {
    dragMode_ = DragMode::Marquee;
    marqueeAdditive_ = additive;
    marqueeAnchor_ = anchor;
    marqueeRect_ = juce::Rectangle<int>(anchor, anchor);
    marqueeBaseSelection_ = additive ? selection_.getSelected() : std::vector<synth::ClipId>{};

    // A non-additive marquee starts from nothing, so dragging over no clip deselects.
    if (!additive)
        selection_.clear();
    repaint();
}

void TimelineClipLaneArea::updateMarquee(juce::Point<int> current) {
    marqueeRect_ = juce::Rectangle<int>(marqueeAnchor_, current);
    auto hits = clipHitTestMarquee(marqueeRect_, collectClipRects());

    if (marqueeAdditive_) {
        std::set<synth::ClipId> merged(marqueeBaseSelection_.begin(), marqueeBaseSelection_.end());
        merged.insert(hits.begin(), hits.end());
        selection_.setSelection({merged.begin(), merged.end()});
    } else {
        selection_.setSelection(hits);
    }
    repaint();
}

void TimelineClipLaneArea::endMarquee() {
    marqueeRect_ = {};
    marqueeBaseSelection_.clear();
    marqueeAdditive_ = false;
    repaint();
}

//==============================================================================
void TimelineClipLaneArea::showClipContextMenu(synth::ClipId id, juce::Point<int> localPos) {
    if (doc_ == nullptr)
        return;
    const auto* clip = doc_->getClip(id);
    if (clip == nullptr)
        return;

    const double pointerBeat = viewState_.xToBeat((double)localPos.x);
    const double snappedPointer = snappedBeatAt(pointerBeat);
    const bool strictlyInside =
        snappedPointer > clip->startBeat && snappedPointer < clip->startBeat + clip->lengthBeats;

    juce::PopupMenu menu;
    juce::PopupMenu::Item split("Split at pointer");
    split.setEnabled(strictlyInside);
    split.action = [this, id, pointerBeat] {
        applyClipContextChoice(id, ClipContextChoice::SplitAtPointer, pointerBeat);
    };
    menu.addItem(split);
    menu.addItem("Duplicate", [this, id] { applyClipContextChoice(id, ClipContextChoice::Duplicate, 0.0); });
    menu.addItem("Delete", [this, id] { applyClipContextChoice(id, ClipContextChoice::Delete, 0.0); });

    menu.showMenuAsync(juce::PopupMenu::Options());
}

void TimelineClipLaneArea::applyClipContextChoice(synth::ClipId id, ClipContextChoice choice, double pointerBeat) {
    if (doc_ == nullptr)
        return;

    switch (choice) {
    case ClipContextChoice::SplitAtPointer: {
        const auto* clip = doc_->getClip(id);
        if (clip == nullptr)
            return;
        const double snappedPointer = snappedBeatAt(pointerBeat);
        const double atBeat = snappedPointer - clip->startBeat; // clip-relative, per splitClip's contract
        if (!(atBeat > 0.0 && atBeat < clip->lengthBeats))
            return; // must land strictly inside

        auto mutate = [this, id, atBeat] { doc_->splitClip(id, atBeat); };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
        break;
    }
    case ClipContextChoice::Duplicate: {
        synth::ClipId newId;
        auto mutate = [this, id, &newId] { newId = doc_->duplicateClip(id); };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
        if (newId.isValid())
            selection_.setSelection({newId});
        break;
    }
    case ClipContextChoice::Delete: {
        auto mutate = [this, id] { doc_->removeClip(id); };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
        break;
    }
    }

    repaint();
}

} // namespace synth::ui
