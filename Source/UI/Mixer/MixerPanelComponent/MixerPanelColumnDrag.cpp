// Concern: MixerPanelComponent's drag-to-reorder of strip columns (track-fed strips among themselves,
// track-less strips and buses among themselves) -- the header hooks, the animator glue, autoscroll, the
// drop's move of the strip's first track or save of the bus order, and column placement while a reorder
// is in flight, plus the strips' track selection, delete and keyboard move.
#include "AppUndoManager.h"
#include "MixerPanelComponent.h"
#include "MixerPanelInternal.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/ReorderDrag/ViewportDragScroll.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerTrackSelectionHost.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace synth::ui {

namespace {
constexpr int kColumnPitch = kMixerColumnWidth + kMixerColumnGap;
constexpr int kAutoscrollZone = 48; // wider than a list's: a column is a broad target
constexpr int kDragRepeatMs = 40;

// The one track a strip stands for; invalid for a bus, a strip shared by several tracks, Direct and Master.
synth::TrackId singleTrackOf(const std::vector<synth::TrackId>& feeding, bool isStrip) {
    return isStrip && feeding.size() == 1 ? feeding.front() : synth::TrackId{};
}
constexpr int kShadowLayers = 6;
constexpr float kColumnRadius = 3.0f;

int indexOfUuid(const std::vector<juce::String>& uuids, const juce::String& uuid) {
    const auto it = std::find(uuids.begin(), uuids.end(), uuid);
    return it == uuids.end() ? -1 : static_cast<int>(it - uuids.begin());
}
} // namespace

// Every strip and bus column in the scrolling group is wired; Direct and Master never get hooks, so
// their headers stay plain click targets. Which group a drag moves is decided at press time
// (beginColumnDrag). The hooks capture the panel and a uuid, never a column pointer -- a drop rebuilds the columns.
void MixerPanelComponent::wireColumnReorder(MixerColumnComponent& column, const juce::String& uuid) {
    MixerColumnHeader::ReorderHooks hooks;
    hooks.onGrab = [this, uuid](const juce::MouseEvent& e) { beginColumnDrag(uuid, e); };
    hooks.onDrag = [this](const juce::MouseEvent& e) { dragColumn(e); };
    hooks.onRelease = [this](const juce::MouseEvent&) { return endColumnDrag(); };
    hooks.isDragging = [this] { return columnReorder_.isDragging(); };
    column.setReorderHooks(std::move(hooks));
}

// The pointer is converted into content_ coordinates on every event, so the grab offset captured
// at press (pointer minus the column's left edge) stays valid while the columns move under the
// pointer and while the viewport scrolls; JUCE's own getMouseDownPosition() would not.
float MixerPanelComponent::pointerXInContent(const juce::MouseEvent& e) {
    return e.getEventRelativeTo(&content_).position.x;
}

// Slots are the static column positions (index within the scrolling group times pitch), not the columns'
// current bounds, so a press during an earlier drop's settle still starts from the true layout. Only
// the pressed column's group takes part -- track-fed strips, or track-less strips and buses -- and only
// in the scrolling zone; a pinned column is not reorderable.
void MixerPanelComponent::beginColumnDrag(const juce::String& uuid, const juce::MouseEvent& e) {
    columnFrames_.stop();
    columnCancelKey_.disarm();
    columnDragCancelled_ = false;
    columnReorder_.cancel();
    liftedUuid_ = {};
    reorderUuids_.clear();
    reorderFirstTracks_.clear();
    const auto pressed = std::find_if(columnEntries_.begin(), columnEntries_.end(), [&](const ColumnEntry& entry) {
        return entry.kind == ColumnEntry::Kind::Strip && entry.uuid == uuid;
    });
    if (pressed == columnEntries_.end())
        return;
    reorderingBuses_ = pressed->feedingTracks.empty();
    std::vector<ReorderDragAnimator::Slot> slots;
    int pressedKey = -1;
    for (const auto& entry : columnEntries_) {
        if (!inActiveReorderGroup(entry))
            continue;
        if (entry.uuid == uuid)
            pressedKey = static_cast<int>(reorderUuids_.size());
        reorderUuids_.push_back(entry.uuid);
        reorderFirstTracks_.push_back(reorderingBuses_ ? synth::TrackId{} : entry.feedingTracks.front());
        slots.push_back({static_cast<float>(entry.zoneIndex * kColumnPitch), static_cast<float>(kMixerColumnWidth)});
    }
    if (pressedKey < 0)
        return;
    const float pointer = pointerXInContent(e);
    columnReorder_.begin(slots, pressedKey, pointer - slots[static_cast<size_t>(pressedKey)].start, pointer,
                         isShowing());
    lastDraggedStart_ = slots[static_cast<size_t>(pressedKey)].start;
}

bool MixerPanelComponent::inActiveReorderGroup(const ColumnEntry& entry) const {
    return entry.kind == ColumnEntry::Kind::Strip && entry.zone == synth::MixerZone::Scrolling &&
           entry.feedingTracks.empty() == reorderingBuses_;
}

// Scrolls the viewport while the pointer is within the edge zone; the drag auto-repeat keeps the
// events coming when the pointer rests there. Faster the deeper into the zone.
// The shared step (UI/Layout/ReorderDrag/ViewportDragScroll.h) on the panel's own viewport.
void MixerPanelComponent::autoscrollForPointer(juce::Point<int> pointer) {
    viewport_drag_scroll::follow(viewport_, pointer, viewport_drag_scroll::Axis::Horizontal, kAutoscrollZone);
}

// The pointer is read in desktop coordinates before anything moves: the event comes from the dragged column's own
// header, which the scroll and the column's placement both move, so its event-relative position would go stale.
void MixerPanelComponent::dragColumn(const juce::MouseEvent& e) {
    if (!columnReorder_.isPressed() && !columnReorder_.isDragging())
        return;
    const auto pointer = viewport_drag_scroll::pointerOf(e);
    if (columnReorder_.isDragging())
        autoscrollForPointer(pointer);
    if (!columnReorder_.dragTo(viewport_drag_scroll::along(content_, pointer, viewport_drag_scroll::Axis::Horizontal)))
        return;
    if (!columnCancelKey_.isArmed()) {
        columnCancelKey_.arm(*this, [this] { cancelColumnDrag(); });
        // A process-wide setting on the mouse source, so only for a real (showing) gesture: a
        // synthesized drag never releases, and would leave the repeat running under later code.
        if (isShowing())
            juce::Component::beginDragAutoRepeat(kDragRepeatMs);
    }
    liftedUuid_ = reorderUuids_[static_cast<size_t>(columnReorder_.getDraggedKey())];
    const bool tweensChanged = columnReorder_.getTweenGeneration() != columnGenerationSeen_;
    startColumnFramesIfNeeded();
    const float start = columnReorder_.getDraggedStart();
    if (tweensChanged || start != lastDraggedStart_) {
        lastDraggedStart_ = start;
        placeColumns(content_.getHeight(), false);
        content_.repaint();
    }
}

// A frame pump runs only while a tween is in flight; a held drag with the pointer at rest, and a
// settled list, cost no frames and no repaints.
void MixerPanelComponent::startColumnFramesIfNeeded() {
    if (columnReorder_.getTweenGeneration() == columnGenerationSeen_)
        return;
    columnGenerationSeen_ = columnReorder_.getTweenGeneration();
    if (columnReorder_.needsFrames())
        columnFrames_.run(ReorderDragAnimator::kMakeRoomMs + 20.0, [this] { onColumnReorderFrame(); });
}

void MixerPanelComponent::onColumnReorderFrame() {
    columnReorder_.finishIfSettled();
    if (!columnReorder_.isReordering())
        liftedUuid_ = {};
    placeColumns(content_.getHeight(), false);
    content_.repaint();
}

// Replaces the positions the dragged group's visible uuids hold in the full bus list (hidden buses
// included) with the new visible order, in sequence; every other position is untouched.
std::vector<juce::String> MixerPanelComponent::mergeBusOrder(const std::vector<juce::String>& visibleBefore,
                                                             const std::vector<juce::String>& visibleAfter) const {
    auto merged = busUuidsInOrder_;
    size_t next = 0;
    for (auto& uuid : merged)
        if (indexOfUuid(visibleBefore, uuid) >= 0 && next < visibleAfter.size())
            uuid = visibleAfter[next++];
    return merged;
}

// A bus drop saves the new order in the view document as one undo step (its rebuild is synchronous, so
// the columns already sit in the new order when the settle starts). A track drop is expressed as a
// timeline move instead.
//
// The move is expressed as a timeline move of the dragged strip's FIRST feeding track (a strip fed
// by several tracks sits at its first one) to the index of the first track of the strip it takes
// the place of -- valid in both directions, since TimelineDoc::moveTrack takes the final index.
// The wiring rebuilds the mixer from the new track order; if it did not, the columns are rebuilt
// here so the settle lands on real positions. `committingColumnDrag_` keeps that rebuild from
// discarding the drag it is part of.
void MixerPanelComponent::commitColumnDrag() {
    committingColumnDrag_ = true;
    const int dragged = columnReorder_.getDraggedKey();
    const int insertion = columnReorder_.getInsertionIndex();
    std::vector<juce::String> newOrder;
    for (int key : columnReorder_.getNewOrder())
        newOrder.push_back(reorderUuids_[static_cast<size_t>(key)]);

    if (reorderingBuses_) {
        const auto merged = mergeBusOrder(reorderUuids_, newOrder);
        if (insertion != dragged && merged != busUuidsInOrder_)
            applyViewEdit([&](synth::MixerViewDoc& d) { d.setBusOrder(merged); });
    } else if (insertion != dragged && onMoveTrack && doc_ != nullptr) {
        const auto& tracks = doc_->getTracks();
        const auto anchorId = reorderFirstTracks_[static_cast<size_t>(insertion)];
        const auto anchor =
            std::find_if(tracks.begin(), tracks.end(), [&](const synth::Track& t) { return t.id == anchorId; });
        if (anchor != tracks.end())
            onMoveTrack(reorderFirstTracks_[static_cast<size_t>(dragged)], static_cast<int>(anchor - tracks.begin()));
    }

    std::vector<juce::String> laidOut;
    for (const auto& entry : columnEntries_)
        if (inActiveReorderGroup(entry))
            laidOut.push_back(entry.uuid);
    if (laidOut != newOrder)
        rebuild();

    settleColumnsAfterMove(reorderUuids_);
    committingColumnDrag_ = false;
}

// Returns true whenever the press became a drag (or was cancelled by Esc): the header must not also
// select the column on that release. The header copies this hook before calling it, because the
// drop can rebuild -- and so destroy -- the very header that invoked it.
bool MixerPanelComponent::endColumnDrag() {
    columnCancelKey_.disarm();
    if (columnDragCancelled_) {
        columnDragCancelled_ = false;
        return true;
    }
    if (columnReorder_.isPressed()) {
        columnReorder_.cancel();
        return false;
    }
    if (!columnReorder_.isDragging())
        return false;
    commitColumnDrag();
    return true;
}

// Esc: nothing is committed -- no track moves, no undo step. The animator glues everything back.
void MixerPanelComponent::cancelColumnDrag() {
    columnCancelKey_.disarm();
    if (!columnReorder_.isDragging())
        return;
    columnDragCancelled_ = true;
    columnReorder_.abort();
    startColumnFramesIfNeeded();
    onColumnReorderFrame();
}

// A rebuild is about to destroy the columns a held drag belongs to.
void MixerPanelComponent::discardColumnDrag() {
    columnCancelKey_.disarm();
    columnFrames_.stop();
    columnReorder_.cancel();
    liftedUuid_ = {};
    columnDragCancelled_ = false;
}

// Strips of the active reorder group take their x from the animator while a reorder is in flight (the
// dragged one from its lifted position); everything else sits at its static slot in its zone. Strips are
// matched to animator keys by uuid, so this stays right across the rebuild a drop causes.
void MixerPanelComponent::placeColumns(int columnHeight, bool relayout) {
    const bool reordering = columnReorder_.isReordering();
    auto placeAt = [columnHeight, relayout](juce::Component& column, int x) {
        // setBounds() alone skips resized() when a column's size did not change, but a section-layout
        // change moves everything inside it -- so a full pass re-lays each column out explicitly.
        column.setBounds(x, 0, kMixerColumnWidth, columnHeight);
        if (relayout)
            column.resized();
    };

    for (const auto& entry : columnEntries_) {
        if (entry.component == nullptr)
            continue;
        int x = entry.zoneIndex * kColumnPitch;
        float lift = 0.0f;
        const int key = reordering && inActiveReorderGroup(entry) ? indexOfUuid(reorderUuids_, entry.uuid) : -1;
        if (key >= 0) {
            const bool isDragged = key == columnReorder_.getDraggedKey();
            x = static_cast<int>(
                std::lround(isDragged ? columnReorder_.getDraggedStart() : columnReorder_.getLayoutStart(key)));
            lift = isDragged ? columnReorder_.getLift() : 0.0f;
        }
        placeAt(*entry.component, x);
        if (entry.kind == ColumnEntry::Kind::Strip) {
            auto& strip = *static_cast<MixerColumnComponent*>(entry.component);
            strip.setLift(lift);
            if (lift > 0.0f)
                strip.toFront(false);
        }
    }
}

// Drawn under the columns (content_'s own paint): the dashed marker of the gap the dragged column
// will land in, and the lifted column's soft shadow, which only shows outside its opaque bounds.
void MixerPanelComponent::paintColumnDragChrome(juce::Graphics& g) {
    if (!columnReorder_.isReordering() || liftedUuid_.isEmpty())
        return;
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const int key = indexOfUuid(reorderUuids_, liftedUuid_);
    if (laf == nullptr || key < 0)
        return;
    const auto& theme = laf->getTheme();

    const juce::Rectangle<float> gap(columnReorder_.getLayoutStart(key), 0.0f, static_cast<float>(kMixerColumnWidth),
                                     static_cast<float>(content_.getHeight()));
    juce::Path outline;
    outline.addRoundedRectangle(gap.reduced(0.5f), kColumnRadius);
    juce::Path dashed;
    const float dashLengths[] = {3.0f, 2.0f};
    juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashLengths, 2);
    g.setColour(theme.colors.border);
    g.fillPath(dashed);

    const float lift = columnReorder_.getLift();
    for (const auto& entry : columnEntries_) {
        if (entry.kind != ColumnEntry::Kind::Strip || entry.uuid != liftedUuid_)
            continue;
        const auto bounds = entry.component->getBounds().toFloat();
        for (int layer = kShadowLayers; layer >= 1; --layer) {
            g.setColour(
                juce::Colours::black.withAlpha(0.05f * juce::jlimit(0.0f, 1.0f, theme.treatment.shadow) * lift));
            g.fillRoundedRectangle(bounds.expanded(static_cast<float>(layer)).translated(0.0f, 1.0f),
                                   kColumnRadius + static_cast<float>(layer));
        }
    }
}

// Strip selection, delete and move (the timeline's selection reached through selectionHost()). A strip fed by one
// track selects it with a header press (Cmd toggles, Shift extends), Cmd+Backspace and the header menu delete the
// selected set through the host (its question, its setting, one undo), and Option+Left/Right moves the set one place
// with the column drag's motion.
void MixerPanelComponent::setTrackSelectionHost(MixerTrackSelectionHost host) {
    selectionHost_ = std::make_shared<const MixerTrackSelectionHost>(std::move(host));
}

const MixerTrackSelectionHost& MixerPanelComponent::selectionHost() const {
    static const MixerTrackSelectionHost none;
    return selectionHost_ != nullptr ? *selectionHost_ : none;
}

void MixerPanelComponent::syncTrackSelectionVisuals() {
    for (auto& entry : columnEntries_) {
        if (entry.kind != ColumnEntry::Kind::Strip || entry.component == nullptr)
            continue;
        const auto track = singleTrackOf(entry.feedingTracks, true);
        const bool selected = track.isValid() && selectionHost().isSelected && selectionHost().isSelected(track);
        static_cast<MixerColumnComponent*>(entry.component)->setTrackSelected(selected);
    }
}

void MixerPanelComponent::pressStripHeaderForTest(int index, juce::ModifierKeys mods) {
    auto* column = getStripColumnForTest(index);
    if (column == nullptr)
        return;
    auto& header = column->getHeaderForTest();
    const auto at = header.getLocalBounds().getCentre().toFloat();
    const auto make = [&] {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                                0.0f, &header, &header, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(),
                                1, false);
    };
    header.mouseDown(make());
    clickFocus_.mouseDown(make());
    header.mouseUp(make());
    clickFocus_.mouseUp(make());
}

// A press on a strip's header: a plain press selects its track, Cmd/Ctrl toggles it, Shift extends (the timeline's
// rules). A modified press only edits the selection, so the release does not also pick the macro on the canvas. A
// right-press keeps a selection it lands in and otherwise selects the strip, like the timeline row's menu.
void MixerPanelComponent::selectFromHeaderPress(const juce::MouseEvent& e, int index) {
    if (!selectionHost().select || !juce::isPositiveAndBelow(index, (int)columnEntries_.size()))
        return;
    const auto& entry = columnEntries_[(size_t)index];
    const auto track = singleTrackOf(entry.feedingTracks, entry.kind == ColumnEntry::Kind::Strip);
    if (!track.isValid())
        return;
    if (e.mods.isPopupMenu()) {
        if (!selectionHost().isSelected || !selectionHost().isSelected(track))
            selectionHost().select(track, juce::ModifierKeys());
        return;
    }
    modifiedHeaderPress_ = e.mods.isShiftDown() || e.mods.isCommandDown();
    selectionHost().select(track, e.mods);
}

// The arrow keys walk the strips and the selection follows the focused one, as the timeline's rows do.
void MixerPanelComponent::selectFocusedStripTrack() {
    if (!selectionHost().select || !juce::isPositiveAndBelow(focusedColumnIndex_, (int)columnEntries_.size()))
        return;
    const auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    if (const auto track = singleTrackOf(entry.feedingTracks, entry.kind == ColumnEntry::Kind::Strip); track.isValid())
        selectionHost().select(track, juce::ModifierKeys());
}

// Cmd+Space: puts the focused strip's track in or out of the selection, the timeline row's key.
bool MixerPanelComponent::toggleFocusedTrackSelection() {
    if (!selectionHost().select || !juce::isPositiveAndBelow(focusedColumnIndex_, (int)columnEntries_.size()))
        return false;
    const auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    const auto track = singleTrackOf(entry.feedingTracks, entry.kind == ColumnEntry::Kind::Strip);
    if (!track.isValid())
        return false;
    selectionHost().select(track, juce::ModifierKeys(juce::ModifierKeys::commandModifier));
    return true;
}

// Cmd+Backspace: the host asks (its question, its setting) and deletes the selected set, or the focused strip's track
// when it is not one of them, in one undo step.
bool MixerPanelComponent::deleteFocusedTracks() {
    if (!selectionHost().deleteTracks || !juce::isPositiveAndBelow(focusedColumnIndex_, (int)columnEntries_.size()))
        return false;
    const auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    const auto track = singleTrackOf(entry.feedingTracks, entry.kind == ColumnEntry::Kind::Strip);
    if (!track.isValid())
        return false;
    selectionHost().deleteTracks(track, false);
    return true;
}

// The timeline's track order after the set (the selection, or the focused strip's track when it is not in it) moves
// one place: each run of selected tracks swaps past the unselected track next to it, a run at the edge stays. Empty
// when nothing would move.
std::vector<synth::TrackId> MixerPanelComponent::trackOrderAfterMove(int direction) const {
    if (doc_ == nullptr || !juce::isPositiveAndBelow(focusedColumnIndex_, (int)columnEntries_.size()))
        return {};
    const auto& focused = columnEntries_[(size_t)focusedColumnIndex_];
    const auto focusedTrack = singleTrackOf(focused.feedingTracks, focused.kind == ColumnEntry::Kind::Strip);
    if (!focusedTrack.isValid())
        return {};
    std::set<synth::TrackId> moving;
    if (selectionHost().selected)
        for (const auto id : selectionHost().selected())
            moving.insert(id);
    if (moving.count(focusedTrack) == 0)
        moving = {focusedTrack};

    std::vector<synth::TrackId> order;
    for (const auto& track : doc_->getTracks())
        order.push_back(track.id);
    const auto before = order;
    const int count = static_cast<int>(order.size());
    if (direction < 0) {
        for (int i = 1; i < count; ++i)
            if (moving.count(order[(size_t)i]) != 0 && moving.count(order[(size_t)i - 1]) == 0)
                std::swap(order[(size_t)i], order[(size_t)i - 1]);
    } else {
        for (int i = count - 2; i >= 0; --i)
            if (moving.count(order[(size_t)i]) != 0 && moving.count(order[(size_t)i + 1]) == 0)
                std::swap(order[(size_t)i], order[(size_t)i + 1]);
    }
    return order == before ? std::vector<synth::TrackId>{} : order;
}

// Option+Left / Option+Right. The key is swallowed on any track strip, even at the edge where nothing moves. The move
// is the timeline's own (one undo step through the host), shown with the column drag's motion: the animator starts
// with every strip of the track group where it stands and release() glides each to the slot the rebuilt panel gives it.
bool MixerPanelComponent::moveSelectedStrips(int direction) {
    if (!selectionHost().reorderTracks || !juce::isPositiveAndBelow(focusedColumnIndex_, (int)columnEntries_.size()))
        return false;
    const auto& focused = columnEntries_[(size_t)focusedColumnIndex_];
    if (!singleTrackOf(focused.feedingTracks, focused.kind == ColumnEntry::Kind::Strip).isValid())
        return false;
    const auto order = trackOrderAfterMove(direction);
    if (order.empty())
        return true;

    columnFrames_.stop();
    columnReorder_.cancel();
    liftedUuid_ = {};
    reorderUuids_.clear();
    reorderFirstTracks_.clear();
    reorderingBuses_ = false;
    std::vector<ReorderDragAnimator::Slot> slots;
    for (const auto& entry : columnEntries_) {
        if (!inActiveReorderGroup(entry))
            continue;
        reorderUuids_.push_back(entry.uuid);
        reorderFirstTracks_.push_back(entry.feedingTracks.front());
        slots.push_back({static_cast<float>(entry.zoneIndex * kColumnPitch), static_cast<float>(kMixerColumnWidth)});
    }
    const bool glide = slots.size() > 1 && (isShowing() || columnMotionForced_) && !prefersReducedMotion();
    if (slots.size() > 1) {
        columnReorder_.begin(slots, 0, 0.0f, slots[0].start - 2.0f * ReorderDragAnimator::kDragThresholdPx, glide);
        columnReorder_.dragTo(slots[0].start); // nothing is lifted: the first strip has not moved
    }

    const bool hadKeyboard = hasKeyboardFocus(true);
    committingColumnDrag_ = true;
    selectionHost().reorderTracks(order);
    rebuild(); // the host's own rebuild is idempotent; this makes the new slots real before the glide starts
    settleColumnsAfterMove(reorderUuids_);
    committingColumnDrag_ = false;
    if (hadKeyboard) { // the edit's own follow-up work (timeline rows, rebuilds) can take the keyboard: take it back
                       // after it
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MixerPanelComponent>(this)] {
            if (safe != nullptr && safe->isVisible() && !safe->hasKeyboardFocus(true))
                safe->grabKeyboardFocus();
        });
    }
    revealFocusedColumn();
    return true;
}

// The tail commitColumnDrag shares: each strip of the group glides from where it was to its slot in the rebuilt panel.
void MixerPanelComponent::settleColumnsAfterMove(const std::vector<juce::String>& uuids) {
    std::vector<float> finalStarts;
    for (const auto& uuid : uuids) {
        const auto entry = std::find_if(columnEntries_.begin(), columnEntries_.end(), [&](const ColumnEntry& e) {
            return inActiveReorderGroup(e) && e.uuid == uuid;
        });
        if (entry == columnEntries_.end()) { // the strip vanished under the move
            columnReorder_.cancel();
            break;
        }
        finalStarts.push_back(static_cast<float>(entry->zoneIndex * kColumnPitch));
    }
    if (columnReorder_.isReordering() && finalStarts.size() == uuids.size())
        columnReorder_.release(finalStarts);
    if (!columnReorder_.isReordering())
        liftedUuid_ = {};
    startColumnFramesIfNeeded();
    placeColumns(content_.getHeight(), false);
    content_.repaint();
}

} // namespace synth::ui
