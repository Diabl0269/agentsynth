// Concern: MixerPanelComponent's drag-to-reorder of strip columns (track-fed strips among themselves,
// track-less strips and buses among themselves) -- the header hooks, the animator glue, autoscroll, the
// drop's move of the strip's first track or save of the bus order, and column placement while a reorder
// is in flight.
#include "AppUndoManager.h"
#include "MixerPanelComponent.h"
#include "MixerPanelInternal.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kColumnPitch = kMixerColumnWidth + kMixerColumnGap;
constexpr float kAutoscrollZone = 48.0f;
constexpr float kAutoscrollMaxStep = 18.0f;
constexpr int kDragRepeatMs = 40;
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
void MixerPanelComponent::autoscrollForPointer(const juce::MouseEvent& e) {
    const float x = e.getEventRelativeTo(&viewport_).position.x;
    const float visible = static_cast<float>(viewport_.getMaximumVisibleWidth());
    float depth = 0.0f;
    if (x < kAutoscrollZone)
        depth = -(kAutoscrollZone - std::max(x, 0.0f)) / kAutoscrollZone;
    else if (x > visible - kAutoscrollZone)
        depth = (std::min(x, visible) - (visible - kAutoscrollZone)) / kAutoscrollZone;
    if (depth == 0.0f)
        return;
    int step = static_cast<int>(std::lround(depth * kAutoscrollMaxStep));
    if (step == 0)
        step = depth < 0.0f ? -1 : 1;
    const int maxX = std::max(0, content_.getWidth() - viewport_.getMaximumVisibleWidth());
    viewport_.setViewPosition(std::clamp(viewport_.getViewPositionX() + step, 0, maxX), viewport_.getViewPositionY());
}

void MixerPanelComponent::dragColumn(const juce::MouseEvent& e) {
    if (!columnReorder_.isPressed() && !columnReorder_.isDragging())
        return;
    if (columnReorder_.isDragging())
        autoscrollForPointer(e);
    if (!columnReorder_.dragTo(pointerXInContent(e)))
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

    std::vector<float> finalStarts;
    for (const auto& uuid : reorderUuids_) {
        const auto entry = std::find_if(columnEntries_.begin(), columnEntries_.end(), [&](const ColumnEntry& e) {
            return inActiveReorderGroup(e) && e.uuid == uuid;
        });
        if (entry == columnEntries_.end()) { // the strip vanished under the drop
            columnReorder_.cancel();
            break;
        }
        finalStarts.push_back(static_cast<float>(entry->zoneIndex * kColumnPitch));
    }
    if (finalStarts.size() == reorderUuids_.size())
        columnReorder_.release(finalStarts);
    committingColumnDrag_ = false;

    if (!columnReorder_.isReordering())
        liftedUuid_ = {};
    startColumnFramesIfNeeded();
    placeColumns(content_.getHeight(), false);
    content_.repaint();
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

} // namespace synth::ui
