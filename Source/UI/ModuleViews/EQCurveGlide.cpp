// EQCurveGlide.cpp -- how EQCurveComponent follows the module: the 30 Hz poll, and the glide an undo or redo gives the
// dots and the curve (EqBandGlide.h), including the cross-fade a changed band type gets. EQCurveComponent is
// declared in EQCurveComponent.h.

#include "EQCurveComponent.h"

#include "AppUndoManager.h"
#include "EQCurveHoldState.h"
#include "EqBandGlide.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/KeyGlide/RestoreWatch.h"

struct EQCurveComponent::GlideState {
    explicit GlideState(juce::Component& owner)
        : glide(owner)
        , soloEase(owner) {}
    synth::ui::EqBandGlide glide;
    synth::ui::RestoreWatch restoreWatch;
    synth::ui::KeyedPointTween soloEase; // per band: (soloed, dimmed by another's solo), keyed by the band
    std::array<juce::Point<float>, ParametricEQModule::kMaxBands> soloRest{};
    int soloApplied = -1;
};

EQCurveComponent::EQCurveComponent(ParametricEQModule& eq)
    : eqModule(eq)
    , hold_(std::make_unique<HoldState>(*this))
    , glide_(std::make_unique<GlideState>(*this)) {
    hold_->hold.onFrame = [this] { applyKeyHold(); };
    hold_->hold.onEnd = [this] { endKeyHold(); };
    hold_->hold.speedFactor = [this](synth::ui::KeyAxis) {
        const auto& host = hold_->hold.host;
        return host.isShiftDown && host.isShiftDown() ? 0.25 : 1.0;
    };
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
    startTimerHz(30);
}

// A solo this curve started ends with it, so audio never stays soloed with nothing on screen to end it.
EQCurveComponent::~EQCurveComponent() {
    stopTimer();
    if (ownsSolo_ && eqModule.getSoloBand() == ownedSoloBand_)
        eqModule.clearSolo();
}

synth::ui::KeyHold& EQCurveComponent::getKeyHoldForTest() noexcept { return hold_->hold; }

synth::ui::KeyedTween<juce::Point<float>>& EQCurveComponent::getBandGlideForTest() noexcept {
    return glide_->glide.tweenForTest();
}

synth::ui::KeyedTween<juce::Point<float>>& EQCurveComponent::getTypeMorphForTest() noexcept {
    return glide_->glide.morphTweenForTest();
}

synth::ui::KeyedTween<juce::Point<float>>& EQCurveComponent::getSoloEaseForTest() noexcept { return glide_->soloEase; }

// A dot's solo look eases between its states like any other drawn change; Reduce Motion fades it in (and lands it out)
// instead of travelling, Animations Off lands it at once.
juce::Point<float> EQCurveComponent::getSoloLook(int band) const {
    const auto rest = glide_->soloRest[(size_t)band];
    const float alpha = glide_->soloEase.alphaFor(band);
    return alpha < 1.0f ? rest * alpha : glide_->soloEase.drawnAt(band, rest);
}

// The module's solo is the truth (another curve on the same EQ, or the audio engine's owner, may change it): a solo
// this curve started drops its claim when someone else moved it, and is ended when its band went off (an undo can do
// that).
void EQCurveComponent::syncSolo() {
    int solo = eqModule.getSoloBand();
    if (ownsSolo_ && solo != ownedSoloBand_)
        ownsSolo_ = false;
    if (ownsSolo_ && !eqModule.isBandEnabled(solo)) {
        eqModule.clearSolo();
        ownsSolo_ = false;
        solo = -1;
    }
    if (solo == glide_->soloApplied)
        return;
    glide_->soloApplied = solo;
    for (int b = 0; b < ParametricEQModule::kMaxBands; ++b) {
        const auto key = (synth::ui::KeyedPointTween::Key)b;
        const juce::Point<float> rest(b == solo ? 1.0f : 0.0f, solo >= 0 && b != solo ? 1.0f : 0.0f);
        const auto before = glide_->soloRest[(size_t)b];
        if (rest == before)
            continue;
        glide_->soloEase.retarget(key, glide_->soloEase.drawnAt(key, before), rest, true);
        glide_->soloRest[(size_t)b] = rest;
    }
    repaint();
    listeners_.call([](Listener& l) { l.eqCurveChanged(); });
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
    glide_->glide.sync(lastBands, lastOutputGainDb, bands, outputGainDb, restored, glideMoves_);
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

    syncSolo();
    spectrumTick();
}
