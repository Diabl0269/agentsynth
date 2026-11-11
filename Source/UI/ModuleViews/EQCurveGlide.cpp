// EQCurveGlide.cpp -- how EQCurveComponent follows the module: the 30 Hz poll, and the glide an undo or redo gives the
// dots and the curve (EqBandGlide.h), including the cross-fade a changed band type gets. EQCurveComponent is
// declared in EQCurveComponent.h.

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
    morphFrom_.resize(numPoints, 0.0f);
    lastBands = eqModule.getBandSnapshots();
    for (size_t b = 0; b < lastBands.size(); ++b)
        morphFromType_[b] = lastBands[b].type;
    lastOutputGainDb = eqModule.getOutputGainDb();
    glide_->glide.onChange = [this] {
        recomputeMagnitudes();
        repaint();
    };
    recomputeMagnitudes();
    setWantsKeyboardFocus(true);
    setTitle("EQ curve");
    setDescription("Parametric EQ response with up to 24 bands");
    setTooltip("EQ curve: Left/Right pick a band, Up/Down change its gain, Alt+Left/Right its frequency, "
               "Page Up/Down its Q, Return switches it on or off. Mouse: double-click to add or remove a "
               "point, drag to move, scroll for Q, right-click a point for its type");
    startTimerHz(30);
}

EQCurveComponent::~EQCurveComponent() { stopTimer(); }

synth::ui::KeyedTween<juce::Point<float>>& EQCurveComponent::getBandGlideForTest() noexcept {
    return glide_->glide.tweenForTest();
}

synth::ui::KeyedTween<juce::Point<float>>& EQCurveComponent::getTypeMorphForTest() noexcept {
    return glide_->glide.morphTweenForTest();
}

// The curve is summed from the DRAWN bands, which trail the real ones while an undo or redo glides. While a band's
// type morphs, the curve is the old one cross-faded into the new one (morphFrom_ is the curve as drawn when the type
// changed), and each dot's ring colour blends the same way.
void EQCurveComponent::recomputeMagnitudes() {
    const auto drawn = glide_->glide.drawn(lastBands, lastOutputGainDb);
    dotBands_ = drawn.dots;
    typeMorph_ = drawn.typeMorph;
    for (size_t b = 0; b < dotAlpha_.size(); ++b)
        dotAlpha_[b] = drawn.alpha[b];
    for (int i = 0; i < numPoints; ++i) {
        const float freq = synth::ui::FrequencyGrid::indexToFreq(i, numPoints);
        const float target =
            juce::jlimit(minDb, maxDb, ParametricEQModule::responseDb(drawn.curve, drawn.outputGainDb, freq));
        magnitudes[(size_t)i] = typeMorph_ > 0.0f ? target + (morphFrom_[(size_t)i] - target) * typeMorph_ : target;
    }
}

void EQCurveComponent::setUndoManager(AppUndoManager* manager) {
    undoManager_ = manager;
    glide_->restoreWatch = synth::ui::RestoreWatch(manager != nullptr ? manager->getRestoreSerial() : 0);
}

void EQCurveComponent::syncBands(const ParametricEQModule::BandSnapshots& bands, float outputGainDb, bool restored) {
    for (size_t b = 0; b < bands.size(); ++b) {
        const bool shapeChanged =
            bands[b].type != lastBands[b].type || bands[b].slopeDbPerOct != lastBands[b].slopeDbPerOct;
        if (shapeChanged) {
            if (morphFrom_.size() != magnitudes.size() || typeMorph_ <= 0.0f)
                morphFrom_ = magnitudes; // a morph already running keeps the curve it started from
            morphFromType_[b] = typeMorph_ > 0.0f ? morphFromType_[b] : dotBands_[b].type;
        } else if (typeMorph_ <= 0.0f) {
            morphFromType_[b] = bands[b].type;
        }
    }
    glide_->glide.sync(lastBands, lastOutputGainDb, bands, outputGainDb, restored);
    lastBands = bands;
    lastOutputGainDb = outputGainDb;
    recomputeMagnitudes();
    repaint();
    refreshAccessibilityValue();
    listeners_.call([](Listener& l) { l.eqCurveChanged(); });
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
