// Concern: the proxy doc behind the modulator band's curve editor -- keeping its one lane a mirror of the real
// amount lane (or, with none, an empty lane at the knob's value), and writing every edit the editor makes to it
// into the real doc as one undo step that creates the amount lane on the first stroke and removes it with the
// last point.
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorBand.h"

#include "AppUndoManager.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"
#include <cmath>

namespace synth::ui {

namespace {
bool samePoints(const std::vector<synth::AutomationLane::Breakpoint>& a,
                const std::vector<synth::AutomationLane::Breakpoint>& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].beat != b[i].beat || a[i].value != b[i].value || a[i].tension != b[i].tension ||
            a[i].curve != b[i].curve)
            return false;
    return true;
}
} // namespace

// A lane's range has no setter, so a new default (the knob moved while there is no amount lane) is a new
// proxy lane; the editor is pointed at it. Never rebuilt under a stroke in flight.
void ModulatorBand::rebuildProxyLane(float defaultValue) {
    if (!proxyTrack_.isValid())
        proxyTrack_ = proxy_.addTrack(synth::TrackKind::Midi, "Amount");
    if (proxyLane_.isValid())
        proxy_.removeLane(proxyLane_);
    auto range = amountLaneRange();
    range.defaultValue = defaultValue;
    proxyLane_ = proxy_.addLane(proxyTrack_, info_.attenuverterUuid, kAmountParamId, range);
    if (editor_ != nullptr)
        editor_->setActiveLane(proxyLane_);
}

// The proxy follows the real doc, never the reverse: these writes are the band's own (syncingProxy_), so
// timelineChanged() does not mistake them for an edit to commit.
void ModulatorBand::syncProxy() {
    if (!isEditable() || editor_ == nullptr || editor_->isDragActiveForTest())
        return;
    const juce::ScopedValueSetter<bool> guard(syncingProxy_, true);
    const auto* real = amountLane();
    const float wantedDefault = real != nullptr ? real->range.defaultValue : (float)knobAmount();
    const auto* lane = proxyLane_.isValid() ? proxy_.getLane(proxyLane_) : nullptr;
    if (lane == nullptr || std::abs(lane->range.defaultValue - wantedDefault) > 1.0e-6f) {
        rebuildProxyLane(wantedDefault);
        lane = proxy_.getLane(proxyLane_);
        if (lane == nullptr)
            return;
    }
    const std::vector<synth::AutomationLane::Breakpoint> wanted =
        real != nullptr ? real->points : std::vector<synth::AutomationLane::Breakpoint>{};
    if (samePoints(lane->points, wanted))
        return;
    std::vector<double> removeBeats;
    for (const auto& point : lane->points)
        removeBeats.push_back(point.beat);
    proxy_.editBreakpoints(proxyLane_, removeBeats, wanted);
    repaint();
}

// The editor committed a gesture to the proxy (one mutation per gesture): that gesture becomes ONE timeline
// undo step on the real doc. The proxy is brought back in line afterwards, asynchronously, because a doc must
// not be edited from inside its own change notification.
void ModulatorBand::timelineChanged(const synth::TimelineDoc&) {
    if (syncingProxy_)
        return;
    commitProxy();
    triggerAsyncUpdate();
}

void ModulatorBand::handleAsyncUpdate() {
    syncProxy();
    repaint();
}

void ModulatorBand::commitProxy() {
    if (doc_ == nullptr || !isEditable())
        return;
    const auto* lane = proxy_.getLane(proxyLane_);
    const std::vector<synth::AutomationLane::Breakpoint> points =
        lane != nullptr ? lane->points : std::vector<synth::AutomationLane::Breakpoint>{};
    const auto* real = amountLane();
    if (real != nullptr ? samePoints(real->points, points) : points.empty())
        return;
    writeToRealDoc(points);
}

void ModulatorBand::createLaneWithPoint(double beat, double value) {
    writeToRealDoc({{beat, value, 0.0f, static_cast<int>(synth::BreakpointCurve::Linear)}});
    syncProxy();
}

// Everything the mutation touches is copied in: the doc notification it fires re-syncs the panel, and the
// band must not be read from inside it. committing_ keeps refreshFromDoc() off the proxy while the doc is
// between its mutations (the lane added but not yet filled).
void ModulatorBand::writeToRealDoc(const std::vector<synth::AutomationLane::Breakpoint>& points) {
    auto* doc = doc_;
    const auto track = ownerTrack();
    const auto attenuverter = info_.attenuverterUuid;
    auto mutate = [doc, track, attenuverter, points] { writeAmountLane(*doc, track, attenuverter, points); };
    juce::Component::SafePointer<ModulatorBand> safeThis(this);
    committing_ = true;
    if (undo_ != nullptr)
        undo_->recordTimelineChange(*doc, mutate);
    else
        mutate();
    if (safeThis != nullptr) {
        committing_ = false;
        updateClickRouting();
        applyNames();
    }
}

} // namespace synth::ui
