// TimelineClipLaneEditTools.cpp
//
// Edit tools: the active tool, its cursor, its gestures (drag/glue/draw/split) and their
// previews, clip renaming, and the clip context menu. TimelineClipLaneArea is declared in
// TimelineClipLaneArea.h; sibling TimelineClipLane*.cpp files in this directory hold the
// rest of the class.

#include "TimelineClipLaneArea.h"

#include "AppUndoManager.h"
#include "UI/Theme/AppLookAndFeel.h"
#include "UI/Timeline/ToolCursors.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
// Half-width of the Split tool's preview line's repaint region. The stroke itself is 1 px; the
// margin is what keeps an antialiased line from leaving a fringe outside the repainted column.
constexpr int kSplitPreviewMarginPx = 2;

// The icon each tool's cursor is rendered from — the SAME already-tinted Drawable the tool
// strip's button paints, which is what keeps cursor and button in sync across themes (see
// synth::ui::makeToolCursor).
synth::theme::Icon iconForTool(synth::ui::EditTool tool) noexcept {
    using synth::theme::Icon;
    switch (tool) {
    case synth::ui::EditTool::Select:
        return Icon::ToolSelect;
    case synth::ui::EditTool::Split:
        return Icon::ToolSplit;
    case synth::ui::EditTool::Glue:
        return Icon::ToolGlue;
    case synth::ui::EditTool::Erase:
        return Icon::ToolErase;
    case synth::ui::EditTool::Mute:
        return Icon::ToolMute;
    case synth::ui::EditTool::Draw:
        return Icon::ToolDraw;
    }
    return Icon::ToolSelect;
}
} // namespace

//==============================================================================
// Edit tools: the active tool, its cursor, its gestures and its previews.
//==============================================================================

void TimelineClipLaneArea::setActiveTool(EditTool tool) {
    if (activeTool_ == tool)
        return;
    activeTool_ = tool;

    // A gesture in flight has no meaning under the new tool — cancel it (nothing is committed,
    // because every commit happens on mouseUp) rather than letting the next release apply the old
    // tool's action.
    dragMode_ = DragMode::None;
    dragClips_.clear();
    previewDeltaBeats_ = 0.0;
    previewRowDelta_ = 0;
    copyDrag_ = false;
    pendingEmptyClick_ = false;
    marqueeRect_ = {};
    clearToolPreviews();
    stopTimer(); // the auto-scroll timer's drag just got cancelled too

    applyToolCursor();
    repaint();
}

void TimelineClipLaneArea::rebuildToolCursors() {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    for (auto tool : kAllEditTools) {
        // getIcon returns nullptr in a headless build (no asset library linked in);
        // makeToolCursor is documented to fall back to a stock cursor for exactly that case.
        std::unique_ptr<juce::Drawable> icon = lf != nullptr ? lf->getIcon(iconForTool(tool)) : nullptr;
        toolCursors_[(std::size_t)tool] = makeToolCursor(tool, icon.get());
    }
    toolCursorsBuilt_ = true;
}

void TimelineClipLaneArea::applyToolCursor() {
    if (activeTool_ == EditTool::Select) {
        // The pointer's cursor is owned by mouseMove (edge zones show a resize cursor); resetting
        // it here just clears whatever tool cursor was showing.
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    if (!toolCursorsBuilt_)
        rebuildToolCursors();
    setMouseCursor(toolCursors_[(std::size_t)activeTool_]);
}

void TimelineClipLaneArea::handleToolMouseDown(const juce::MouseEvent& e) {
    if (activeTool_ == EditTool::Draw) {
        beginDrawGesture(e);
        return;
    }

    // The other four act on a clip and only on a clip: a click on empty lane space with Split or
    // Erase held does nothing at all (it must not fall through to selection either — that would
    // make an "erase" click look like it selected something).
    auto hit = hitTestClip(e.getPosition());
    if (!hit)
        return;

    const double pointerBeat = viewState_.xToBeat((double)e.getPosition().x);
    switch (activeTool_) {
    case EditTool::Split:
        applyClipContextChoice(hit->id, ClipContextChoice::SplitAtPointer, pointerBeat);
        break;
    case EditTool::Glue:
        applyClipContextChoice(hit->id, ClipContextChoice::GlueWithNext, 0.0);
        break;
    case EditTool::Erase:
        applyClipContextChoice(hit->id, ClipContextChoice::Delete, 0.0);
        break;
    case EditTool::Mute:
        applyClipContextChoice(hit->id, ClipContextChoice::ToggleMute, 0.0);
        break;
    case EditTool::Select:
    case EditTool::Draw:
        break; // handled above / never reached
    }
}

synth::ClipId TimelineClipLaneArea::findGlueTarget(synth::ClipId id) const {
    if (doc_ == nullptr)
        return {};
    const auto* clip = doc_->getClip(id);
    const auto* track = doc_->getTrackForClip(id);
    if (clip == nullptr || track == nullptr)
        return {};

    const double end = clip->startBeat + clip->lengthBeats;
    synth::ClipId best;
    double bestStart = 0.0;
    for (const auto& candidate : track->clips) {
        if (candidate.id == id || candidate.startBeat < end - 1e-9)
            continue; // itself, anything before it, and anything overlapping it (joinClips refuses)
        if (!best.isValid() || candidate.startBeat < bestStart) {
            best = candidate.id;
            bestStart = candidate.startBeat;
        }
    }
    return best;
}

//---- Draw ---------------------------------------------------------------------

void TimelineClipLaneArea::beginDrawGesture(const juce::MouseEvent& e) {
    if (doc_ == nullptr)
        return;
    const auto row = trackIndexAt(e.getPosition());
    if (!row)
        return;

    const auto& track = doc_->getTracks()[(std::size_t)*row];
    if (track.kind != synth::TrackKind::Midi)
        return; // an audio row's content is an imported asset and an automation row's is
                // breakpoints — neither is something a pencil can draw

    dragMode_ = DragMode::Draw;
    drawTrack_ = track.id;
    drawRow_ = *row;
    drawAnchorBeat_ = floorSnappedBeatAt(viewState_.xToBeat((double)e.getPosition().x));
    drawEndBeat_ = drawAnchorBeat_;
    drawDragged_ = false;
}

void TimelineClipLaneArea::updateDrawGesture(const juce::MouseEvent& e) {
    // The end is snapped UP so a drag that has entered a cell always includes the whole cell, and
    // floored at one division so the smallest possible drag still makes a usable clip.
    const double raw = viewState_.xToBeat((double)e.getPosition().x);
    const double end = std::max(ceilSnappedBeatAt(raw), drawAnchorBeat_ + minDrawLengthBeats());
    if (drawDragged_ && std::abs(end - drawEndBeat_) < 1e-9)
        return; // same cell: no state change, so no repaint (the ghost is already correct)

    const auto before = getDrawGhostRectForTest();
    drawEndBeat_ = end;
    drawDragged_ = true;
    requestToolPreviewRepaint(before.getUnion(getDrawGhostRectForTest()));
}

void TimelineClipLaneArea::commitDrawGesture() {
    const auto ghost = getDrawGhostRectForTest();
    const bool dragged = drawDragged_ && drawEndBeat_ > drawAnchorBeat_ + 1e-9;
    const auto track = drawTrack_;
    const double anchor = drawAnchorBeat_;
    const double length = drawEndBeat_ - drawAnchorBeat_;

    dragMode_ = DragMode::None;
    drawDragged_ = false;
    drawRow_ = -1;
    drawTrack_ = {};
    if (!ghost.isEmpty())
        requestToolPreviewRepaint(ghost);

    if (doc_ == nullptr || !track.isValid())
        return;

    if (!dragged) {
        // A pencil CLICK is the same authoring gesture the empty-lane double-click already
        // performs — one bar, selected, straight into the note editor.
        createMidiClipAt(track, anchor);
        return;
    }

    const auto* trackPtr = doc_->getTrack(track);
    if (trackPtr == nullptr)
        return;
    const juce::String name = "Clip " + juce::String((int)trackPtr->clips.size() + 1);

    synth::ClipId newId;
    auto mutate = [this, track, anchor, length, name, &newId] { newId = doc_->addClip(track, anchor, length, name); };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    if (!newId.isValid())
        return; // rejected (the track is at kMaxClipsPerTrack)
    selection_.setSelection({newId});
    repaint();
}

//---- Previews -----------------------------------------------------------------

void TimelineClipLaneArea::requestToolPreviewRepaint(juce::Rectangle<int> region) { repaint(region); }

juce::Rectangle<int> TimelineClipLaneArea::splitPreviewBounds(synth::ClipId clip, double beat) const {
    const auto rect = getClipRect(clip);
    if (rect.isEmpty())
        return {};
    const int x = (int)std::llround(viewState_.beatToX(beat));
    return {x - kSplitPreviewMarginPx, rect.getY(), 2 * kSplitPreviewMarginPx + 1, rect.getHeight()};
}

void TimelineClipLaneArea::updateSplitPreview(juce::Point<int> pos) {
    synth::ClipId clip;
    double beat = 0.0;
    if (doc_ != nullptr) {
        if (auto hit = hitTestClip(pos)) {
            if (const auto* c = doc_->getClip(hit->id)) {
                const double snapped = snappedBeatAt(viewState_.xToBeat((double)pos.x));
                // Only a split point strictly inside the clip is previewed — the same test
                // applyClipContextChoice performs before it splits, so the line is never drawn
                // where a click would do nothing.
                if (snapped > c->startBeat && snapped < c->startBeat + c->lengthBeats) {
                    clip = hit->id;
                    beat = snapped;
                }
            }
        }
    }

    if (clip == splitPreviewClip_ && (!clip.isValid() || std::abs(beat - splitPreviewBeat_) < 1e-9))
        return; // THE gate: pointer movement inside one snap cell repaints nothing

    const auto before =
        splitPreviewClip_.isValid() ? splitPreviewBounds(splitPreviewClip_, splitPreviewBeat_) : juce::Rectangle<int>();
    splitPreviewClip_ = clip;
    splitPreviewBeat_ = beat;
    const auto after = clip.isValid() ? splitPreviewBounds(clip, beat) : juce::Rectangle<int>();
    // ONE repaint per change, over the union — so "moved to the next beat" costs exactly one.
    requestToolPreviewRepaint(before.isEmpty() ? after : (after.isEmpty() ? before : before.getUnion(after)));
}

void TimelineClipLaneArea::clearToolPreviews() {
    if (!splitPreviewClip_.isValid())
        return;
    const auto before = splitPreviewBounds(splitPreviewClip_, splitPreviewBeat_);
    splitPreviewClip_ = {};
    splitPreviewBeat_ = 0.0;
    if (!before.isEmpty())
        requestToolPreviewRepaint(before);
}

std::optional<TimelineClipLaneArea::SplitPreview> TimelineClipLaneArea::getSplitPreviewForTest() const {
    if (!splitPreviewClip_.isValid())
        return std::nullopt;
    return SplitPreview{splitPreviewClip_, splitPreviewBeat_};
}

juce::Rectangle<int> TimelineClipLaneArea::getDrawGhostRectForTest() const {
    if (dragMode_ != DragMode::Draw || !drawDragged_ || drawRow_ < 0)
        return {};
    return computeClipRect(viewState_, drawRow_, drawAnchorBeat_, drawEndBeat_ - drawAnchorBeat_, getRowHeight());
}

//---- Inline rename ------------------------------------------------------------

void TimelineClipLaneArea::renameClip(synth::ClipId id, const juce::String& newName) {
    if (doc_ == nullptr)
        return;
    auto mutate = [this, id, newName] { doc_->setClipName(id, newName); };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();
    repaint();
}

void TimelineClipLaneArea::beginRenameClip(synth::ClipId id) {
    if (doc_ == nullptr)
        return;

    // FIRST: any previous rename commits before a new one opens — and it may mutate the doc, so
    // nothing may hold a Clip pointer across it.
    finishRename(true);

    const auto* clip = doc_->getClip(id);
    if (clip == nullptr)
        return;
    const auto rect = getClipRect(id);
    if (rect.isEmpty())
        return;

    renamingClip_ = id;
    renameEditor_ = std::make_unique<juce::TextEditor>("clipRenameEditor");
    renameEditor_->setComponentID("timelineClipRenameEditor");
    renameEditor_->setMultiLine(false);
    renameEditor_->setReturnKeyStartsNewLine(false);
    renameEditor_->setText(clip->name, juce::dontSendNotification);
    renameEditor_->setBounds(rect.reduced(2).withHeight(std::min(20, std::max(12, rect.getHeight() - 4))));
    renameEditor_->onReturnKey = [this] { finishRename(true); };
    renameEditor_->onEscapeKey = [this] { finishRename(false); };
    // Clicking away is a commit, not a cancel — the same reading every in-place rename in this app
    // (and every DAW) has. Escape is the cancel.
    renameEditor_->onFocusLost = [this] { finishRename(true); };
    addAndMakeVisible(*renameEditor_);
    renameEditor_->selectAll();
    renameEditor_->grabKeyboardFocus();
}

void TimelineClipLaneArea::finishRename(bool commit) {
    if (renameEditor_ == nullptr)
        return;
    // Detach FIRST: deleting the editor takes focus away from it, which fires onFocusLost, which
    // re-enters here — and finds a null editor, so it stops.
    auto editor = std::move(renameEditor_);
    const auto id = renamingClip_;
    renamingClip_ = {};
    const juce::String text = editor->getText();
    editor.reset();

    if (commit)
        renameClip(id, text); // setClipName rejects a blank name, keeping the old one
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

    // "Glue with next" is offered whenever a legal join target exists (see findGlueTarget) and
    // greyed out otherwise, rather than hidden — a menu whose items move around is harder to
    // learn than one whose items grey out.
    juce::PopupMenu::Item glue("Glue with next");
    const auto glueTarget = findGlueTarget(id);
    glue.setEnabled(glueTarget.isValid());
    glue.action = [this, id] { applyClipContextChoice(id, ClipContextChoice::GlueWithNext, 0.0); };
    menu.addItem(glue);

    menu.addItem("Duplicate", [this, id] { applyClipContextChoice(id, ClipContextChoice::Duplicate, 0.0); });
    // One toggling item rather than two, labelled for what the click will DO.
    menu.addItem(clip->muted ? "Unmute" : "Mute",
                 [this, id] { applyClipContextChoice(id, ClipContextChoice::ToggleMute, 0.0); });
    // Not a ClipContextChoice: renaming opens an editor rather than mutating, so the headless
    // seam is renameClip() and the enum case is inert (see ClipContextChoice's own comment).
    menu.addItem("Rename...", [this, id] { beginRenameClip(id); });
    menu.addItem("Delete", [this, id] { applyClipContextChoice(id, ClipContextChoice::Delete, 0.0); });

    // Offered for any audio clip (non-empty assetRef) regardless of whether the asset
    // currently resolves — relinking a PRESENT asset (pointing it at a different file) is just as
    // legitimate as fixing a missing one. A callback rather than a ClipContextChoice: relinking
    // needs a host FileChooser + AssetManager import this class doesn't have (see
    // onRelinkAudioRequested's own comment).
    if (clip->assetRef.isNotEmpty() && onRelinkAudioRequested) {
        menu.addSeparator();
        menu.addItem("Relink audio...", [this, id] { onRelinkAudioRequested(id); });
    }

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
        // The id cannot stay selected: a later batched move or Delete would name a freed clip.
        // (TimelinePanelComponent's refreshFromDoc prunes it too; this keeps a lane area driven
        // without a panel — every test, and the Erase tool — equally correct.)
        selection_.remove(id);
        break;
    }
    case ClipContextChoice::ToggleMute: {
        const auto* clip = doc_->getClip(id);
        if (clip == nullptr)
            return;
        const bool muted = !clip->muted;
        auto mutate = [this, id, muted] { doc_->setClipMuted(id, muted); };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
        break;
    }
    case ClipContextChoice::GlueWithNext: {
        const auto target = findGlueTarget(id);
        if (!target.isValid())
            return; // nothing after this clip: no mutation, and therefore no undo entry

        auto mutate = [this, id, target] { doc_->joinClips(id, target); };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
        // `target` was absorbed into `id` — the survivor keeps its own id, so only the swallowed
        // one has to leave the selection.
        selection_.remove(target);
        break;
    }
    case ClipContextChoice::Rename:
        // Deliberately inert: the rename UI has no headless meaning, and its commit path is
        // renameClip() (see ClipContextChoice). The case exists so the menu's whole vocabulary is
        // enumerable — a test can assert this choice mutates nothing.
        break;
    }

    repaint();
}

} // namespace synth::ui
