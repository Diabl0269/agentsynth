// Concern: TimelineAutomationLanes' wiring, fold state and the doc-driven pools of lane headers and
// lane editors. Geometry, placement and the value readouts live in TimelineAutomationLanesLayout.cpp.
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"

namespace synth::ui {

// Takes clicks only on its editors: a y between them (a section row, a gap) falls through to the
// clip lanes underneath, which decide for themselves whether that y is theirs.
TimelineAutomationLanes::Bodies::Bodies() {
    setComponentID("automationLaneBodies");
    setInterceptsMouseClicks(false, true);
}

TimelineAutomationLanes::TimelineAutomationLanes(TimelineViewState& viewState, juce::Component& headerParent)
    : viewState_(viewState)
    , headerParent_(headerParent)
    , bodies_(std::make_unique<Bodies>()) {}

// Pooled children are removed from their parents before they die, so neither parent ever holds a
// dangling child pointer during the owner's own teardown.
TimelineAutomationLanes::~TimelineAutomationLanes() {
    modulators_.clear();
    addRows_.clear();
    headers_.clear();
    editors_.clear();
}

juce::Component& TimelineAutomationLanes::getBodies() noexcept { return *bodies_; }

// A new doc invalidates every lane id and track id the pools and fold sets were keyed by.
void TimelineAutomationLanes::setTimelineDoc(synth::TimelineDoc* doc) {
    modulators_.clear();
    addRows_.clear();
    headers_.clear();
    editors_.clear();
    expanded_.clear();
    collapsedUnassigned_.clear();
    doc_ = doc;
    sync();
}

// The lane headers and modulator rows hold the host from construction, so a change rebuilds them.
void TimelineAutomationLanes::setHost(TrackHeaderHost* host) {
    host_ = host;
    modulators_.clear();
    headers_.clear();
    sync();
}

void TimelineAutomationLanes::setUndoManager(AppUndoManager* undo) {
    undo_ = undo;
    headers_.clear();
    for (auto& [id, editor] : editors_)
        editor->setUndoManager(undo);
    sync();
}

void TimelineAutomationLanes::setTransport(synth::TransportService* transport) {
    transport_ = transport;
    for (auto& [id, editor] : editors_)
        editor->setTransport(transport);
}

void TimelineAutomationLanes::setEditTool(EditTool tool) {
    editTool_ = tool;
    for (auto& [id, editor] : editors_)
        editor->setEditTool(tool);
}

// An ordinary track starts folded; the Automation track (lanes no single track owns) starts open,
// because a lane that lands there has no other place on screen to be seen.
bool TimelineAutomationLanes::isExpanded(synth::TrackId track) const {
    const auto* t = doc_ != nullptr ? doc_->getTrack(track) : nullptr;
    if (t == nullptr)
        return false;
    if (t->kind == synth::TrackKind::Automation)
        return collapsedUnassigned_.count(track) == 0;
    return expanded_.count(track) > 0;
}

void TimelineAutomationLanes::setExpanded(synth::TrackId track, bool expanded) {
    const auto* t = doc_ != nullptr ? doc_->getTrack(track) : nullptr;
    if (t == nullptr || isExpanded(track) == expanded)
        return;
    if (t->kind == synth::TrackKind::Automation) {
        if (expanded)
            collapsedUnassigned_.erase(track);
        else
            collapsedUnassigned_.insert(track);
    } else if (expanded) {
        expanded_.insert(track);
    } else {
        expanded_.erase(track);
    }
    sync();
    if (onLayoutChanged)
        onLayoutChanged();
}

bool TimelineAutomationLanes::isVisibleLane(const synth::Track& track) const {
    return !track.lanes.empty() && isExpanded(track.id);
}

// Drops fold state for tracks that are gone, then brings both pools in line with the lanes now
// on screen and re-reads everything they show.
void TimelineAutomationLanes::sync() {
    if (doc_ == nullptr) {
        modulators_.clear();
        addRows_.clear();
        headers_.clear();
        editors_.clear();
        return;
    }
    const auto prune = [this](std::set<synth::TrackId>& ids) {
        for (auto it = ids.begin(); it != ids.end();)
            it = doc_->getTrack(*it) == nullptr ? ids.erase(it) : std::next(it);
    };
    prune(expanded_);
    prune(collapsedUnassigned_);
    syncPools();
    syncModulators();
    refreshPooled();
}

// Keyed by LaneId so an edit that keeps a lane (a point drag, a record-mode change, a rename) keeps
// its editor and header: an in-flight gesture or keyboard focus survives the doc notification.
// Only a lane that leaves the screen (folded, moved under a folded track, deleted) loses them.
void TimelineAutomationLanes::syncPools() {
    std::set<synth::LaneId> wanted;
    for (const auto& track : doc_->getTracks())
        if (isVisibleLane(track))
            for (const auto& lane : track.lanes)
                wanted.insert(lane.id);

    for (auto it = editors_.begin(); it != editors_.end();)
        it = wanted.count(it->first) == 0 ? editors_.erase(it) : std::next(it);
    for (auto it = headers_.begin(); it != headers_.end();)
        it = wanted.count(it->first) == 0 ? headers_.erase(it) : std::next(it);

    syncAddRows();
    for (const auto id : wanted) {
        if (editors_.count(id) == 0) {
            auto editor = std::make_unique<AutomationLaneEditor>(viewState_);
            editor->setTimelineDoc(doc_);
            editor->setUndoManager(undo_);
            editor->setTransport(transport_);
            editor->setEditTool(editTool_);
            editor->setActiveLane(id);
            editor->onFocused = [this, id] {
                if (onLaneFocused)
                    onLaneFocused(id);
            };
            bodies_->addAndMakeVisible(*editor);
            editors_[id] = std::move(editor);
        }
        if (headers_.count(id) == 0) {
            auto header = std::make_unique<AutomationLaneHeaderComponent>(*doc_, id, host_, undo_);
            headerParent_.addAndMakeVisible(*header);
            headers_[id] = std::move(header);
        }
    }
}

// One "+ Add automation..." row per track whose lanes are open, closing its lane rows. The same gate as
// the lane rows: a track with no lanes shows nothing here and is reached through its header's context menu.
void TimelineAutomationLanes::syncAddRows() {
    for (auto it = addRows_.begin(); it != addRows_.end();) {
        const auto* track = doc_->getTrack(it->first);
        it = (track == nullptr || !isVisibleLane(*track)) ? addRows_.erase(it) : std::next(it);
    }
    for (const auto& track : doc_->getTracks()) {
        if (!isVisibleLane(track) || addRows_.count(track.id) > 0)
            continue;
        auto row = std::make_unique<AddAutomationRow>(track.id);
        AddAutomationRow* raw = row.get();
        row->onAddRequested = [this, raw](synth::TrackId id) {
            if (onAddAutomationRequested)
                onAddAutomationRequested(id, *raw);
        };
        headerParent_.addAndMakeVisible(*row);
        addRows_[track.id] = std::move(row);
    }
}

void TimelineAutomationLanes::refreshPooled() {
    for (auto& [id, row] : addRows_)
        if (const auto* track = doc_->getTrack(id))
            row->setTrackName(track->name);
    const auto unassigned = unassignedColour();
    for (auto& [id, header] : headers_) {
        header->refreshFromDoc();
        header->setReadoutBeat(std::max(0.0, lastReadoutBeat_));
    }
    for (auto& [id, editor] : editors_) {
        editor->setCurveColour(laneColourFor(*doc_, id, unassigned));
        editor->repaint();
    }
}

juce::Colour TimelineAutomationLanes::unassignedColour() const {
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&headerParent_.getLookAndFeel()))
        return lf->getTheme().colors.textMuted;
    return juce::Colour(0xff8A93A0);
}

AutomationLaneEditor* TimelineAutomationLanes::editorFor(synth::LaneId lane) const {
    const auto it = editors_.find(lane);
    return it != editors_.end() ? it->second.get() : nullptr;
}

AutomationLaneHeaderComponent* TimelineAutomationLanes::headerFor(synth::LaneId lane) const {
    const auto it = headers_.find(lane);
    return it != headers_.end() ? it->second.get() : nullptr;
}

AddAutomationRow* TimelineAutomationLanes::addRowFor(synth::TrackId track) const {
    const auto it = addRows_.find(track);
    return it != addRows_.end() ? it->second.get() : nullptr;
}

void TimelineAutomationLanes::repaintEditors() {
    for (auto& [id, editor] : editors_)
        editor->repaint();
}

} // namespace synth::ui
