// TrackAutomationLanes.cpp
//
// Concern: expansion state, the pooled lane-row headers and editors, their placement from the
// shared TrackRowLayout, and lane-row focus (docs/timeline/track-automation.md#lane-rows). The
// toolbar controls and the tool mapping live in TrackAutomationLanesToolbar.cpp.

#include "TrackAutomationLanes.h"

#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include "UI/Timeline/TrackColour.h"
#include <cmath>

namespace synth::ui {

TrackAutomationLanes::TrackAutomationLanes(TimelineViewState& viewState, juce::Component& headerList)
    : viewState_(viewState)
    , headerList_(headerList)
    , curveButton_("Draw curve", {})
    , globalButton_("Global automation", globalAutomationGlyph)
    , followsButton_("Automation follows clips", followsClipsGlyph) {
    // Click-through everywhere except on an editor: a press on the layer's empty area (a track row)
    // falls to the clip lanes underneath, exactly as if the layer were not there.
    editorLayer_.setInterceptsMouseClicks(false, true);
    editorLayer_.setComponentID("trackAutomationLaneLayer");
    setUpToolbarButtons();
}

TrackAutomationLanes::~TrackAutomationLanes() {
    for (auto& editor : editors_)
        editor->removeMouseListener(&focusListener_);
}

void TrackAutomationLanes::setTimelineDoc(synth::TimelineDoc* doc) {
    doc_ = doc;
    for (auto& editor : editors_) {
        editor->setTimelineDoc(doc);
        editor->setActiveLane({});
        editor->setVisible(false);
    }
    for (auto& header : headers_) {
        header->bind(doc, undoManager_, host_, {});
        header->setVisible(false);
    }
    focusedLane_ = {};
}

void TrackAutomationLanes::setUndoManager(AppUndoManager* undoManager) {
    undoManager_ = undoManager;
    for (auto& editor : editors_)
        editor->setUndoManager(undoManager);
    for (auto& header : headers_)
        header->bind(doc_, undoManager, host_, header->getLane());
}

void TrackAutomationLanes::setTrackHeaderHost(TrackHeaderHost* host) {
    host_ = host;
    for (auto& header : headers_)
        header->bind(doc_, undoManager_, host, header->getLane());
}

void TrackAutomationLanes::setTransport(synth::TransportService* transport) {
    transport_ = transport;
    for (auto& editor : editors_)
        editor->setTransport(transport);
}

//==============================================================================
bool TrackAutomationLanes::isExpanded(synth::TrackId track) const {
    return viewState_.expandedLaneTracks.count(track.value) != 0;
}

// In-session UI state only: TimelineViewState is not serialised, and nothing in the project file
// records which tracks were open (docs/timeline/track-automation.md#expanding-a-track).
void TrackAutomationLanes::setExpanded(synth::TrackId track, bool expanded) {
    if (!track.isValid() || isExpanded(track) == expanded)
        return;
    if (expanded)
        viewState_.expandedLaneTracks.insert(track.value);
    else
        viewState_.expandedLaneTracks.erase(track.value);
    // Collapsing the track that holds the focused lane drops the focus with it -- a hidden row
    // cannot be "the lane you are editing".
    if (!expanded && doc_ != nullptr)
        if (const auto* owner = doc_->getTrackForLane(focusedLane_); owner != nullptr && owner->id == track)
            focusLane({});
    if (callbacks_.relayout)
        callbacks_.relayout();
}

// Global lanes (on an Automation-kind track) are never shown as rows, so they are not ours to
// reveal: the caller (TimelinePanelComponent::revealAutomationLane) opens the bottom strip on them.
bool TrackAutomationLanes::revealLane(synth::LaneId lane) {
    if (doc_ == nullptr)
        return false;
    const auto* track = doc_->getTrackForLane(lane);
    if (track == nullptr || track->kind == synth::TrackKind::Automation)
        return false;
    const auto trackId = track->id;
    setExpanded(trackId, true);
    if (callbacks_.relayout)
        callbacks_.relayout(); // unconditional: the lane may be new on an already-expanded track
    if (const auto* layout = callbacks_.rowLayout ? callbacks_.rowLayout() : nullptr)
        if (const auto* row = layout->rowForLane(lane); row != nullptr && callbacks_.ensureContentVisible)
            callbacks_.ensureContentVisible(row->top, row->bottom());
    focusLane(lane);
    return true;
}

// The focused lane is explicit state (not "whichever editor has keyboard focus"): real focus is a
// best-effort no-op without a native peer, and the highlight must still be right headlessly.
void TrackAutomationLanes::focusLane(synth::LaneId lane) {
    focusedLane_ = lane;
    for (auto& header : headers_)
        header->setFocusedLane(lane.isValid() && header->isVisible() && header->getLane() == lane);
    for (auto& editor : editors_) {
        const bool focused = lane.isValid() && editor->isVisible() && editor->getActiveLane() == lane;
        editor->setHighlighted(focused);
        if (focused && editor->isShowing())
            editor->grabKeyboardFocus();
    }
}

void TrackAutomationLanes::FocusListener::mouseDown(const juce::MouseEvent& e) {
    if (auto* editor = dynamic_cast<AutomationLaneEditor*>(e.eventComponent))
        owner_.focusLane(editor->getActiveLane());
}

//==============================================================================
void TrackAutomationLanes::refreshFromDoc() {
    if (doc_ == nullptr || (focusedLane_.isValid() && doc_->getLane(focusedLane_) == nullptr))
        focusedLane_ = {};
    for (auto& header : headers_)
        if (header->isVisible())
            header->refreshFromDoc();
    for (auto& editor : editors_)
        if (editor->isVisible()) {
            editor->setTrackLaneStyle(true, trackColourForLane(editor->getActiveLane()));
            editor->repaint();
        }
}

// Headers are positioned in the header LIST's content coordinates (the viewport scrolls them),
// one per expanded lane row, re-bound by index; a header is re-bound only when its row now shows a
// different lane, so a header whose own combo callback is on the stack keeps its lane.
void TrackAutomationLanes::layoutRows(const TrackRowLayout& layout, int headerWidth) {
    std::size_t used = 0;
    for (const auto& row : layout.getRows()) {
        if (!row.isLaneRow())
            continue;
        auto& header = headerFor(used++);
        if (header.getLane() != row.lane)
            header.bind(doc_, undoManager_, host_, row.lane);
        header.setBounds(0, row.top, headerWidth, row.height);
        header.setVisible(true);
        header.setFocusedLane(row.lane == focusedLane_);
    }
    for (std::size_t i = used; i < headers_.size(); ++i)
        headers_[i]->setVisible(false);
    layoutEditors(layout);
}

// Editors exist only for rows inside the visible window (the layer's height below trackScrollY),
// reused across scrolls. An editor already showing a still-visible lane keeps it -- its in-flight
// gesture survives a relayout triggered by its own commit -- and only an editor whose lane went
// out of view is re-pointed. The pool never shrinks during a session: a doc mutation fired from
// inside an editor's own mouseUp must never free that editor.
void TrackAutomationLanes::layoutEditors(const TrackRowLayout& layout) {
    const int scroll = (int)std::llround(viewState_.trackScrollY);
    const int viewHeight = editorLayer_.getHeight();
    std::vector<const TrackRowLayout::Row*> visibleRows;
    for (const auto& row : layout.getRows())
        if (row.isLaneRow() && row.bottom() > scroll && row.top < scroll + viewHeight)
            visibleRows.push_back(&row);

    std::vector<bool> claimed(editors_.size(), false);
    std::vector<AutomationLaneEditor*> assigned(visibleRows.size(), nullptr);
    for (std::size_t r = 0; r < visibleRows.size(); ++r)
        for (std::size_t e = 0; e < editors_.size(); ++e)
            if (!claimed[e] && editors_[e]->getActiveLane() == visibleRows[r]->lane) {
                claimed[e] = true;
                assigned[r] = editors_[e].get();
                break;
            }
    for (std::size_t r = 0; r < visibleRows.size(); ++r) {
        if (assigned[r] == nullptr)
            assigned[r] = &editorFor(visibleRows[r]->lane, claimed);
        auto* editor = assigned[r];
        const auto& row = *visibleRows[r];
        editor->setBounds(0, row.top - scroll, editorLayer_.getWidth(), row.height);
        editor->setTrackLaneStyle(true, trackColourForLane(row.lane));
        editor->setHighlighted(row.lane == focusedLane_);
        editor->setVisible(true);
    }
    for (std::size_t e = 0; e < editors_.size(); ++e)
        if (!claimed[e])
            editors_[e]->setVisible(false);
}

AutomationLaneEditor& TrackAutomationLanes::editorFor(synth::LaneId lane, std::vector<bool>& claimed) {
    for (std::size_t e = 0; e < editors_.size(); ++e)
        if (!claimed[e]) {
            claimed[e] = true;
            editors_[e]->setActiveLane(lane);
            return *editors_[e];
        }
    auto editor = std::make_unique<AutomationLaneEditor>(viewState_);
    editor->setComponentID("trackLaneEditor");
    editor->setTimelineDoc(doc_);
    editor->setUndoManager(undoManager_);
    editor->setTransport(transport_);
    editor->addMouseListener(&focusListener_, false);
    applyToolToEditor(*editor);
    editor->setActiveLane(lane);
    editorLayer_.addChildComponent(*editor);
    editors_.push_back(std::move(editor));
    claimed.push_back(true);
    return *editors_.back();
}

TrackLaneHeaderComponent& TrackAutomationLanes::headerFor(std::size_t index) {
    while (headers_.size() <= index) {
        auto header = std::make_unique<TrackLaneHeaderComponent>();
        header->onFocusRequested = [this](synth::LaneId lane) { focusLane(lane); };
        headerList_.addChildComponent(*header);
        headers_.push_back(std::move(header));
    }
    return *headers_[index];
}

juce::Colour TrackAutomationLanes::trackColourForLane(synth::LaneId lane) const {
    if (doc_ != nullptr)
        if (const auto* track = doc_->getTrackForLane(lane)) {
            const auto index = (int)(track - doc_->getTracks().data());
            return resolveTrackColour(track->colourArgb, index, track->muted);
        }
    return juce::Colour(0xff808080);
}

//==============================================================================
AutomationLaneEditor* TrackAutomationLanes::getEditorForLane(synth::LaneId lane) const {
    for (const auto& editor : editors_)
        if (editor->isVisible() && editor->getActiveLane() == lane)
            return editor.get();
    return nullptr;
}

TrackLaneHeaderComponent* TrackAutomationLanes::getLaneHeaderForLane(synth::LaneId lane) const {
    for (const auto& header : headers_)
        if (header->isVisible() && header->getLane() == lane)
            return header.get();
    return nullptr;
}

int TrackAutomationLanes::getVisibleEditorCount() const {
    int count = 0;
    for (const auto& editor : editors_)
        count += editor->isVisible() ? 1 : 0;
    return count;
}

int TrackAutomationLanes::getVisibleLaneHeaderCount() const {
    int count = 0;
    for (const auto& header : headers_)
        count += header->isVisible() ? 1 : 0;
    return count;
}

} // namespace synth::ui
