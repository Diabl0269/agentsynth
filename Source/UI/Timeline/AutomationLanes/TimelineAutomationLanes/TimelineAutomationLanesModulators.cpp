// Concern: TimelineAutomationLanes' modulator rows -- the routings into each visible lane's CV jack,
// re-derived from the graph through the host, pooled per lane, and their live values.
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"

#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"
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

// The rows are not stored anywhere: the graph is the truth, so whatever routes into the lane's CV jack
// -- added from the lane's menu or patched by hand on the canvas -- is a row. A refresh that finds the
// same routings keeps the same rows (keyboard focus and an in-flight drag survive it) and only re-reads
// their titles, colours and values; a changed set of routings rebuilds that lane's rows. Returns true
// when any lane's rows were rebuilt or dropped, i.e. when the layout must be redone.
bool TimelineAutomationLanes::syncModulators() {
    bool rebuilt = false;
    std::set<synth::LaneId> visible;
    for (const auto& track : doc_->getTracks()) {
        if (!isVisibleLane(track))
            continue;
        for (const auto& lane : track.lanes) {
            visible.insert(lane.id);
            auto infos =
                host_ != nullptr ? host_->getModulators(lane.nodeUuid, lane.paramId) : std::vector<ModulatorInfo>{};
            const auto parameterName = laneLabelsFor(lane, host_).parameter;
            auto& entry = modulators_[lane.id];
            if (!sameRoutings(entry.infos, infos)) {
                rebuildModulators(entry, std::move(infos), parameterName);
                rebuilt = true;
                continue;
            }
            for (size_t i = 0; i < infos.size(); ++i) {
                entry.rows[i]->setInfo(infos[i], parameterName);
                entry.rows[i]->refreshValues();
                entry.bands[i]->setColour(infos[i].colour);
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

void TimelineAutomationLanes::rebuildModulators(LaneModulators& entry, std::vector<ModulatorInfo> infos,
                                                const juce::String& parameterName) {
    entry.rows.clear();
    entry.bands.clear();
    entry.infos = std::move(infos);
    for (const auto& info : entry.infos) {
        auto row = std::make_unique<ModulatorRow>(info, host_, parameterName);
        headerParent_.addAndMakeVisible(*row);
        entry.rows.push_back(std::move(row));
        auto band = std::make_unique<ModulatorBand>(info.colour);
        bodies_->addAndMakeVisible(*band);
        entry.bands.push_back(std::move(band));
    }
}

// The graph changed under the timeline (an LFO added or removed, a cable patched, an undo): re-derive
// the rows, and relayout only when a lane's set of rows actually changed.
void TimelineAutomationLanes::refreshModulators() {
    if (doc_ == nullptr)
        return;
    if (syncModulators() && onLayoutChanged)
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
