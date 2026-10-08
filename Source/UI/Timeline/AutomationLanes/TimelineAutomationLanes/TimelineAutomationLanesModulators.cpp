// Concern: TimelineAutomationLanes' modulator rows -- the routings into each visible lane's CV jack,
// re-derived from the graph through the host, pooled per lane, and their live values and amounts -- and
// which lanes are really a modulator's amount lane.
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"

#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
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
} // namespace

int TimelineAutomationLanes::modulatorRowHeight() const {
    return std::max(kMinModulatorRowHeight,
                    (int)std::llround((double)ModulatorRow::kBaseHeight * viewState_.rowHeightScale));
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
    const bool changed = amounts != amountLanes_;
    amountLanes_ = std::move(amounts);
    return changed;
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
// their titles, colours, values and amounts; a changed set of routings rebuilds that lane's rows.
// Returns true when any lane's rows were rebuilt or dropped, i.e. when the layout must be redone.
bool TimelineAutomationLanes::syncModulators() {
    bool rebuilt = false;
    std::set<synth::LaneId> visible;
    for (const auto& track : doc_->getTracks()) {
        if (!isVisibleLane(track))
            continue;
        for (const auto& lane : track.lanes) {
            if (isAmountLane(lane.id))
                continue;
            visible.insert(lane.id);
            auto infos = routings_[lane.id];
            const auto parameterName = laneLabelsFor(lane, host_).parameter;
            auto& entry = modulators_[lane.id];
            if (!sameRoutings(entry.infos, infos)) {
                rebuildModulators(entry, lane.id, std::move(infos), parameterName);
                rebuilt = true;
                continue;
            }
            for (size_t i = 0; i < infos.size(); ++i) {
                entry.rows[i]->setInfo(infos[i], parameterName);
                entry.rows[i]->refreshValues();
                entry.bands[i]->setModulator(infos[i], lane.id, parameterName);
            }
            entry.infos = std::move(infos);
        }
    }
    refreshModulatorAmounts(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
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

void TimelineAutomationLanes::wireBand(ModulatorBand& band) const {
    band.setTimelineDoc(doc_);
    band.setHost(host_);
    band.setUndoManager(undo_);
    band.setTransport(transport_);
    band.setEditTool(editTool_);
    band.setDrawShape(drawShape_);
}

void TimelineAutomationLanes::rebuildModulators(LaneModulators& entry, synth::LaneId lane,
                                                std::vector<ModulatorInfo> infos, const juce::String& parameterName) {
    std::set<juce::String> known; // routings that already had a row: they do not fade in again
    for (const auto& old : entry.infos)
        known.insert(old.key());
    entry.fades.clear();
    entry.rows.clear();
    entry.bands.clear();
    entry.infos = std::move(infos);
    for (const auto& info : entry.infos) {
        auto row = std::make_unique<ModulatorRow>(info, host_, parameterName);
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
        band->onMenuRequested = [row = entry.rows.back().get()](const juce::PopupMenu::Options& options) {
            row->showMenuAt(options);
        };
        band->setModulator(info, lane, parameterName);
        bodies_->addAndMakeVisible(*band);
        entry.bands.push_back(std::move(band));
        if (known.count(info.key()) == 0) {
            // A new routing's row and band come in with a fade; off screen they simply land shown.
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

// The graph changed under the timeline (an LFO added or removed, a cable patched, an undo): re-derive
// the rows, and relayout only when a lane's set of rows (or of shown lanes) actually changed.
void TimelineAutomationLanes::refreshModulators() {
    if (doc_ == nullptr)
        return;
    // A cable patched or removed by hand can turn an amount lane back into a lane row (or the reverse),
    // which the lane pools and the track's lane count must follow.
    const bool amountLanesChanged = deriveRoutings();
    if (amountLanesChanged)
        syncPools();
    const bool rebuilt = syncModulators();
    if ((amountLanesChanged || rebuilt) && onLayoutChanged)
        onLayoutChanged();
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
