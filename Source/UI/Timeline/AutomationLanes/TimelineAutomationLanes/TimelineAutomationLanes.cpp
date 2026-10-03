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
    , bodies_(std::make_unique<Bodies>())
    , laneDrag_(headerParent, [this] { onLaneDragFrame(); }) {
    laneRange_.onChanged = [this] { laneRangeChanged(); };
}

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
    discardLaneDrag();
    rowOrigin_.clear();
    modulators_.clear();
    routings_.clear();
    amountLanes_.clear();
    addRows_.clear();
    headers_.clear();
    editors_.clear();
    expanded_.clear();
    collapsedUnassigned_.clear();
    laneRange_.clear();
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
    for (auto& [id, entry] : modulators_)
        for (auto& band : entry.bands)
            band->setUndoManager(undo);
    sync();
}

void TimelineAutomationLanes::setTransport(synth::TransportService* transport) {
    transport_ = transport;
    for (auto& [id, editor] : editors_)
        editor->setTransport(transport);
    for (auto& [id, entry] : modulators_)
        for (auto& band : entry.bands)
            band->setTransport(transport);
}

void TimelineAutomationLanes::setEditTool(EditTool tool) {
    editTool_ = tool;
    for (auto& [id, editor] : editors_)
        editor->setEditTool(tool);
    for (auto& [id, entry] : modulators_)
        for (auto& band : entry.bands)
            band->setEditTool(tool);
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

// An open ordinary track shows its "+ Add automation..." row even with no lanes yet: that is how the
// timeline starts automation. The Unassigned section has no add-only state; it exists for its lanes.
bool TimelineAutomationLanes::isVisibleLane(const synth::Track& track) const {
    if (track.lanes.empty() && track.kind == synth::TrackKind::Automation)
        return false;
    return isExpanded(track.id);
}

// Drops fold state for tracks that are gone, then brings both pools in line with the lanes now
// on screen and re-reads everything they show.
void TimelineAutomationLanes::sync() {
    if (doc_ == nullptr) {
        modulators_.clear();
        routings_.clear();
        amountLanes_.clear();
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
    deriveRoutings();
    syncPools();
    syncModulators();
    refreshPooled();
}

// Keyed by LaneId so an edit that keeps a lane (a point drag, a record-mode change, a rename) keeps
// its editor and header: an in-flight gesture or keyboard focus survives the doc notification.
// Only a lane that leaves the screen (folded, moved under a folded track, deleted, or turned into a
// modulator's amount lane) loses them.
void TimelineAutomationLanes::syncPools() {
    std::set<synth::LaneId> wanted;
    for (const auto& track : doc_->getTracks())
        if (isVisibleLane(track))
            for (const auto& lane : track.lanes)
                if (!isAmountLane(lane.id))
                    wanted.insert(lane.id);

    // A lane that leaves the screen mid-drag takes its header (and the gesture) with it.
    if (laneDrag_.isReordering() && wanted.count(liftedLane_) == 0 && doc_->getLane(liftedLane_) == nullptr)
        discardLaneDrag();
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
            editor->setDrawShape(drawShape_);
            editor->setLaneRange(&laneRange_);
            editor->setClipboard(&pointClipboard_);
            editor->setActiveLane(id);
            // host_ is read at call time: the host can be set after the editor exists.
            editor->valueToText = [this, id](double value) {
                const auto* lane = doc_ != nullptr ? doc_->getLane(id) : nullptr;
                return lane != nullptr ? laneValueText(*lane, value, host_) : juce::String();
            };
            editor->textToValue = [this, id](const juce::String& text) -> std::optional<double> {
                const auto* lane = doc_ != nullptr ? doc_->getLane(id) : nullptr;
                return lane != nullptr ? laneTextToValue(*lane, text, host_) : std::nullopt;
            };
            editor->laneLabel = [this, id] {
                const auto* lane = doc_ != nullptr ? doc_->getLane(id) : nullptr;
                return lane != nullptr ? laneParameterName(*lane, host_) : juce::String();
            };
            editor->onLaneMenuRequested = [this, id](const juce::PopupMenu::Options& options) {
                if (auto* header = headerFor(id))
                    header->showMenuAt(options);
            };
            editor->onLaneKey = [this, id](const juce::KeyPress& key) { return handleLaneKey(id, key); };
            editor->onSelectionChanged = [this, id] { updateSelectedReadout(id); };
            editor->onFocused = [this, id] {
                if (onLaneFocused)
                    onLaneFocused(id);
            };
            bodies_->addAndMakeVisible(*editor);
            editors_[id] = std::move(editor);
        }
        if (headers_.count(id) == 0) {
            auto header = std::make_unique<AutomationLaneHeaderComponent>(*doc_, id, host_, undo_);
            wireHeader(*header, id);
            headerParent_.addAndMakeVisible(*header);
            headers_[id] = std::move(header);
        }
    }
}

// One "+ Add automation..." row per open track, closing its lane rows (the only row of a track with no lanes).
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
        editor->laneDocChanged();
    }
    for (const auto& [id, header] : headers_)
        updateSelectedReadout(id);
}

juce::Colour TimelineAutomationLanes::unassignedColour() const {
    return synth::theme::themeOf(headerParent_).colors.textMuted;
}

AutomationLaneEditor* TimelineAutomationLanes::editorFor(synth::LaneId lane) const {
    const auto it = editors_.find(lane);
    return it != editors_.end() ? it->second.get() : nullptr;
}

AutomationLaneEditor* TimelineAutomationLanes::focusedEditor() const {
    for (const auto& [id, editor] : editors_)
        if (editor->hasKeyboardFocus(true))
            return editor.get();
    return nullptr;
}

synth::LaneId TimelineAutomationLanes::focusedLane() const {
    if (const auto* editor = focusedEditor())
        return editor->getActiveLane();
    for (const auto& [id, header] : headers_)
        if (header->hasKeyboardFocus(true))
            return id;
    return {};
}

bool TimelineAutomationLanes::requestDuplicateLane(synth::LaneId lane) {
    auto* header = headerFor(lane);
    if (header == nullptr)
        return false;
    header->openDuplicatePicker();
    return true;
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
