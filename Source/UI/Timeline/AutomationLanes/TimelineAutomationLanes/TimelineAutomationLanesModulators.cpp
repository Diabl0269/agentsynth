// Concern: TimelineAutomationLanes' modulator rows -- the routings into each visible lane's CV jack,
// re-derived from the graph through the host, pooled per lane, and their live values -- and which lanes
// are really a modulator's sections.
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"

#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorSections.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <cmath>

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
// a modulator's sections: an LFO's `level` lane, on the same track as a lane the LFO modulates, is drawn as
// that modulator row's band and not as a lane row. A sections lane the graph itself modulates stays a lane
// row, so nothing it carries is ever hidden. Folded tracks are asked too: their fold arrow's lane count
// leaves the sections out. Returns true when the set of such lanes changed, i.e. when the lane pools and the
// layout must follow.
bool TimelineAutomationLanes::deriveRoutings() {
    routings_.clear();
    for (const auto& track : doc_->getTracks())
        for (const auto& lane : track.lanes)
            routings_[lane.id] =
                host_ != nullptr ? host_->getModulators(lane.nodeUuid, lane.paramId) : std::vector<ModulatorInfo>{};

    std::set<synth::LaneId> sections;
    for (const auto& track : doc_->getTracks()) {
        for (const auto& lane : track.lanes) {
            for (const auto& info : routings_[lane.id]) {
                const auto* level = info.isLfo ? sectionsLaneFor(*doc_, info.sourceUuid) : nullptr;
                if (level != nullptr && level->id != lane.id && doc_->getTrackForLane(level->id) == &track &&
                    routings_[level->id].empty())
                    sections.insert(level->id);
            }
        }
    }
    const bool changed = sections != sectionsLanes_;
    sectionsLanes_ = std::move(sections);
    return changed;
}

bool TimelineAutomationLanes::isSectionsLane(synth::LaneId lane) const { return sectionsLanes_.count(lane) > 0; }

int TimelineAutomationLanes::hiddenLaneCount(synth::TrackId track) const {
    const auto* t = doc_ != nullptr ? doc_->getTrack(track) : nullptr;
    int count = 0;
    if (t != nullptr)
        for (const auto& lane : t->lanes)
            count += isSectionsLane(lane.id) ? 1 : 0;
    return count;
}

// The rows are not stored anywhere: the graph is the truth, so whatever routes into the lane's CV jack
// -- added from the lane's menu or patched by hand on the canvas -- is a row. A refresh that finds the
// same routings keeps the same rows (keyboard focus and an in-flight drag survive it) and only re-reads
// their titles, colours, values and sections; a changed set of routings rebuilds that lane's rows.
// Returns true when any lane's rows were rebuilt or dropped, i.e. when the layout must be redone.
bool TimelineAutomationLanes::syncModulators() {
    bool rebuilt = false;
    std::set<synth::LaneId> visible;
    for (const auto& track : doc_->getTracks()) {
        if (!isVisibleLane(track))
            continue;
        for (const auto& lane : track.lanes) {
            if (isSectionsLane(lane.id))
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
    band.setUndoManager(undo_);
    band.setTransport(transport_);
    band.setEditTool(editTool_);
}

void TimelineAutomationLanes::rebuildModulators(LaneModulators& entry, synth::LaneId lane,
                                                std::vector<ModulatorInfo> infos, const juce::String& parameterName) {
    entry.rows.clear();
    entry.bands.clear();
    entry.infos = std::move(infos);
    for (const auto& info : entry.infos) {
        auto row = std::make_unique<ModulatorRow>(info, host_, parameterName);
        headerParent_.addAndMakeVisible(*row);
        entry.rows.push_back(std::move(row));
        auto band = std::make_unique<ModulatorBand>(viewState_);
        wireBand(*band);
        band->setModulator(info, lane, parameterName);
        bodies_->addAndMakeVisible(*band);
        entry.bands.push_back(std::move(band));
    }
}

// The graph changed under the timeline (an LFO added or removed, a cable patched, an undo): re-derive
// the rows, and relayout only when a lane's set of rows (or of shown lanes) actually changed.
void TimelineAutomationLanes::refreshModulators() {
    if (doc_ == nullptr)
        return;
    // A cable patched or removed by hand can turn a sections lane back into a lane row (or the reverse),
    // which the lane pools and the track's lane count must follow.
    const bool sectionsChanged = deriveRoutings();
    if (sectionsChanged)
        syncPools();
    const bool rebuilt = syncModulators();
    if ((sectionsChanged || rebuilt) && onLayoutChanged)
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
