// EQCurveGlide.cpp -- how EQCurveComponent follows the module: the 30 Hz poll, and the glide an undo or redo gives the
// dots and the curve (EqBandGlide.h). EQCurveComponent is declared (with its mouse and paint code) in
// EQCurveComponent.h.

#include "EQCurveComponent.h"

#include "AppUndoManager.h"
#include "EqBandGlide.h"
#include "UI/Layout/KeyGlide/RestoreWatch.h"

struct EQCurveComponent::GlideState {
    explicit GlideState(juce::Component& owner)
        : glide(owner) {}
    synth::ui::EqBandGlide glide;
    synth::ui::RestoreWatch restoreWatch;
};

EQCurveComponent::EQCurveComponent(ParametricEQModule& eq)
    : eqModule(eq)
    , glide_(std::make_unique<GlideState>(*this)) {
    magnitudes.resize(numPoints, 0.0f);
    fftData.resize(fftSize * 2, 0.0f);
    spectrumMagnitudes.resize(numPoints, kSpecMinDb);
    lastBands = eqModule.getBandSnapshots();
    lastOutputGainDb = eqModule.getOutputGainDb();
    glide_->glide.onChange = [this] {
        recomputeMagnitudes();
        repaint();
    };
    recomputeMagnitudes();
    setWantsKeyboardFocus(true);
    setTitle("EQ curve");
    setDescription("Parametric EQ response with up to four bands");
    setTooltip("EQ curve: Left/Right pick a band, Up/Down change its gain, Alt+Left/Right its frequency, "
               "Page Up/Down its Q, Return switches it on or off. Mouse: double-click to add or remove a "
               "point, drag to move, scroll for Q");
    startTimerHz(30);
}

EQCurveComponent::~EQCurveComponent() { stopTimer(); }

synth::ui::KeyedTween<juce::Point<float>>& EQCurveComponent::getBandGlideForTest() noexcept {
    return glide_->glide.tweenForTest();
}

void EQCurveComponent::recomputeMagnitudes() {
    // The curve is summed from the DRAWN bands, which trail the real ones while an undo or redo glides.
    const auto drawn = glide_->glide.drawn(lastBands, lastOutputGainDb);
    dotBands_ = drawn.dots;
    for (size_t b = 0; b < dotAlpha_.size(); ++b)
        dotAlpha_[b] = drawn.alpha[b];
    for (int i = 0; i < numPoints; ++i) {
        const float freq = synth::ui::FrequencyGrid::indexToFreq(i, numPoints);
        magnitudes[(size_t)i] =
            juce::jlimit(minDb, maxDb, ParametricEQModule::responseDb(drawn.curve, drawn.outputGainDb, freq));
    }
}

void EQCurveComponent::setUndoManager(AppUndoManager* manager) {
    undoManager_ = manager;
    glide_->restoreWatch = synth::ui::RestoreWatch(manager != nullptr ? manager->getRestoreSerial() : 0);
}

void EQCurveComponent::syncBands(
    const std::array<ParametricEQModule::BandSnapshot, ParametricEQModule::kNumBands>& bands, float outputGainDb,
    bool restored) {
    glide_->glide.sync(lastBands, lastOutputGainDb, bands, outputGainDb, restored);
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
    const bool restored = undoManager_ != nullptr &&
                          glide_->restoreWatch.take(undoManager_->isRestoring(), undoManager_->getRestoreSerial());
    if (bandsDiffer(bands, lastBands) || outputGainDb != lastOutputGainDb)
        syncBands(bands, outputGainDb, restored);

    spectrumTick();
}
