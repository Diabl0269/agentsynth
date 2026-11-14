// EQCurveInteraction.cpp -- the gestures of EQCurveComponent: hit testing, adding, removing, dragging and Q changes,
// and the mouse handlers that call them. The curve is declared in EQCurveComponent.h.

#include "EQCurveComponent.h"
#include "EQCurveHoldState.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {
using Module = ParametricEQModule;
} // namespace

int EQCurveComponent::hitTestBand(juce::Point<float> p) const {
    int best = -1;
    float bestDistanceSq = kHitRadius * kHitRadius;
    for (int b = 0; b < Module::kMaxBands; ++b) {
        if (!lastBands[(size_t)b].enabled)
            continue;
        const float distanceSq = bandPosition(lastBands[(size_t)b]).getDistanceSquaredFrom(p);
        // On a tie the selected band wins, so a stack of dots never traps the one being edited.
        if (distanceSq < bestDistanceSq || (distanceSq <= bestDistanceSq && b == selectedBand)) {
            best = b;
            bestDistanceSq = distanceSq;
        }
    }
    return best;
}

// Slots 1-4 keep their home-frequency picking (and their own types); past four the lowest free slot is reused as a
// Bell. The band's settings are written before it switches on, so it never plays its old shape.
int EQCurveComponent::addBandAt(float freqHz, float gainDb) {
    int band = eqModule.findBandForNewPoint(freqHz);
    const bool extra = band < 0;
    if (extra)
        band = eqModule.findFreeBand();
    if (band < 0)
        return -1;

    beginGesture();
    if (extra)
        eqModule.setBandType(band, synth::eq::BandType::Peak);
    eqModule.setBandFreq(band, freqHz);
    eqModule.setBandGain(band, gainDb);
    eqModule.setBandQ(band, Module::kDefaultQ);
    eqModule.setBandEnabled(band, true);
    endGesture();

    // Refresh first: selecting an off band whose cached settings are stale would draw its chip at the old frequency.
    refreshFromModule();
    setSelectedBand(band);
    return band;
}

int EQCurveComponent::addPointAt(juce::Point<float> p) { return addBandAt(freqAtX(p.x), gainAtY(p.y)); }

int EQCurveComponent::addBandInLargestGap() {
    std::vector<float> logs;
    for (int b = 0; b < Module::kMaxBands; ++b)
        if (lastBands[(size_t)b].enabled)
            logs.push_back(std::log(lastBands[(size_t)b].freqHz));
    std::sort(logs.begin(), logs.end());
    const float lo = std::log(Module::kMinFreq);
    const float hi = std::log(Module::kMaxFreq);
    logs.insert(logs.begin(), lo);
    logs.push_back(hi);

    float bestMid = 0.5f * (lo + hi);
    float bestGap = -1.0f;
    for (size_t i = 1; i < logs.size(); ++i) {
        const float gap = logs[i] - logs[i - 1];
        if (gap > bestGap) {
            bestGap = gap;
            bestMid = 0.5f * (logs[i] + logs[i - 1]);
        }
    }
    return addBandAt(std::exp(bestMid), 0.0f);
}

// The copy takes the original's settings and lands two semitones higher, so it is told apart from it at once. It is
// written before it switches on, like an added band, so it grows in without ever playing a stale shape.
bool EQCurveComponent::duplicateSelectedBand() {
    if (selectedBand < 0 || !eqModule.isBandEnabled(selectedBand))
        return false;
    const int slot = eqModule.findFreeBand();
    if (slot < 0)
        return false;
    const auto source = eqModule.getBandSnapshots()[(size_t)selectedBand];
    beginGesture();
    eqModule.setBandType(slot, source.type);
    eqModule.setBandSlope(slot, source.slopeDbPerOct);
    eqModule.setBandFreq(
        slot, juce::jlimit(Module::kMinFreq, Module::kMaxFreq, source.freqHz * std::pow(2.0f, 2.0f / 12.0f)));
    eqModule.setBandGain(slot, source.gainDb);
    eqModule.setBandQ(slot, source.q);
    eqModule.setBandEnabled(slot, true);
    endGesture();
    refreshFromModule();
    setSelectedBand(slot);
    return true;
}

void EQCurveComponent::removeBand(int band) {
    if (band < 0 || band >= Module::kMaxBands || !eqModule.isBandEnabled(band))
        return;
    if (eqModule.getSoloBand() == band) {
        ownsSolo_ = false;
        eqModule.clearSolo(); // nothing would be left to hear
    }
    beginGesture();
    eqModule.setBandEnabled(band, false);
    endGesture();
    refreshFromModule();
    if (hold_->selection.contains(band)) { // it leaves the selection; the others stay
        auto rest = hold_->selection.selected();
        rest.erase(std::remove(rest.begin(), rest.end(), band), rest.end());
        const int cursor = rest.empty() ? -1 : (selectedBand == band ? rest.back() : selectedBand);
        setSelection(std::move(rest), cursor, cursor);
    }
}

void EQCurveComponent::dragBandTo(int band, juce::Point<float> p) {
    if (band < 0 || band >= Module::kMaxBands)
        return;
    eqModule.setBandFreq(band, freqAtX(p.x));
    eqModule.setBandGain(band, gainAtY(p.y));
    refreshFromModule();
}

void EQCurveComponent::nudgeBandQ(int band, float amount) {
    if (band < 0 || band >= Module::kMaxBands)
        return;
    const float updated = lastBands[(size_t)band].q * std::pow(2.0f, amount);
    beginGesture();
    eqModule.setBandQ(band, updated);
    endGesture();
    refreshFromModule();
}

// Every selected band that is on moves, each stopping at its own limit; the dots ease to their new places. A press that
// moves nothing (every band at its end) opens no undo step.
bool EQCurveComponent::nudgeSelectedBand(float octaves, float dbDelta) {
    const auto bands = movableSelection();
    std::vector<std::pair<int, juce::Point<float>>> start;
    for (const int band : bands)
        start.push_back({band, {lastBands[(size_t)band].freqHz, lastBands[(size_t)band].gainDb}});
    bool changes = false;
    for (const auto& [band, place] : start) {
        const float freq = juce::jlimit(Module::kMinFreq, Module::kMaxFreq, place.x * std::pow(2.0f, octaves));
        const float gain = juce::jlimit(-Module::kMaxGainDb, Module::kMaxGainDb, place.y + dbDelta);
        changes = changes || freq != place.x || gain != place.y;
    }
    if (!changes)
        return false;
    beginGesture();
    moveBandsFrom(start, octaves, dbDelta);
    endGesture();
    glideMoves_ = true;
    refreshFromModule();
    glideMoves_ = false;
    return true;
}

void EQCurveComponent::selectBand(int band) {
    if (band < 0 || band >= Module::kMaxBands)
        return;
    if (!eqModule.isBandEnabled(band)) {
        beginGesture();
        eqModule.setBandEnabled(band, true);
        endGesture();
    }
    setSelectedBand(band);
    refreshFromModule();
}

// True when an enabled band is selected; with none selected, selects the first enabled one. A selected band that is
// switched off stays selected (and the call is false).
bool EQCurveComponent::ensureSelectedBand() {
    if (selectedBand >= 0)
        return eqModule.isBandEnabled(selectedBand);
    for (int b = 0; b < Module::kMaxBands; ++b) {
        if (eqModule.isBandEnabled(b)) {
            setSelectedBand(b);
            return true;
        }
    }
    return false;
}

void EQCurveComponent::refreshFromModule() {
    syncBands(eqModule.getBandSnapshots(), eqModule.getOutputGainDb(), false); // a gesture lands at once
    repaint();
}

bool EQCurveComponent::bandsDiffer(const Module::BandSnapshots& a, const Module::BandSnapshots& b) {
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].enabled != b[i].enabled || a[i].freqHz != b[i].freqHz || a[i].gainDb != b[i].gainDb ||
            a[i].q != b[i].q || a[i].type != b[i].type || a[i].slopeDbPerOct != b[i].slopeDbPerOct)
            return true;
    return false;
}

void EQCurveComponent::mouseMove(const juce::MouseEvent& e) {
    const int band = hitTestBand(e.position);
    if (band != hoveredBand) {
        hoveredBand = band;
        setMouseCursor(band >= 0 ? juce::MouseCursor::UpDownLeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EQCurveComponent::mouseExit(const juce::MouseEvent&) {
    if (hoveredBand != -1) {
        hoveredBand = -1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

// No undo gesture opens here: a plain click that never turns into a drag would otherwise push an empty undo step.
// beginGesture happens on the first mouseDrag. A right-click on a dot selects it and opens its type menu. A press on
// empty space arms a box (Shift, Cmd or Ctrl keeps the selection); Shift- or Cmd-click on a dot toggles it.
void EQCurveComponent::mouseDown(const juce::MouseEvent& e) {
    dragBand = hitTestBand(e.position);
    collapseOnRelease_ = false;
    dragStart_.clear();
    const bool additive = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
    if (dragBand < 0) {
        if (!e.mods.isPopupMenu())
            hold_->box.press(e.getPosition(), additive, hold_->selection.selected());
        return;
    }
    if (e.mods.isAltDown() &&
        !e.mods.isPopupMenu()) { // Option-click auditions the dot (and ends it) instead of dragging
        const int band = std::exchange(dragBand, -1);
        const bool wasSoloed = eqModule.getSoloBand() == band;
        setSelectedBand(band);
        if (wasSoloed)
            endSolo();
        else
            startSolo(band);
        return;
    }
    if (e.mods.isPopupMenu()) {
        const int band = dragBand;
        dragBand = -1;
        setSelectedBand(band);
        showTypeMenuAtDot(band);
        return;
    }
    if (additive) { // add the band to the selection or take it out; nothing moves
        const int band = std::exchange(dragBand, -1);
        auto bands = hold_->selection.selected();
        const auto at = std::find(bands.begin(), bands.end(), band);
        if (at != bands.end()) {
            bands.erase(at);
            const int cursor = band == selectedBand ? (bands.empty() ? -1 : bands.back()) : selectedBand;
            setSelection(std::move(bands), cursor, cursor);
        } else {
            bands.push_back(band);
            setSelection(std::move(bands), band, band);
        }
        return;
    }
    if (hold_->selection.size() > 1 && hold_->selection.contains(dragBand)) { // a drag moves the whole group
        collapseOnRelease_ = true;
        setSelection(hold_->selection.selected(), dragBand, dragBand);
    } else {
        setSelectedBand(dragBand);
    }
    repaint();
}

void EQCurveComponent::mouseDrag(const juce::MouseEvent& e) {
    if (dragBand < 0) {
        dragBoxTo(e.position);
        return;
    }
    if (!gestureActive) {
        beginGesture();
        gestureActive = true;
        collapseOnRelease_ = false;
        dragStart_.clear();
        for (const int band : hold_->selection.selected())
            if (lastBands[(size_t)band].enabled)
                dragStart_.push_back({band, {lastBands[(size_t)band].freqHz, lastBands[(size_t)band].gainDb}});
    }
    if (dragStart_.size() > 1)
        dragSelectionTo(e.position);
    else
        dragBandTo(dragBand, e.position);
}

void EQCurveComponent::mouseUp(const juce::MouseEvent&) {
    if (gestureActive) {
        endGesture();
        gestureActive = false;
    }
    if (collapseOnRelease_ && dragBand >= 0) // a click on one of several bands, with no drag, picks just that one
        setSelectedBand(dragBand);
    collapseOnRelease_ = false;
    dragStart_.clear();
    dragBand = -1;
    if (hold_->box.release() == synth::ui::PointBoxGesture::Release::click)
        setSelectedBand(-1);
}

void EQCurveComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    const int band = hitTestBand(e.position);
    if (band >= 0)
        removeBand(band);
    else
        addPointAt(e.position);
}

void EQCurveComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    int band = hitTestBand(e.position);
    if (band < 0)
        band = selectedBand;
    if (band < 0 || !eqModule.isBandEnabled(band)) {
        Component::mouseWheelMove(e, wheel); // nothing under the cursor: let a parent scroll view have it
        return;
    }
    nudgeBandQ(band, wheel.deltaY * kQScrollSensitivity);
}
