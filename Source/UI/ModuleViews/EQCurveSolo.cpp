// EQCurveSolo.cpp -- how EQCurveComponent auditions one band: the S key and the Option-click start and end it, the
// selection carries it along, and the curve that started it ends it. The look of a soloed dot is EQCurveGlide.cpp
// (the ease) and EQCurvePaint.cpp (the drawing). EQCurveComponent is declared in EQCurveComponent.h.

#include "EQCurveComponent.h"

// The module keeps one solo for all the curves on it; `ownsSolo_` marks the one this curve started, which is the only
// one it ends on its own (Escape, a selection that has no band to follow, its destruction).
void EQCurveComponent::startSolo(int band) {
    if (band < 0 || !eqModule.isBandEnabled(band))
        return;
    eqModule.setSoloBand(band);
    ownsSolo_ = true;
    ownedSoloBand_ = band;
    syncSolo();
}

void EQCurveComponent::endSolo() {
    ownsSolo_ = false;
    eqModule.clearSolo();
    syncSolo();
}

bool EQCurveComponent::toggleSoloSelectedBand() {
    if (selectedBand < 0 || !eqModule.isBandEnabled(selectedBand))
        return false;
    if (eqModule.getSoloBand() == selectedBand)
        endSolo();
    else
        startSolo(selectedBand);
    return true;
}

// A solo this curve started follows the selection, so stepping through the bands auditions each in turn; a selection
// with nothing on to hear ends it.
void EQCurveComponent::followSelectionSolo() {
    if (selectedBand >= 0 && eqModule.isBandEnabled(selectedBand))
        startSolo(selectedBand);
    else
        endSolo();
}
