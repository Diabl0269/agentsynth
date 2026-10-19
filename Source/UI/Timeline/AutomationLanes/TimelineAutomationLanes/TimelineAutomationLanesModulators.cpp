// Concern: TimelineAutomationLanes' modulator rows -- the routings into each visible lane's CV jack,
// re-derived from the graph through the host, pooled per lane, and their live values and amounts -- and
// which lanes are really a modulator's amount lane.
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"

#include "AppUndoManager.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace synth::ui {

namespace {
constexpr int kMinModulatorRowHeight = 12;

bool sameRoutings(const std::vector<ModulatorInfo>& a, const std::vector<ModulatorInfo>& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].key() != b[i].key())
            return false;
    return true;
}

bool sameRoutingSet(const std::vector<ModulatorInfo>& a, const std::vector<ModulatorInfo>& b) {
    if (a.size() != b.size())
        return false;
    return std::all_of(b.begin(), b.end(), [&a](const ModulatorInfo& info) {
        return std::any_of(a.begin(), a.end(), [&info](const ModulatorInfo& x) { return x.key() == info.key(); });
    });
}

template <typename Map>
void renameKey(Map& map, const juce::String& from, const juce::String& to) {
    auto node = map.extract(from);
    if (node.empty())
        return;
    node.key() = to;
    map.insert(std::move(node));
}

bool hasKey(const std::vector<ModulatorInfo>& infos, const juce::String& key) {
    return std::any_of(infos.begin(), infos.end(), [&key](const ModulatorInfo& info) { return info.key() == key; });
}
} // namespace

int TimelineAutomationLanes::modulatorRowHeight(synth::LaneId lane) const {
    return std::max(kMinModulatorRowHeight, (int)std::llround((double)ModulatorRow::kBaseHeight *
                                                              viewState_.rowHeightScale * laneHeightScale(lane)));
}

// Asks the graph, once per sync, what routes into every lane's CV jack, and from that which lanes are only
// a modulator's amount lane: the `amount` lane of a hidden Attenuverter that one of those routings runs
// through is drawn as that modulator row's band, not as a lane row. Matched by the Attenuverter's uuid, never
// by the parameter name alone (other modules have an `amount` too). An amount lane the graph itself modulates,
// or one whose routing reaches no lane in the doc, stays a lane row, so nothing it carries is ever hidden.
// Folded tracks are asked too: their fold arrow's lane count leaves the amount lanes out. Returns true when
// the set of such lanes changed, i.e. when the lane pools and the layout must follow.
bool TimelineAutomationLanes::deriveRoutings() {
    ++routingDerivations_;
    routings_.clear();
    std::vector<synth::LaneId> ids;
    std::vector<std::pair<juce::String, juce::String>> lanes;
    for (const auto& track : doc_->getTracks())
        for (const auto& lane : track.lanes) {
            ids.push_back(lane.id);
            lanes.emplace_back(lane.nodeUuid, lane.paramId);
        }
    auto infos = host_ != nullptr ? host_->getModulatorsForLanes(lanes) : std::vector<std::vector<ModulatorInfo>>{};
    for (size_t i = 0; i < ids.size(); ++i)
        routings_[ids[i]] = i < infos.size() ? std::move(infos[i]) : std::vector<ModulatorInfo>{};

    std::set<synth::LaneId> amounts;
    for (const auto& [laneId, infos] : routings_) {
        for (const auto& info : infos) {
            const auto* amount = amountLaneFor(*doc_, info.attenuverterUuid);
            if (amount != nullptr && amount->id != laneId && routings_[amount->id].empty())
                amounts.insert(amount->id);
        }
    }
    noteReplacedRoutings();
    // A folded row's state goes with its routing (to the routing that replaced it in place, see noteReplacedRoutings):
    // a routing that is gone starts open if it ever comes back.
    std::set<juce::String> live;
    for (const auto& [laneId, infos] : routings_)
        for (const auto& info : infos)
            live.insert(info.key());
    for (auto it = collapsedMods_.begin(); it != collapsedMods_.end();)
        it = live.count(*it) == 0 ? collapsedMods_.erase(it) : std::next(it);
    for (auto it = foldTweens_.begin(); it != foldTweens_.end();)
        it = live.count(it->first) == 0 ? foldTweens_.erase(it) : std::next(it);

    const bool changed = amounts != amountLanes_;
    amountLanes_ = std::move(amounts);
    return changed;
}

// Each lane's routings in row order, against the same from the last derivation. Every edit that re-cables a routing
// gives it a new Attenuverter and so a new key: Change parameter re-cables each routing from the same source, Change
// source puts a new source into the old routing's slot, and an undo of either swaps them back. Seen by key alone that
// is a row removed and another added; replacedRoutings pairs them instead, and the replaced routing hands its fold
// state and glide to its successor here, and its row and band in syncModulators (rekeyed_), so nothing jumps or
// re-fades.
void TimelineAutomationLanes::noteReplacedRoutings() {
    rekeyed_.clear();
    std::map<synth::LaneId, std::vector<ModulatorInfo>> shown;
    for (const auto& track : doc_->getTracks())
        for (const auto& lane : track.lanes) {
            auto& now = shown[lane.id] = sortedByModulatorOrder(routings_[lane.id], lane.modulatorOrder);
            if (const auto last = shownRoutings_.find(lane.id); last != shownRoutings_.end())
                for (const auto& [from, to] : replacedRoutings(last->second, now))
                    rekeyed_[from] = to;
        }
    shownRoutings_ = std::move(shown);
    for (const auto& [from, to] : rekeyed_) {
        if (collapsedMods_.erase(from) > 0)
            collapsedMods_.insert(to);
        renameKey(foldTweens_, from, to);
        renameKey(glideTweens_, from, to);
    }
}

bool TimelineAutomationLanes::isAmountLane(synth::LaneId lane) const { return amountLanes_.count(lane) > 0; }

int TimelineAutomationLanes::hiddenLaneCount(synth::TrackId track) const {
    const auto* t = doc_ != nullptr ? doc_->getTrack(track) : nullptr;
    int count = 0;
    if (t != nullptr)
        for (const auto& lane : t->lanes)
            count += isAmountLane(lane.id) ? 1 : 0;
    return count;
}

// The rows are not stored anywhere: the graph is the truth, so whatever routes into the lane's CV jack
// -- added from the lane's menu or patched by hand on the canvas -- is a row. A refresh that finds the
// same routings keeps the same rows (keyboard focus and an in-flight drag survive it) and only re-reads
// their titles, colours, values and amounts; a routing replaced in place re-binds its row (rekeyModulators); any other
// change to the set of routings rebuilds that lane's rows.
// Returns true when any lane's rows were rebuilt or dropped, i.e. when the layout must be redone.
bool TimelineAutomationLanes::syncModulators() {
    sweepLeavingModulators();
    bool rebuilt = false;
    std::set<synth::LaneId> visible;
    for (const auto& track : doc_->getTracks()) {
        if (!isVisibleLane(track))
            continue;
        for (const auto& lane : track.lanes) {
            if (isAmountLane(lane.id))
                continue;
            visible.insert(lane.id);
            auto infos = sortedByModulatorOrder(routings_[lane.id], lane.modulatorOrder);
            const auto parameterName = laneLabelsFor(lane, host_).parameter;
            const bool shownBefore = modulators_.count(lane.id) > 0; // a block growing in brings its rows with it
            auto& entry = modulators_[lane.id];
            rekeyModulators(entry, lane.id, infos, parameterName);
            if (!sameRoutings(entry.infos, infos)) {
                if (!sameRoutingSet(entry.infos, infos)) {
                    rebuildModulators(entry, lane.id, std::move(infos), parameterName, shownBefore);
                    rebuilt = true;
                    continue;
                }
                permuteModulators(entry, infos); // only the order changed: the same rows take their new slots
                rebuilt = true;
            }
            for (size_t i = 0; i < infos.size(); ++i) {
                entry.rows[i]->setInfo(infos[i], parameterName);
                entry.rows[i]->refreshValues();
                entry.bands[i]->setModulator(infos[i], lane.id, parameterName);
            }
            entry.infos = std::move(infos);
        }
    }
    applyModulatorFold();
    refreshModulatorAmounts(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    if (!dragModKeys_.empty() && modDrag_.isReordering()) {
        // The dragged routing went (removed, undone, moved by the drop itself): the gesture ends with it.
        const auto entry = modulators_.find(dragModLane_);
        const bool stays = entry != modulators_.end() &&
                           std::any_of(entry->second.infos.begin(), entry->second.infos.end(),
                                       [this](const ModulatorInfo& info) { return info.key() == dragModInfo_.key(); });
        if (!stays)
            discardModulatorDrag();
    }
    for (auto it = modulators_.begin(); it != modulators_.end();) {
        if (visible.count(it->first) > 0) {
            ++it;
            continue;
        }
        rebuilt = rebuilt || !it->second.rows.empty();
        it = modulators_.erase(it);
    }
    return rebuilt;
}

// A routing replaced in place keeps its row and band: they re-bind to the routing that took its slot (the row swaps its
// name and icon in place) instead of one fading out while another fades in. Keyboard focus and a fold survive it.
void TimelineAutomationLanes::rekeyModulators(LaneModulators& entry, synth::LaneId lane,
                                              const std::vector<ModulatorInfo>& infos,
                                              const juce::String& parameterName) {
    for (size_t i = 0; i < entry.infos.size() && i < entry.rows.size(); ++i) {
        const auto to = rekeyed_.find(entry.infos[i].key());
        if (to == rekeyed_.end())
            continue;
        const auto now = std::find_if(infos.begin(), infos.end(),
                                      [&to](const ModulatorInfo& info) { return info.key() == to->second; });
        if (now == infos.end() || hasKey(entry.infos, now->key()))
            continue;
        entry.rows[i]->setRouting(*now, parameterName);
        entry.bands[i]->setModulator(*now, lane, parameterName);
        entry.infos[i] = *now;
    }
}

void TimelineAutomationLanes::wireBand(ModulatorBand& band) const {
    band.setTimelineDoc(doc_);
    band.setHost(host_);
    band.setUndoManager(undo_);
    band.setTransport(transport_);
    band.setEditTool(editTool_);
    band.setDrawShape(drawShape_);
}

void TimelineAutomationLanes::rebuildModulators(LaneModulators& entry, synth::LaneId lane,
                                                std::vector<ModulatorInfo> infos, const juce::String& parameterName,
                                                bool growNewRows) {
    std::set<juce::String> known; // routings that already had a row: they do not fade in again
    for (const auto& old : entry.infos)
        known.insert(old.key());
    std::set<juce::String> staying;
    for (const auto& info : infos)
        staying.insert(info.key());
    entry.fades.clear();
    retireModulatorRows(entry, lane, staying);
    entry.infos = std::move(infos);
    for (const auto& info : entry.infos) {
        auto row = std::make_unique<ModulatorRow>(info, host_, parameterName, &laneControls_);
        headerParent_.addAndMakeVisible(*row);
        entry.rows.push_back(std::move(row));
        auto band = std::make_unique<ModulatorBand>(viewState_);
        wireBand(*band);
        auto* rowPtr = entry.rows.back().get();
        rowPtr->onFocusMoveRequested = [this, rowPtr](int direction) {
            if (onFocusMoveRequested)
                onFocusMoveRequested(*rowPtr, direction);
        };
        rowPtr->onKeyboardFocused = [this, rowPtr] {
            if (onKeyboardStopFocused)
                onKeyboardStopFocused(*rowPtr);
        };
        rowPtr->getControlStrip().onTrayExtentChanged = [this] {
            if (onLayoutChanged)
                onLayoutChanged();
        };
        rowPtr->onFoldRequested = [this, rowPtr](bool collapse) {
            setModulatorCollapsed(rowPtr->getInfo().key(), collapse);
        };
        rowPtr->onMoveKey = [this, lane, rowPtr](const juce::KeyPress& key) {
            return handleModulatorKey(lane, rowPtr->getInfo(), key);
        };
        wireModulatorDrag(*rowPtr, lane);
        band->onMoveKey = [this, lane, bandPtr = band.get()](const juce::KeyPress& key) {
            return handleModulatorKey(lane, bandPtr->getInfo(), key);
        };
        band->onMenuRequested = [row = entry.rows.back().get()](const juce::PopupMenu::Options& options) {
            row->showMenuAt(options);
        };
        band->setModulator(info, lane, parameterName);
        bodies_->addAndMakeVisible(*band);
        entry.bands.push_back(std::move(band));
        if (known.count(info.key()) == 0) {
            // A new routing's row and band come in with a fade, and grow from nothing in a lane already on screen; off
            // screen they simply land shown.
            if (growNewRows)
                growIn(rowMotionKey(lane, info.key()));
            auto* newRow = entry.rows.back().get();
            auto* newBand = entry.bands.back().get();
            newRow->setVisible(false);
            newBand->setVisible(false);
            entry.fades.push_back(
                std::make_unique<synth::ui::FadeVisibility>(std::initializer_list<juce::Component*>{newRow, newBand}));
            entry.fades.back()->setShown(true);
        }
    }
}

// A row whose routing is gone (removed, moved to another lane, undone) fades out in its slot while the gap it leaves
// closes, when it is on screen; otherwise it is dropped at once. Rows whose routing stays are rebuilt in place.
void TimelineAutomationLanes::retireModulatorRows(LaneModulators& entry, synth::LaneId lane,
                                                  const std::set<juce::String>& staying) {
    sweepLeavingModulators();
    const bool shrink = canAnimateBlocks();
    int stayingAbove = 0;
    bool started = false;
    for (size_t i = 0; i < entry.rows.size(); ++i) {
        if (staying.count(entry.infos[i].key()) > 0) {
            ++stayingAbove;
            continue;
        }
        if (!FadeVisibility::canAnimateIn(&headerParent_))
            continue;
        auto leaving = std::make_unique<LeavingModulator>();
        leaving->lane = lane;
        leaving->slot = stayingAbove;
        leaving->height = modulatorRowHeightFor(lane, (int)i) + trayExtentOf(lane, (int)i);
        leaving->presence = shrink ? FoldTween{1.0f, 0.0f, 1.0f} : FoldTween{0.0f, 0.0f, 0.0f};
        started = started || shrink;
        entry.rows[i]->setLift(0.0f);
        leaving->row = std::move(entry.rows[i]);
        leaving->band = std::move(entry.bands[i]);
        leaving->fade = std::make_unique<FadeVisibility>(
            std::initializer_list<juce::Component*>{leaving->row.get(), leaving->band.get()});
        leaving->fade->setShown(false);
        // A row dropped on another lane is already drawn there (it grows in at its new slot): here only its gap closes.
        if (entry.infos[i].key() == droppedRoutingKey_)
            leaving->fade->snapTo(false);
        leaving_.push_back(std::move(leaving));
    }
    entry.rows.clear();
    entry.bands.clear();
    if (started)
        startBlockMotion();
}

// Drops the rows that have faded out and whose gap has closed (never from inside their own fade callback).
void TimelineAutomationLanes::sweepLeavingModulators() {
    leaving_.erase(std::remove_if(leaving_.begin(), leaving_.end(),
                                  [](const auto& l) {
                                      return !l->fade->isFading() && !l->fade->isShown() && l->presence.now <= 0.0f;
                                  }),
                   leaving_.end());
}

// The graph changed under the timeline (an LFO added or removed, a cable patched, an undo): re-derive
// the rows, and relayout only when a lane's set of rows (or of shown lanes) actually changed.
void TimelineAutomationLanes::refreshModulators() {
    if (doc_ == nullptr)
        return;
    if (modulatorsHeld()) {
        modulatorsPending_ = true;
        return;
    }
    // A cable patched or removed by hand can turn an amount lane back into a lane row (or the reverse),
    // which the lane pools and the track's lane count must follow.
    const bool amountLanesChanged = deriveRoutings();
    if (amountLanesChanged)
        syncPools();
    const bool rebuilt = syncModulators();
    if ((amountLanesChanged || rebuilt) && onLayoutChanged)
        onLayoutChanged();
}

// Change source cables the new routing, re-keys the amount lane, writes the order and cuts the old routing; an undo
// restores the timeline before the graph. Derived at any step in between, the rows would see a routing come and another
// go in separate changes (a row fading in, another out, its fold dropped) instead of one routing replacing another.
bool TimelineAutomationLanes::modulatorsHeld() const {
    return modulatorHolds_ > 0 || (undo_ != nullptr && undo_->isRestoring());
}

// The last hold went (or an undo step settled): one refresh for everything that came while held.
void TimelineAutomationLanes::releaseHeldModulators() {
    if (modulatorsHeld() || !modulatorsPending_)
        return;
    modulatorsPending_ = false;
    refreshModulators();
}

TimelineAutomationLanes::ModulatorHold::ModulatorHold(TimelineAutomationLanes& lanes)
    : lanes_(lanes) {
    ++lanes_.modulatorHolds_;
}

TimelineAutomationLanes::ModulatorHold::~ModulatorHold() {
    --lanes_.modulatorHolds_;
    lanes_.releaseHeldModulators();
}

// Rides the panel's existing transport poll; only rows on screen ask the host, and each control
// writes itself only when its value moved.
void TimelineAutomationLanes::tickModulatorValues(int visibleTop, int visibleBottom) {
    for (auto& [id, entry] : modulators_) {
        for (auto& row : entry.rows) {
            const auto b = row->getBounds();
            if (b.getBottom() > visibleTop && b.getY() < visibleBottom)
                row->refreshValues();
        }
    }
    refreshModulatorAmounts(visibleTop, visibleBottom);
}

// What each routing's amount is now: its amount lane at the playhead, else the Attenuverter's knob. The row
// shows it, the band speaks it and draws its flat line from it; both are in the owning track's colour.
void TimelineAutomationLanes::refreshModulatorAmounts(int visibleTop, int visibleBottom) {
    const auto unassigned = unassignedColour();
    const double beat = std::max(0.0, lastReadoutBeat_);
    for (auto& [laneId, entry] : modulators_) {
        const auto colour = laneColourFor(*doc_, laneId, unassigned);
        for (size_t i = 0; i < entry.rows.size(); ++i) {
            auto& row = *entry.rows[i];
            const auto b = row.getBounds();
            if (b.getBottom() <= visibleTop || b.getY() >= visibleBottom)
                continue;
            const auto& info = entry.infos[i];
            row.setTrackColour(colour);
            entry.bands[i]->setTrackColour(colour);
            if (info.attenuverterUuid.isEmpty()) {
                row.setAmount(std::nullopt);
                continue;
            }
            const auto* lane = amountLaneFor(*doc_, info.attenuverterUuid);
            const double amount =
                lane != nullptr
                    ? laneValueAt(*lane, beat)
                    : (host_ != nullptr ? (double)host_->getNodeParameter(info.attenuverterUuid, kAmountParamId) : 0.0);
            row.setAmount(amount);
            entry.bands[i]->setAmountReadout(amount);
        }
    }
}

std::vector<juce::Component*> TimelineAutomationLanes::keyboardStopsFor(const synth::Track& track) const {
    std::vector<juce::Component*> stops;
    if (!isVisibleLane(track))
        return stops;
    for (const auto& lane : track.lanes) {
        auto* header = headerFor(lane.id);
        if (isAmountLane(lane.id) || header == nullptr)
            continue;
        stops.push_back(header);
        for (int i = 0; i < modulatorCount(lane.id); ++i)
            stops.push_back(modulatorRowFor(lane.id, i));
    }
    return stops;
}

int TimelineAutomationLanes::modulatorCount(synth::LaneId lane) const {
    const auto it = modulators_.find(lane);
    return it != modulators_.end() ? (int)it->second.rows.size() : 0;
}

ModulatorRow* TimelineAutomationLanes::modulatorRowFor(synth::LaneId lane, int index) const {
    const auto it = modulators_.find(lane);
    if (it == modulators_.end() || index < 0 || index >= (int)it->second.rows.size())
        return nullptr;
    return it->second.rows[(size_t)index].get();
}

ModulatorBand* TimelineAutomationLanes::modulatorBandFor(synth::LaneId lane, int index) const {
    const auto it = modulators_.find(lane);
    if (it == modulators_.end() || index < 0 || index >= (int)it->second.bands.size())
        return nullptr;
    return it->second.bands[(size_t)index].get();
}

} // namespace synth::ui
