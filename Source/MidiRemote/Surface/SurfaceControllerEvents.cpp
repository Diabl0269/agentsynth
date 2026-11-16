// SurfaceController's per-event mapping: a fader moves the strip's gain as one host gesture, touch
// brackets that gesture, and the strip/bank buttons reach the same paths the mixer's own controls use
// (docs/control/midi-remote-mcu-hui.md#3-surfacecontroller-message-thread-half).
#include "SurfaceController.h"

#include "AppUndoManager.h"
#include "Modules/ModuleBase.h"
#include "UI/Mixer/MixerFaderTaper.h"

namespace synth::midi {

void SurfaceController::handleEvent(Unit& unit, const SurfaceEvent& event) {
    switch (event.kind) {
    case SurfaceEventKind::fader:
        handleFader(unit, event.index, event.value);
        break;
    case SurfaceEventKind::touch:
        handleTouch(unit, event.index, event.value != 0);
        break;
    case SurfaceEventKind::button:
        if (event.value != 0)
            handleButton(unit, event.index);
        break;
    case SurfaceEventKind::sysex:
        // Device ready: the unit (re)started and shows nothing we sent, so redraw it whole. MCU mode
        // needs no reply bytes (the challenge exchange is Logic Control only).
        if (McuSurface::isDeviceReady(event) && unit.renderer != nullptr)
            unit.renderer->markDirty();
        break;
    case SurfaceEventKind::encoderDelta:
    case SurfaceEventKind::jog:
        break; // decoded, not mapped in phase 1
    }
}

// A move on a strip with no gesture open begins one (a surface without touch notes, or a move just
// after a bank change); the value goes through the mixer fader's taper so 14-bit fader travel and the
// on-screen fader agree. Takeover is always Jump: the motor already put the fader at the value.
void SurfaceController::handleFader(Unit& unit, int strip, int value) {
    if (strip < 0 || strip >= mcu::kStripsPerUnit) // the master fader is not mapped in phase 1
        return;
    auto& gesture = unit.gestures[static_cast<std::size_t>(strip)];
    const auto node = gesture.active ? gesture.node : nodeForStrip(strip);
    auto* gain = stripParameter(node, "gain");
    if (gain == nullptr)
        return;
    if (!gesture.active) {
        gesture.node = node;
        gesture.active = true;
        gain->beginChangeGesture();
    }
    gesture.lastMoveMs = now();
    const float fraction = static_cast<float>(juce::jlimit(0, mcu::kFaderMax, value)) / mcu::kFaderMax;
    gain->setValueNotifyingHost(gain->convertTo0to1(synth::ui::faderFractionToDb(fraction)));
}

// Touch-down opens the gesture and release closes it: one undo step and one automation pass per touch,
// however many moves came between. The renderer holds the motor still while the hand is on it.
void SurfaceController::handleTouch(Unit& unit, int strip, bool touched) {
    if (strip < 0 || strip >= mcu::kStripsPerUnit)
        return;
    auto& gesture = unit.gestures[static_cast<std::size_t>(strip)];
    gesture.touched = touched;
    if (unit.renderer != nullptr)
        unit.renderer->setTouched(strip, touched);
    if (!touched) {
        endGesture(gesture);
        return;
    }
    if (gesture.active)
        return;
    if (auto* gain = stripParameter(nodeForStrip(strip), "gain")) {
        gesture.node = nodeForStrip(strip);
        gesture.active = true;
        gesture.lastMoveMs = now();
        gain->beginChangeGesture();
    }
}

// Mute is the column's own toggle (one undo bracket around setMuted, as MixerColumnComponent does);
// Solo goes through the node-command seam so the engine's solo gate and its undo step are the
// mixer's; Select hands the node to the app. REC does nothing in phase 1.
void SurfaceController::handleButton(Unit& unit, int note) {
    juce::ignoreUnused(unit);
    if (note >= mcu::kSoloFirstNote && note < mcu::kSoloFirstNote + mcu::kStripsPerUnit) {
        if (const auto node = nodeForStrip(note - mcu::kSoloFirstNote); stripParameter(node, "gain") != nullptr)
            invoker_.invokeNodeCommand(node, NodeCommandKind::toggleSolo);
    } else if (note >= mcu::kMuteFirstNote && note < mcu::kMuteFirstNote + mcu::kStripsPerUnit) {
        const auto node = nodeForStrip(note - mcu::kMuteFirstNote);
        auto* muted = stripParameter(node, "muted");
        auto* module =
            muted != nullptr ? dynamic_cast<ModuleBase*>(graph_.getNodeForId(node)->getProcessor()) : nullptr;
        if (module == nullptr)
            return;
        if (undo_ != nullptr)
            undo_->captureBeforeState(graph_);
        module->setMuted(muted->getValue() < 0.5f);
        if (undo_ != nullptr)
            undo_->pushSnapshotFromCapture(graph_);
    } else if (note >= mcu::kSelectFirstNote && note < mcu::kSelectFirstNote + mcu::kStripsPerUnit) {
        const auto node = nodeForStrip(note - mcu::kSelectFirstNote);
        if (node == NodeID{})
            return;
        selectedNode_ = node;
        if (onSelectNode)
            onSelectNode(node);
    } else if (note == mcu::kBankLeftNote || note == mcu::kBankRightNote) {
        moveBank(note == mcu::kBankLeftNote ? -mcu::kStripsPerUnit : mcu::kStripsPerUnit);
    } else if (note == mcu::kChannelLeftNote || note == mcu::kChannelRightNote) {
        moveBank(note == mcu::kChannelLeftNote ? -1 : 1);
    }
}

// Clamped so the last bank is full where possible. A real move ends every open gesture first (the
// strips are about to mean other channels) and redraws every unit whole.
void SurfaceController::moveBank(int delta) {
    const int maxOffset = juce::jmax(0, static_cast<int>(slots_.size()) - mcu::kStripsPerUnit);
    const int next = juce::jlimit(0, maxOffset, bankOffset_ + delta);
    if (next == bankOffset_)
        return;
    for (auto& unit : units_) {
        for (auto& gesture : unit->gestures)
            endGesture(gesture);
        if (unit->renderer != nullptr)
            unit->renderer->markDirty();
    }
    bankOffset_ = next;
}

void SurfaceController::endGesture(Gesture& gesture) {
    if (gesture.active)
        if (auto* gain = stripParameter(gesture.node, "gain"))
            gain->endChangeGesture();
    gesture.active = false;
    gesture.node = {};
}

// The no-touch fallback: a surface that never sends touch notes gets RemoteEngine's own idle end, so a
// slow sweep is still one undo step. A held touch never expires.
void SurfaceController::expireIdleGestures(Unit& unit, double nowMs) {
    for (auto& gesture : unit.gestures)
        if (gesture.active && !gesture.touched && nowMs - gesture.lastMoveMs > kGestureIdleMs)
            endGesture(gesture);
}

} // namespace synth::midi
