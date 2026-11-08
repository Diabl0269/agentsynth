// EQCurveGlide.cpp -- how EQCurveComponent follows the module: the 30 Hz poll, and the glide an undo or redo gives the
// dots and the curve (EqBandGlide.h). EQCurveComponent is declared (with its mouse and paint code) in
// EQCurveComponent.h.

#include "EQCurveComponent.h"

#include "AppUndoManager.h"

void EQCurveComponent::setUndoManager(AppUndoManager* manager) {
    undoManager_ = manager;
    restoreWatch_ = synth::ui::RestoreWatch(manager != nullptr ? manager->getRestoreSerial() : 0);
}

void EQCurveComponent::syncBands(const synth::ui::EqBandGlide::Bands& bands, float outputGainDb, bool restored) {
    glide_.sync(lastBands, lastOutputGainDb, bands, outputGainDb, restored);
    lastBands = bands;
    lastOutputGainDb = outputGainDb;
    recomputeMagnitudes();
    repaint();
    refreshAccessibilityValue();
}

void EQCurveComponent::timerCallback() {
    const auto bands = eqModule.getBandSnapshots();
    const float outputGainDb = eqModule.getOutputGainDb();

    // Asked on every tick, changed or not: a restore that left this EQ alone still uses up its turn, so it cannot
    // make the next ordinary edit glide.
    const bool restored =
        undoManager_ != nullptr && restoreWatch_.take(undoManager_->isRestoring(), undoManager_->getRestoreSerial());
    if (bandsDiffer(bands, lastBands) || outputGainDb != lastOutputGainDb)
        syncBands(bands, outputGainDb, restored);

    spectrumTick();
}
