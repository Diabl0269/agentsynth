// StepGridChordActions.cpp -- the Poly Sequencer's chord label as a control: where it is, the picker that opens from it
// (click on the label, or Enter on a focused step) and picking a chord as one undo step. The picker's contents are
// StepGridChords.cpp; the 120 ms cross-fade of a changed label is started here and drawn by StepGridPaint.cpp.
#include "StepGridComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/PopupHost.h"
#include "UI/Layout/ReducedMotion.h"

namespace synth::ui {

using namespace stepgrid;

namespace test_hooks {
std::function<void(std::unique_ptr<StepGridChordPicker>)>& chordPickerHookForTest() {
    static std::function<void(std::unique_ptr<StepGridChordPicker>)> hook;
    return hook;
}
} // namespace test_hooks

namespace {
constexpr double kLabelFadeMs = 120.0;
constexpr double kReducedFadeMs = 80.0;
constexpr float kAuditionVelocity = 0.8f;
} // namespace

juce::Rectangle<int> StepGridComponent::chordLabelBounds(int step) const {
    if (!module_.isPoly() || columnWidth() < 22.0 || step < 0 || step >= kMaxSequencerSteps)
        return {};
    const auto lane = currentLayout().lanes[(size_t)SequencerLane::Pitch];
    if (!laneShown(SequencerLane::Pitch))
        return {};
    const auto bar = pitchBarRect(step, (float)playedPitchOf(step), lane);
    return chordLabelRect(bar, lane, step).toNearestInt();
}

bool StepGridComponent::chordLabelAt(juce::Point<int> p, int step) const { return chordLabelBounds(step).contains(p); }

juce::String StepGridComponent::getTooltip() {
    // The tip follows the label the last mouse move was over, not the machine's pointer, so an off-screen grid (a
    // render, a test) never reads wherever the mouse happens to be.
    if (module_.isPoly() && hoverChordStep_ >= 0)
        return "Click to choose a chord (Enter on the keyboard)";
    return juce::SettableTooltipClient::getTooltip();
}

// Tracks the chord label under the pointer: its outline eases in (FadeAmount), the cursor becomes a pointing hand.
void StepGridComponent::updateChordHover(juce::Point<int> p) {
    int step = -1;
    if (module_.isPoly()) {
        const int under = stepAtX(p.x);
        if (under >= 0 && chordLabelAt(p, under))
            step = under;
    }
    if (step == hoverChordStep_)
        return;
    hoverChordStep_ = step;
    if (step >= 0) {
        hoverDrawnStep_ = step;
        chordHover_.setShown(true);
    } else {
        chordHover_.setShown(false);
    }
    chordHover_.repaintArea = [this] { return chordLabelBounds(hoverDrawnStep_).expanded(3); };
    updateMouseCursor();
}

void StepGridComponent::mouseMove(const juce::MouseEvent& e) { updateChordHover(e.getPosition()); }

void StepGridComponent::mouseExit(const juce::MouseEvent&) { updateChordHover({-1, -1}); }

juce::MouseCursor StepGridComponent::getMouseCursor() {
    return hoverChordStep_ >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor;
}

void StepGridComponent::openChordPicker(int step) {
    if (!module_.isPoly() || step < 0 || step >= kMaxSequencerSteps)
        return;
    const auto& entry = module_.getPattern().steps[(size_t)step];
    auto picker = std::make_unique<StepGridChordPicker>(chordPickerContent(playedPitchOf(step), scale_), entry.chord,
                                                        monoDestination_);
    juce::Component::SafePointer<StepGridComponent> safe(this);
    picker->onPick = [safe, step](SequencerChord chord) {
        if (safe != nullptr)
            safe->pickChord(step, chord);
    };
    picker->onAudition = [safe, step](std::optional<SequencerChord> chord) {
        if (safe != nullptr)
            safe->auditionChord(step, chord);
    };
    if (auto& hook = test_hooks::chordPickerHookForTest()) {
        hook(std::move(picker));
        return;
    }
    auto area = chordLabelBounds(step);
    if (area.isEmpty())
        area = cellBounds(SequencerLane::Pitch, step);
    synth::ui::launchPopover(std::move(picker), localAreaToGlobal(area), this);
}

// Plays the chord the step would play with `chord` through the module's output, or ends the audition (nullopt).
void StepGridComponent::auditionChord(int step, std::optional<SequencerChord> chord) {
    if (!chord.has_value()) {
        module_.stopPreview();
        return;
    }
    const auto notes = sequencerChordNotes(*chord, playedPitchOf(step), scale_, module_.getPattern().seed, step);
    module_.previewNotes(notes.notes.data(), notes.count, kAuditionVelocity);
}

void StepGridComponent::pickChord(int step, SequencerChord chord) {
    if (step < 0 || step >= kMaxSequencerSteps || module_.getPattern().steps[(size_t)step].chord == chord)
        return;
    const auto beforePattern = module_.getPattern();
    const auto beforeState = module_.getExtraState();
    editStep(step, [chord](SequencerStep& s) { s.chord = chord; });
    startGlide(beforePattern);
    commit(beforeState);
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

// A label whose chord changed fades from the old text to the new in 120 ms (80 under Reduce Motion, at once with
// Animations Off or when the grid is not on screen). The old chord is read from glideFrom_.
void StepGridComponent::startLabelFade() {
    labelFadeAnim_.stop(vblank_);
    labelFading_ = false;
    if (!module_.isPoly() || !FadeVisibility::canAnimateIn(this))
        return;
    labelFading_ = true;
    labelFadeT_ = 0.0f;
    labelFadeAnim_.start(
        vblank_, motionMs(kLabelFadeMs, kReducedFadeMs), [](float t) { return t; },
        [this](float t) {
            labelFadeT_ = t;
            repaint();
        },
        [this] {
            labelFading_ = false;
            repaint();
        });
}

} // namespace synth::ui
