// ModuleComponentEnvelopeCard.cpp -- the ADSR envelope card (FRO112): the graph disclosure
// toggle + curve editor + BPM|MS row, the five knobs' short captions, the two-way sync between
// attack/hold/decay/sustain/release/*Curve and the curve editor's model, undo-gesture wiring, and
// the playhead poll. ModuleComponent is declared in ModuleComponent.h; the rest of its
// implementation lives in the sibling ModuleComponent*.cpp units next to this one (FRO65 split of
// the former single ModuleComponent.cpp).
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/ADSRModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <cmath>

using namespace detail;
using synth::ui::CurveMode;
using synth::ui::CurveModel;
using synth::ui::CurveNode;
using synth::ui::CurvePlayhead;

namespace {

// Short knob captions for the envelope card's five knobs (attack/hold/decay/sustain/release),
// keyed by the parameter display name createControls() already uses as both the slider's
// componentID and its label text.
const char* envelopeKnobShortLabel(const juce::String& paramName) {
    if (paramName == "Attack")
        return "ATK";
    if (paramName == "Hold")
        return "HOLD";
    if (paramName == "Decay")
        return "DEC";
    if (paramName == "Sustain")
        return "SUS";
    if (paramName == "Release")
        return "REL";
    return nullptr;
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

    const double attack = (double)floatParam("attack");
    const double hold = (double)floatParam("hold");
    const double decay = (double)floatParam("decay");
    const float sustain = floatParam("sustain");
    const double release = (double)floatParam("release");

    // ADSRModule's own attack/hold/decay/release NormalisableRange ceiling (see ADSRModule.h) --
    // mirrored here as each movable node's maxSegment so a graph drag can never request a
    // duration the parameter itself could not represent.
    constexpr double kMaxStageSeconds = 5.0;

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

void ModuleComponent::applyEnvelopeKnobShortLabels() {
    for (int i = 0; i < sliders.size(); ++i) {
        if (const char* shortLabel = envelopeKnobShortLabel(sliders[i]->getComponentID()))
            sliderLabels[i]->setText(shortLabel, juce::dontSendNotification);
    }
}

void ModuleComponent::createEnvelopeCardControls() {
    envelopeCurveEditor = std::make_unique<synth::ui::CurveEditorComponent>();
    envelopeCurveEditor->setModel(buildEnvelopeCurveModel(*module));
    envelopeCurveEditor->setVisible(false); // collapsed by default -- see envelopeGraphToggle
    addChildComponent(envelopeCurveEditor.get());

    wireEnvelopeGestureCallbacks();
    // `this` outlives envelopeCurveEditor (a member unique_ptr, destroyed as part of this
    // component's own teardown before the outer object finishes destructing) -- the same
    // no-dangling-pointer reasoning createControls()'s addMouseListener(this, ...) comment gives
    // for the generic auto-UI sliders.
    envelopeCurveEditor->onNodeChanged = [this](int) { writeEnvelopeParamsFromCurve(); };
    envelopeCurveEditor->onBendChanged = [this](int) { writeEnvelopeParamsFromCurve(); };

    // Same pattern as the scope/frequency-response toggles: hidden by default, NOT persisted --
    // resets to collapsed on every construction (matches those two, not Macro Group's persisted
    // collapse; see docs/modules.md for the decision).
    envelopeGraphToggle = std::make_unique<juce::ToggleButton>("Show Envelope Graph");
    envelopeGraphToggle->setToggleState(false, juce::dontSendNotification);
    envelopeGraphToggle->onClick = [this] {
        envelopeCurveEditor->setVisible(envelopeGraphToggle->getToggleState());
        updateLayout();
    };
    addAndMakeVisible(envelopeGraphToggle.get());

    // BPM|MS segmented control. MS is fully functional (today's ms-based attack/hold/decay/
    // release). BPM is a visual placeholder: FRO113 (a parallel ticket) owns the tempoSync/
    // attackDiv/holdDiv/decayDiv/releaseDiv parameters and the DSP behind them; wiring this
    // button to `tempoSync` is the one seam left for that ticket once its parameters land.
    envelopeMsButton = std::make_unique<juce::TextButton>("MS");
    envelopeBpmButton = std::make_unique<juce::TextButton>("BPM");
    // setRadioGroupId gives the pair JUCE's own mutual-exclusion for free: Button::setToggleState
    // calls turnOffOtherButtonsInGroup() (a sibling search under the shared parent) SYNCHRONOUSLY
    // before dispatching the click, whichever path reaches it -- a real mouse click via
    // triggerClick(), or a direct setToggleState(true, sendNotificationSync) call (the pattern a
    // headless test uses, since triggerClick() only posts an async command message).
    constexpr int kEnvelopeSyncRadioGroup = 0x454e5631; // 'ENV1', arbitrary but unique to this pair
    envelopeMsButton->setClickingTogglesState(true);
    envelopeBpmButton->setClickingTogglesState(true);
    envelopeMsButton->setRadioGroupId(kEnvelopeSyncRadioGroup);
    envelopeBpmButton->setRadioGroupId(kEnvelopeSyncRadioGroup);
    envelopeMsButton->setToggleState(true, juce::dontSendNotification);
    envelopeMsButton->setConnectedEdges(juce::TextButton::ConnectedOnRight);
    envelopeBpmButton->setConnectedEdges(juce::TextButton::ConnectedOnLeft);
    envelopeBpmButton->setTooltip("Tempo sync (coming soon)");
    addAndMakeVisible(envelopeMsButton.get());
    addAndMakeVisible(envelopeBpmButton.get());

    // createControls()'s own tail already ran updateLayout() once, before these children
    // existed -- mirrors createWavetableTabs()'s identical need to re-lay the card after adding
    // controls of its own post-createControls().
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

    writeParam("attack", (float)model.segmentDuration(0));
    writeParam("hold", (float)model.segmentDuration(1));
    writeParam("decay", (float)model.segmentDuration(2));
    writeParam("release", (float)model.segmentDuration(3));
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
}

void ModuleComponent::updateEnvelopePlayhead() {
    if (envelopeCurveEditor == nullptr || !envelopeCurveEditor->isVisible() || module == nullptr)
        return;
    auto* adsr = dynamic_cast<ADSRModule*>(module);
    if (adsr == nullptr)
        return;
    envelopeCurveEditor->setPlayhead(envelopePlayheadFor(adsr->getPlayheadStage(), adsr->getPlayheadProgress()));
}

// Extracted out of the shared layoutDefaultContent (ModuleComponentLayout.cpp) to keep that
// function under its own line-count ratchet — a no-op for every non-ADSR module, where
// envelopeGraphToggle is null.
int ModuleComponent::layoutEnvelopeGraphSection(int y, int contentX, int contentW, bool apply) {
    if (envelopeGraphToggle == nullptr)
        return y;

    if (apply) {
        constexpr int kBpmMsWidth = 90;
        envelopeGraphToggle->setBounds(contentX, y, contentW - kBpmMsWidth - 8, kRowHeight);
        if (envelopeMsButton && envelopeBpmButton) {
            const int segW = kBpmMsWidth / 2;
            envelopeMsButton->setBounds(contentX + contentW - kBpmMsWidth, y, segW, kRowHeight);
            envelopeBpmButton->setBounds(contentX + contentW - kBpmMsWidth + segW, y, kBpmMsWidth - segW, kRowHeight);
        }
    }
    y += kRowHeight + 2;

    if (envelopeCurveEditor && envelopeCurveEditor->isVisible()) {
        constexpr int kEnvelopeGraphHeight = 150;
        if (apply)
            envelopeCurveEditor->setBounds(contentX, y, contentW, kEnvelopeGraphHeight);
        y += kEnvelopeGraphHeight + 8;
    }

    return y;
}
