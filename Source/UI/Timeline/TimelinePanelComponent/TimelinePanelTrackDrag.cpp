// Concern: TimelinePanelComponent's drag-to-reorder of the track header rows -- the
// animator glue, autoscroll, the drop's doc move, and row placement while a reorder is in flight.
// The row (TimelineTrackHeaderComponent) detects the gesture and hands up raw screen Y; the clip
// lanes on the right are NOT animated, they follow the new order when the drop commits.
#include "AppUndoManager.h"
#include "TimelinePanelComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr float kAutoscrollZone = 32.0f;
constexpr float kAutoscrollMaxStep = 18.0f;
constexpr int kDragRepeatMs = 40;
constexpr float kRowRadius = 3.0f;

int indexOfTrack(const std::vector<synth::TrackId>& ids, synth::TrackId id) {
    const auto it = std::find(ids.begin(), ids.end(), id);
    return it == ids.end() ? -1 : static_cast<int>(it - ids.begin());
}
} // namespace

// Screen Y in the header list's own coordinates. Converted on every event, so the grab offset
// captured at press stays valid while rows move under the pointer and while the viewport scrolls;
// JUCE's getMouseDownPosition() would not (see docs/layout/animation.md#reorder-drag).
float TimelinePanelComponent::trackPointerY(int screenY) const {
    return static_cast<float>(trackHeaderList_.getLocalPoint(nullptr, juce::Point<int>(0, screenY)).y);
}

// Slots are the rows' static positions (their TimelineRowLayout spans), not their current bounds, so a
// press during an earlier drop's settle still starts from the true layout.
void TimelinePanelComponent::beginTrackDrag(synth::TrackId trackId, int screenY) {
    trackFrames_.stop();
    trackCancelKey_.disarm();
    trackDragCancelled_ = false;
    trackReorder_.cancel();
    liftedTrackId_ = {};
    reorderTrackIds_.clear();
    const auto layout = rowLayout();
    std::vector<ReorderDragAnimator::Slot> slots;
    int pressedKey = -1;
    // The "Unassigned automation" section is pinned last: it is never a slot, so it can neither be
    // dragged nor have another track dropped below it.
    for (int i = 0; i < trackHeaderList_.headers.size(); ++i) {
        const auto* header = trackHeaderList_.headers.getUnchecked(i);
        if (header->isSectionHeader())
            continue;
        const auto id = header->getTrackId();
        if (id == trackId)
            pressedKey = (int)reorderTrackIds_.size();
        reorderTrackIds_.push_back(id);
        const auto span = layout.trackSpan(i);
        slots.push_back({static_cast<float>(span.getStart()), static_cast<float>(span.getLength())});
    }
    if (pressedKey < 0)
        return;
    const float pointer = trackPointerY(screenY);
    trackReorder_.begin(slots, pressedKey, pointer - slots[static_cast<size_t>(pressedKey)].start, pointer,
                        isShowing());
    lastDraggedTrackStart_ = slots[static_cast<size_t>(pressedKey)].start;
}

// Scrolls the track list while the pointer is within the edge zone of its viewport; the drag
// auto-repeat keeps the events coming when the pointer rests there. Faster the deeper into the zone.
// It goes through scrollTrackRows so the clip lanes and the viewport stay in step.
void TimelinePanelComponent::autoscrollForTrackPointer(int screenY) {
    const float y = static_cast<float>(trackHeaderViewport_.getLocalPoint(nullptr, juce::Point<int>(0, screenY)).y);
    const float visible = static_cast<float>(trackHeaderViewport_.getMaximumVisibleHeight());
    float depth = 0.0f;
    if (y < kAutoscrollZone)
        depth = -(kAutoscrollZone - std::max(y, 0.0f)) / kAutoscrollZone;
    else if (y > visible - kAutoscrollZone)
        depth = (std::min(y, visible) - (visible - kAutoscrollZone)) / kAutoscrollZone;
    if (depth == 0.0f)
        return;
    int step = static_cast<int>(std::lround(depth * kAutoscrollMaxStep));
    if (step == 0)
        step = depth < 0.0f ? -1 : 1;
    scrollTrackRows(static_cast<double>(step));
}

void TimelinePanelComponent::updateTrackDrag(int screenY) {
    if (!trackReorder_.isPressed() && !trackReorder_.isDragging())
        return;
    if (trackReorder_.isDragging())
        autoscrollForTrackPointer(screenY);
    if (!trackReorder_.dragTo(trackPointerY(screenY)))
        return;
    if (!trackCancelKey_.isArmed()) {
        trackCancelKey_.arm(*this, [this] { cancelTrackDrag(); });
        // A process-wide setting on the mouse source, so only for a real (showing) gesture: a
        // synthesized drag never releases, and would leave the repeat running under later code.
        if (isShowing())
            juce::Component::beginDragAutoRepeat(kDragRepeatMs);
    }
    liftedTrackId_ = reorderTrackIds_[static_cast<size_t>(trackReorder_.getDraggedKey())];
    const bool tweensChanged = trackReorder_.getTweenGeneration() != trackGenerationSeen_;
    startTrackFramesIfNeeded();
    const float start = trackReorder_.getDraggedStart();
    if (tweensChanged || start != lastDraggedTrackStart_) {
        lastDraggedTrackStart_ = start;
        placeTrackHeaders();
        trackHeaderList_.repaint();
    }
}

// A frame pump runs only while a tween is in flight; a held drag with the pointer at rest, and a
// settled list, cost no frames and no repaints.
void TimelinePanelComponent::startTrackFramesIfNeeded() {
    if (trackReorder_.getTweenGeneration() == trackGenerationSeen_)
        return;
    trackGenerationSeen_ = trackReorder_.getTweenGeneration();
    if (trackReorder_.needsFrames())
        trackFrames_.run(ReorderDragAnimator::kMakeRoomMs + 20.0, [this] { onTrackReorderFrame(); });
}

void TimelinePanelComponent::onTrackReorderFrame() {
    trackReorder_.finishIfSettled();
    if (!trackReorder_.isReordering())
        liftedTrackId_ = {};
    placeTrackHeaders();
    trackHeaderList_.repaint();
}

// The drop is one TimelineDoc::moveTrack (through the host's undo path, as before) to the animator's
// insertion index, which is already the track's final index. The doc notification rebuilds the
// header column synchronously -- destroying the row whose mouseUp is on the stack (see the ORDERING
// HAZARD note on onRowDragEnded) -- so nothing here touches a row pointer across that call, and
// `committingTrackDrag_` keeps that rebuild from discarding the very gesture being committed.
void TimelinePanelComponent::commitTrackDrag() {
    committingTrackDrag_ = true;
    const int dragged = trackReorder_.getDraggedKey();
    const int insertion = trackReorder_.getInsertionIndex();
    const auto trackId = reorderTrackIds_[static_cast<size_t>(dragged)];

    if (insertion != dragged && doc_ != nullptr) {
        // Same no-host fallback TimelineTrackHeaderComponent::performEdit uses -- a panel driven
        // directly against a doc (no MainComponent/undo wiring) still works.
        auto mutate = [this, trackId, insertion] { doc_->moveTrack(trackId, insertion); };
        if (trackHeaderHost_ != nullptr)
            trackHeaderHost_->performTrackEdit(mutate);
        else
            mutate();
    }

    std::vector<float> finalStarts;
    const auto layout = rowLayout();
    for (auto id : reorderTrackIds_) {
        int index = -1;
        for (int i = 0; i < trackHeaderList_.headers.size(); ++i)
            if (trackHeaderList_.headers.getUnchecked(i)->getTrackId() == id)
                index = i;
        if (index < 0) { // the track vanished under the drop
            trackReorder_.cancel();
            break;
        }
        finalStarts.push_back(static_cast<float>(layout.trackTop(index)));
    }
    if (finalStarts.size() == reorderTrackIds_.size())
        trackReorder_.release(finalStarts);
    committingTrackDrag_ = false;

    if (!trackReorder_.isReordering())
        liftedTrackId_ = {};
    startTrackFramesIfNeeded();
    placeTrackHeaders();
    trackHeaderList_.repaint();
}

void TimelinePanelComponent::endTrackDrag() {
    trackCancelKey_.disarm();
    if (trackDragCancelled_) {
        trackDragCancelled_ = false;
        return;
    }
    if (trackReorder_.isPressed()) {
        trackReorder_.cancel();
        return;
    }
    if (!trackReorder_.isDragging())
        return;
    commitTrackDrag();
}

// Esc: nothing is committed -- no track moves, no undo step. The animator glues everything back.
void TimelinePanelComponent::cancelTrackDrag() {
    trackCancelKey_.disarm();
    if (!trackReorder_.isDragging())
        return;
    trackDragCancelled_ = true;
    trackReorder_.abort();
    startTrackFramesIfNeeded();
    onTrackReorderFrame();
}

// A rebuild is about to destroy the rows a held drag belongs to.
void TimelinePanelComponent::discardTrackDrag() {
    trackCancelKey_.disarm();
    trackFrames_.stop();
    trackReorder_.cancel();
    liftedTrackId_ = {};
    trackDragCancelled_ = false;
}

// Rows take their y from the animator while a reorder is in flight (the dragged one from its lifted
// position); everything else sits at its static slot. Rows are matched to animator keys by track
// id, so this stays right across the rebuild a drop causes.
void TimelinePanelComponent::placeTrackHeaders() {
    const auto layout = rowLayout();
    const int width = std::max(0, trackHeaderViewport_.getMaximumVisibleWidth());
    const bool reordering = trackReorder_.isReordering();
    for (int i = 0; i < trackHeaderList_.headers.size(); ++i) {
        auto* header = trackHeaderList_.headers.getUnchecked(i);
        int y = layout.trackTop(i);
        float lift = 0.0f;
        const int key = reordering ? indexOfTrack(reorderTrackIds_, header->getTrackId()) : -1;
        if (key >= 0) {
            const bool isDragged = key == trackReorder_.getDraggedKey();
            y = static_cast<int>(
                std::lround(isDragged ? trackReorder_.getDraggedStart() : trackReorder_.getLayoutStart(key)));
            lift = isDragged ? trackReorder_.getLift() : 0.0f;
        }
        header->setBounds(0, y, width, layout.trackRowHeight(i));
        automationLanes_.placeHeadersFor(header->getTrackId(), y + layout.trackRowHeight(i), width);
        header->setLift(lift);
        if (lift > 0.0f)
            header->toFront(false);
    }
}

// Drawn under the rows (each row fills its own bounds, and the gap is exactly where none stands):
// the dashed marker of the slot the dragged row will land in.
void TimelinePanelComponent::TrackHeaderList::paint(juce::Graphics& g) {
    const auto& reorder = owner_.trackReorder_;
    if (!reorder.isReordering() || !owner_.liftedTrackId_.isValid())
        return;
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const int key = indexOfTrack(owner_.reorderTrackIds_, owner_.liftedTrackId_);
    if (laf == nullptr || key < 0)
        return;

    const juce::Rectangle<float> gap(0.0f, reorder.getLayoutStart(key), static_cast<float>(getWidth()),
                                     static_cast<float>(owner_.currentRowHeight()));
    juce::Path outline;
    outline.addRoundedRectangle(gap.reduced(0.5f), kRowRadius);
    juce::Path dashed;
    const float dashLengths[] = {3.0f, 2.0f};
    juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashLengths, 2);
    g.setColour(laf->getTheme().colors.border);
    g.fillPath(dashed);
}

} // namespace synth::ui
