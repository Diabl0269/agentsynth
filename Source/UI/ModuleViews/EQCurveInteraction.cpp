// EQCurveInteraction.cpp -- the gestures of EQCurveComponent: hit testing, adding, removing, dragging and Q changes,
// and the mouse handlers that call them. The curve is declared in EQCurveComponent.h.

#include "EQCurveComponent.h"

#include <algorithm>
#include <cmath>
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

void EQCurveComponent::removeBand(int band) {
    if (band < 0 || band >= Module::kMaxBands || !eqModule.isBandEnabled(band))
        return;
    beginGesture();
    eqModule.setBandEnabled(band, false);
    endGesture();
    if (selectedBand == band)
        setSelectedBand(-1);
    refreshFromModule();
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

bool EQCurveComponent::nudgeSelectedBand(float octaves, float dbDelta) {
    if (!ensureSelectedBand())
        return false;
    const auto band = lastBands[(size_t)selectedBand];
    const float newFreq = juce::jlimit(Module::kMinFreq, Module::kMaxFreq, band.freqHz * std::pow(2.0f, octaves));
    const float newGain = juce::jlimit(-Module::kMaxGainDb, Module::kMaxGainDb, band.gainDb + dbDelta);
    if (newFreq == band.freqHz && newGain == band.gainDb)
        return false; // already at the end of its range: no undo step for a press that changes nothing
    beginGesture();
    eqModule.setBandFreq(selectedBand, newFreq);
    eqModule.setBandGain(selectedBand, newGain);
    endGesture();
    refreshFromModule();
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

void EQCurveComponent::setSelectedBand(int band) {
    band = juce::jlimit(-1, Module::kMaxBands - 1, band);
    if (selectedBand == band)
        return;
    selectedBand = band;
    refreshAccessibilityValue();
    repaint();
    listeners_.call([](Listener& l) { l.eqCurveChanged(); });
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
// beginGesture happens on the first mouseDrag. A right-click on a dot selects it and opens its type menu.
void EQCurveComponent::mouseDown(const juce::MouseEvent& e) {
    dragBand = hitTestBand(e.position);
    if (dragBand < 0)
        return;
    setSelectedBand(dragBand);
    if (e.mods.isPopupMenu()) {
        const int band = dragBand;
        dragBand = -1;
        showTypeMenuAtDot(band);
        return;
    }
    repaint();
}

void EQCurveComponent::mouseDrag(const juce::MouseEvent& e) {
    if (dragBand < 0)
        return;
    if (!gestureActive) {
        beginGesture();
        gestureActive = true;
    }
    dragBandTo(dragBand, e.position);
}

void EQCurveComponent::mouseUp(const juce::MouseEvent&) {
    if (gestureActive) {
        endGesture();
        gestureActive = false;
    }
    dragBand = -1;
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
