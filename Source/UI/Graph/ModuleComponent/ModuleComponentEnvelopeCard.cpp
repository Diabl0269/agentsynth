// ModuleComponentEnvelopeCard.cpp -- the ADSR card's envelope graph: the curve editor the card body places
// as its Envelope view (CardBodyViews.cpp), the "Show Envelope Graph" toggle that opens and closes it, the
// two-way sync between attack/hold/decay/sustain/release/*Curve (or the *Div divisions while tempoSync is
// on) and the curve editor's model, undo-gesture wiring, and the playhead poll. The Time/Tempo switch and
// the stage controls (each division swapped in for its time) are the card body's own, laid out from the
// layout; this unit never touches them. ModuleComponent is declared in ModuleComponent.h; the rest of its
// implementation lives in the sibling ModuleComponent*.cpp units next to this one.
#include "AudioEngine/AudioEngine.h"
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/ADSRModule.h"
#include "Modules/Envelope/EnvelopeTempoSync.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/CardWidgets/CardTogglePill.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <cmath>
#include <limits>

using namespace detail;
using synth::ui::CurveMode;
using synth::ui::CurveModel;
using synth::ui::CurveNode;
using synth::ui::CurvePlayhead;

namespace {

// Nearest note-division index to `seconds` at `bpm`, compared in log-time so e.g. a duration
// halfway (on a musical, not linear, scale) between 1/4 and 1/8 rounds to whichever it is
// actually closer to perceptually. `seconds` is floored well above 0 so a degenerate (0 ms)
// duration never takes log() of zero.
int nearestEnvelopeDivisionIndex(double seconds, double bpm) {
    const double logSeconds = std::log(juce::jmax(1.0e-6, seconds));
    int best = 0;
    double bestDist = std::numeric_limits<double>::infinity();
    for (int i = 0; i < synth::envelopeNoteDivisions().size(); ++i) {
        const double candidate = (double)synth::envelopeNoteDivisionSeconds(i, bpm);
        const double dist = std::abs(logSeconds - std::log(juce::jmax(1.0e-6, candidate)));
        if (dist < bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

// BPM-mode tick-label formatter: the model's x axis stays in seconds either way (see
// buildEnvelopeCurveModel), so this only changes how a time is WORDED — in beats at `bpm`, not
// seconds. Kept simple per the design brief: whole beats read "N beat(s)"; anything else falls
// back to a plain "x.xx beats" rather than trying to spell out a sub-beat division name from a
// single tick time, which the grid's own 1/2/5x10^n spacing does not line up with a division
// boundary in general.
juce::String envelopeBeatLabel(double seconds, double bpm) {
    const double beats = seconds * bpm / 60.0;
    const double rounded = std::round(beats);
    if (std::abs(beats - rounded) < 0.01) {
        const int n = (int)rounded;
        return juce::String(n) + (n == 1 ? " beat" : " beats");
    }
    return juce::String(beats, 2) + " beats";
}

// The five node positions (time in seconds, level in 0..1) implied by an ADSRModule's current
// attack/hold/decay/sustain/release/*Curve parameter values. Node x is CUMULATIVE (origin=0,
// attack peak=attack, hold end=attack+hold, ...) per CurveModel::setNodeX's ripple contract: an
// x-edit on one node preserves every OTHER node's own segment duration, which is exactly why
// writeEnvelopeParamsFromCurve below can safely recompute all four durations from
// segmentDuration() after any single-node drag.
CurveModel buildEnvelopeCurveModel(juce::AudioProcessor& processor) {
    auto floatParam = [&processor](const char* paramID) -> float {
        auto* p = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(&processor, paramID));
        return p != nullptr ? p->get() : 0.0f;
    };

    auto* tempoSyncParam = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&processor, "tempoSync"));
    const bool bpmMode = tempoSyncParam != nullptr && tempoSyncParam->get();

    double attack, hold, decay, release;
    // Each movable node's maxSegment (below) mirrors the ceiling the current mode can actually
    // reach: ADSRModule's own attack/hold/decay/release NormalisableRange (5.0, see ADSRModule.h)
    // in MS mode, or the coarsest division ("1/1") at the module's last-seen tempo in BPM mode --
    // either way, a graph drag can never request a duration the mode itself could not represent.
    double maxStageSeconds = 5.0;
    if (bpmMode) {
        double bpm = 120.0;
        if (auto* adsr = dynamic_cast<ADSRModule*>(&processor))
            bpm = adsr->getLastSeenBpm();
        auto divSeconds = [&](const char* paramID) -> double {
            auto* p = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&processor, paramID));
            return p != nullptr ? (double)synth::envelopeNoteDivisionSeconds(p->getIndex(), bpm) : 0.0;
        };
        attack = divSeconds("attackDiv");
        hold = divSeconds("holdDiv");
        decay = divSeconds("decayDiv");
        release = divSeconds("releaseDiv");
        maxStageSeconds = (double)synth::envelopeNoteDivisionSeconds(synth::envelopeNoteDivisions().size() - 1,
                                                                     bpm); // "1/1", the coarsest
    } else {
        attack = (double)floatParam("attack");
        hold = (double)floatParam("hold");
        decay = (double)floatParam("decay");
        release = (double)floatParam("release");
    }
    const float sustain = floatParam("sustain");
    const double kMaxStageSeconds = maxStageSeconds;

    std::vector<CurveNode> nodes(5);
    nodes[0] = {0.0, 0.0f, /*xMovable*/ false, /*yMovable*/ false, 0.0, 0.0, 0.0f, 0.0f};
    nodes[1] = {attack, 1.0f, true, false, 0.0, kMaxStageSeconds, 1.0f, 1.0f};
    nodes[2] = {attack + hold, 1.0f, true, false, 0.0, kMaxStageSeconds, 1.0f, 1.0f};
    nodes[3] = {attack + hold + decay, sustain, true, true, 0.0, kMaxStageSeconds, 0.0f, 1.0f};
    nodes[4] = {attack + hold + decay + release, 0.0f, true, false, 0.0, kMaxStageSeconds, 0.0f, 0.0f};

    CurveModel model(CurveMode::Fixed);
    model.setNodes(std::move(nodes));
    model.setBend(0, floatParam("attackCurve"));
    model.setBend(2, floatParam("decayCurve"));
    model.setBend(3, floatParam("releaseCurve"));
    model.setBendable(1, false); // hold: flat (level 1 -> 1), no curve param behind it
    return model;
}

// EnvelopeStage -> the curve's (segment, progress-in-segment). Sustain parks exactly on node 3
// (segment 2's end), which IS the sustain node's own position, so it needs no special geometry --
// just a fixed progress of 1.0 rather than the generator's own free-running sustain progress.
std::optional<CurvePlayhead> envelopePlayheadFor(synth::EnvelopeStage stage, float progress) {
    using synth::EnvelopeStage;
    switch (stage) {
    case EnvelopeStage::Attack:
        return CurvePlayhead{0, progress};
    case EnvelopeStage::Hold:
        return CurvePlayhead{1, progress};
    case EnvelopeStage::Decay:
        return CurvePlayhead{2, progress};
    case EnvelopeStage::Sustain:
        return CurvePlayhead{2, 1.0f};
    case EnvelopeStage::Release:
        return CurvePlayhead{3, progress};
    case EnvelopeStage::Idle:
    default:
        return std::nullopt;
    }
}

} // namespace

void ModuleComponent::createEnvelopeCardControls() {
    // The curve editor is the card body's Envelope view, when the layout places one; a layout without it
    // draws no graph, so there is nothing to wire and no toggle.
    if (cardBody_ == nullptr)
        return;
    envelopeCurveEditor =
        dynamic_cast<synth::ui::CurveEditorComponent*>(cardBody_->findView(synth::CardView::Envelope));
    if (envelopeCurveEditor == nullptr)
        return;

    wireEnvelopeGestureCallbacks();
    // `this` outlives envelopeCurveEditor (owned by cardBody_, a member unique_ptr destroyed as part of this
    // component's own teardown before the outer object finishes destructing).
    envelopeCurveEditor->onNodeChanged = [this](int) { writeEnvelopeParamsFromCurve(); };
    envelopeCurveEditor->onBendChanged = [this](int) { writeEnvelopeParamsFromCurve(); };

    // Same pattern as the scope/frequency-response toggles: opens and closes the view, NOT persisted --
    // every construction starts with the layout's own `open` (docs/modules/modules.md#adsr-envelope-module).
    // On a card with a footer it is a pill in that row, like Show Scope.
    envelopeGraphToggle = std::make_unique<juce::ToggleButton>(synth::cardbody::kShowEnvelopeText);
    envelopeGraphToggle->setToggleState(cardBody_->isViewOpen(synth::CardView::Envelope), juce::dontSendNotification);
    envelopeGraphToggle->onClick = [this] {
        // The view fades and the card follows it; the body makes the neighbours room (CardBlockFade).
        cardBody_->setViewOpen(synth::CardView::Envelope, envelopeGraphToggle->getToggleState());
    };
    addAndMakeVisible(envelopeGraphToggle.get());
    if (cardBody_->hasFooter())
        synth::ui::setTogglePillStyle(*envelopeGraphToggle, true);

    // Builds the curve's initial model (MS- or tempo-shaped) and time-label formatter, since the editor has
    // no model yet at this point.
    syncEnvelopeCurveFromParams();

    // createControls()'s own tail already ran updateLayout() once, before these children existed.
    updateLayout();
}

void ModuleComponent::wireEnvelopeGestureCallbacks() {
    if (envelopeCurveEditor == nullptr)
        return;

    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    envelopeCurveEditor->onGestureStart = [safeThis] {
        if (safeThis == nullptr)
            return;
        safeThis->envelopeCurveGestureActive = true;
        if (safeThis->undoManager != nullptr && safeThis->module != nullptr)
            safeThis->undoManager->captureBeforeState(safeThis->owner.getAudioEngine().getGraph());
    };
    // One undo entry per whole drag (mirrors wireEqGestureCallbacks), not per intermediate delta:
    // writeEnvelopeParamsFromCurve's setValueNotifyingHost calls never bracket themselves with
    // beginChangeGesture/endChangeGesture, so parameterGestureChanged never fires for them.
    envelopeCurveEditor->onGestureEnd = [safeThis] {
        if (safeThis == nullptr)
            return;
        if (safeThis->undoManager != nullptr && safeThis->module != nullptr)
            safeThis->undoManager->pushSnapshotFromCapture(safeThis->owner.getAudioEngine().getGraph());
        safeThis->envelopeCurveGestureActive = false;
        // Snap the graph to the quantised/clamped param values now that the gesture (and this
        // component's own suppression of the reverse sync) has ended.
        safeThis->syncEnvelopeCurveFromParams();
    };
}

void ModuleComponent::writeEnvelopeParamsFromCurve() {
    if (envelopeCurveEditor == nullptr || module == nullptr)
        return;

    const auto& model = envelopeCurveEditor->getModel();

    auto writeParam = [this](const char* paramID, float newValue) {
        auto* p = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(module, paramID));
        if (p == nullptr)
            return;
        // Epsilon-gated: a drag that only moved one node/handle must not re-emit every other
        // unchanged value as a fresh host-automation write on every mouse-move.
        if (std::abs(p->get() - newValue) <= 1.0e-5f)
            return;
        p->setValueNotifyingHost(p->convertTo0to1(newValue));
    };

    auto* tempoSyncParam = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(module, "tempoSync"));
    const bool bpmMode = tempoSyncParam != nullptr && tempoSyncParam->get();

    if (bpmMode) {
        // An x-drag in BPM mode snaps to the nearest division rather than writing a raw
        // ms value the DSP would ignore anyway (resolveStageTimes only reads *Div while synced).
        double bpm = 120.0;
        if (auto* adsr = dynamic_cast<ADSRModule*>(module))
            bpm = adsr->getLastSeenBpm();
        auto writeDivParam = [this, bpm](const char* paramID, double seconds) {
            auto* p = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(module, paramID));
            if (p == nullptr)
                return;
            const int newIndex = nearestEnvelopeDivisionIndex(seconds, bpm);
            if (p->getIndex() == newIndex)
                return;
            p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1((float)newIndex));
        };
        writeDivParam("attackDiv", model.segmentDuration(0));
        writeDivParam("holdDiv", model.segmentDuration(1));
        writeDivParam("decayDiv", model.segmentDuration(2));
        writeDivParam("releaseDiv", model.segmentDuration(3));
    } else {
        writeParam("attack", (float)model.segmentDuration(0));
        writeParam("hold", (float)model.segmentDuration(1));
        writeParam("decay", (float)model.segmentDuration(2));
        writeParam("release", (float)model.segmentDuration(3));
    }
    writeParam("sustain", model.getNode(3).y);
    writeParam("attackCurve", model.getBend(0));
    writeParam("decayCurve", model.getBend(2));
    writeParam("releaseCurve", model.getBend(3));
}

void ModuleComponent::syncEnvelopeCurveFromParams() {
    // Message-thread only -- callers (parameterValueChanged, which can fire off the audio
    // thread) are responsible for marshalling, exactly like every other reverse-sync path in
    // ModuleComponentInteraction.cpp (applyPolyStateChange, applyDualIOLayoutChange, ...).
    if (envelopeCurveEditor == nullptr || module == nullptr || envelopeCurveGestureActive)
        return;
    envelopeCurveEditor->setModel(buildEnvelopeCurveModel(*module));

    auto* tempoSyncParam = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(module, "tempoSync"));
    if (tempoSyncParam != nullptr && tempoSyncParam->get()) {
        double bpm = 120.0;
        if (auto* adsr = dynamic_cast<ADSRModule*>(module))
            bpm = adsr->getLastSeenBpm();
        envelopeCurveEditor->setTimeLabelFormatter([bpm](double seconds) { return envelopeBeatLabel(seconds, bpm); });
    } else {
        envelopeCurveEditor->setTimeLabelFormatter(nullptr); // back to the default "0"/"250ms"/"1s" labels
    }
}

void ModuleComponent::updateEnvelopePlayhead() {
    if (envelopeCurveEditor == nullptr || !envelopeCurveEditor->isVisible() || module == nullptr)
        return;
    auto* adsr = dynamic_cast<ADSRModule*>(module);
    if (adsr == nullptr)
        return;
    envelopeCurveEditor->setPlayhead(envelopePlayheadFor(adsr->getPlayheadStage(), adsr->getPlayheadProgress()));
}
