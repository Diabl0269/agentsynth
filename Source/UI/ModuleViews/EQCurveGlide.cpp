// EQCurveGlide.cpp -- how EQCurveComponent follows the module: the 30 Hz poll, and the glide an undo or redo gives the
// dots and the curve (EqBandGlide.h), including the cross-fade a changed band type gets. EQCurveComponent is
// declared in EQCurveComponent.h.

#include "EQCurveComponent.h"

#include "AppUndoManager.h"
#include "EQCurveHoldState.h"
#include "EqBandGlide.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/KeyGlide/RestoreWatch.h"

#include <algorithm>

struct EQCurveComponent::GlideState {
    explicit GlideState(juce::Component& owner)
        : glide(owner)
        , soloEase(owner)
        , readoutEase(owner)
        , haloEase(owner) {}
    synth::ui::EqBandGlide glide;
    synth::ui::RestoreWatch restoreWatch;
    synth::ui::KeyedPointTween soloEase; // per band: (soloed, dimmed by another's solo), keyed by the band
    std::array<juce::Point<float>, ParametricEQModule::kMaxBands> soloRest{};
    synth::ui::KeyedPointTween readoutEase; // per band: (readout shown, 0), keyed by the band
    std::array<juce::Point<float>, ParametricEQModule::kMaxBands> readoutRest{};
    synth::ui::KeyedPointTween haloEase; // per band: (halo and accent ring lit, 0), keyed by the band
    std::array<juce::Point<float>, ParametricEQModule::kMaxBands> haloRest{};
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

synth::ui::KeyedTween<juce::Point<float>>& EQCurveComponent::getReadoutEaseForTest() noexcept {
    return glide_->readoutEase;
}

synth::ui::KeyedTween<juce::Point<float>>& EQCurveComponent::getHaloEaseForTest() noexcept { return glide_->haloEase; }

// The readout of the hovered band (its chip too), of the band being dragged, and of a band a single-band change
// touched in the last second. Selection alone shows nothing, and a group has no single value to show.
void EQCurveComponent::holdReadout(int band) {
    if (band < 0 || band >= ParametricEQModule::kMaxBands)
        return;
    constexpr double kReadoutHoldMs = 1000.0;
    readoutHoldUntilMs_[(size_t)band] =
        (readoutClock_ ? readoutClock_() : juce::Time::getMillisecondCounterHiRes()) + kReadoutHoldMs;
    syncReadouts();
}

void EQCurveComponent::setChipHoveredBand(int band) {
    if (band == chipHoveredBand_)
        return;
    chipHoveredBand_ = band;
    syncReadouts();
}

std::vector<int> EQCurveComponent::getReadoutBands() const {
    std::vector<int> bands;
    const auto add = [&](int band) {
        if (band >= 0 && lastBands[(size_t)band].enabled && std::find(bands.begin(), bands.end(), band) == bands.end())
            bands.push_back(band);
    };
    add(hoveredBand);
    add(chipHoveredBand_);
    add(dragBand);
    const double now = readoutClock_ ? readoutClock_() : juce::Time::getMillisecondCounterHiRes();
    for (int b = 0; b < ParametricEQModule::kMaxBands; ++b)
        if (readoutHoldUntilMs_[(size_t)b] > now)
            add(b);
    return bands;
}

// The readout fades in and out like a dot's solo look: the drawn value trails the wanted one, Reduce Motion fades it,
// Animations Off lands it.
float EQCurveComponent::getReadoutLook(int band) const {
    const auto rest = glide_->readoutRest[(size_t)band];
    const float alpha = glide_->readoutEase.alphaFor(band);
    return juce::jlimit(0.0f, 1.0f, alpha < 1.0f ? rest.x * alpha : glide_->readoutEase.drawnAt(band, rest).x);
}

// A dot's halo and accent ring (hovered or selected) light and clear the same way: the drawn value trails the wanted
// one.
float EQCurveComponent::getHaloLook(int band) const {
    const auto rest = glide_->haloRest[(size_t)band];
    const float alpha = glide_->haloEase.alphaFor(band);
    return juce::jlimit(0.0f, 1.0f, alpha < 1.0f ? rest.x * alpha : glide_->haloEase.drawnAt(band, rest).x);
}

void EQCurveComponent::syncReadouts() {
    const auto wanted = getReadoutBands();
    bool changed = false;
    for (int b = 0; b < ParametricEQModule::kMaxBands; ++b) {
        const juce::Point<float> lit(b == hoveredBand || hold_->selection.contains(b) ? 1.0f : 0.0f, 0.0f);
        const auto before = glide_->haloRest[(size_t)b];
        if (lit == before)
            continue;
        glide_->haloEase.retarget((synth::ui::KeyedPointTween::Key)b,
                                  glide_->haloEase.drawnAt((synth::ui::KeyedPointTween::Key)b, before), lit, true);
        glide_->haloRest[(size_t)b] = lit;
        changed = true;
    }
    for (int b = 0; b < ParametricEQModule::kMaxBands; ++b) {
        const juce::Point<float> rest(std::find(wanted.begin(), wanted.end(), b) != wanted.end() ? 1.0f : 0.0f, 0.0f);
        const auto before = glide_->readoutRest[(size_t)b];
        if (rest == before)
            continue;
        glide_->readoutEase.retarget((synth::ui::KeyedPointTween::Key)b,
                                     glide_->readoutEase.drawnAt((synth::ui::KeyedPointTween::Key)b, before), rest,
                                     true);
        glide_->readoutRest[(size_t)b] = rest;
        changed = true;
    }
    if (changed)
        repaint();
}

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
    if (restored) { // undo brings a deleted band back selected (keys have it); redo hands the selection on as Delete
                    // did
        int turnedOn = -1;
        bool selectedTurnedOff = false;
        float offHz = 0.0f;
        for (size_t b = 0; b < bands.size(); ++b) {
            if (bands[b].enabled && !lastBands[b].enabled)
                turnedOn = (int)b;
            if (!bands[b].enabled && lastBands[b].enabled && hold_->selection.contains((int)b)) {
                selectedTurnedOff = true;
                offHz = lastBands[b].freqHz;
            }
        }
        lastBands = bands;
        if (turnedOn >= 0)
            setSelection({turnedOn}, turnedOn, turnedOn);
        else if (selectedTurnedOff)
            selectNextAfterRemoval(offHz);
    }
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
    syncReadouts(); // a band switched off, or on, elsewhere
    spectrumTick();
}
