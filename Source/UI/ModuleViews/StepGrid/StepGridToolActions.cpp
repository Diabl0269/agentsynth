// StepGridToolActions.cpp -- what the tools row does to the pattern. Each click computes a new pattern with the pure
// ops (Modules/Sequencer/SequencerPatternOps.h), writes it with setPattern and records ONE undo step; the bars glide
// from the old pattern because the grid's own edits do not start a glide by themselves. Double also sets Steps, in the
// same undo step. Pitch tools work on the pitch a step plays (written plus Transpose, through the scale), so a
// transposed pattern moves and fits as it sounds.
#include "StepGridComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include <cmath>

namespace synth::ui {

using namespace stepgrid;
using Tool = StepGridTools::Tool;
namespace ops = synth::sequencer_ops;

namespace test_hooks {
std::function<void(std::unique_ptr<StepGridRandomizePanel>)>& randomizePanelHookForTest() {
    static std::function<void(std::unique_ptr<StepGridRandomizePanel>)> hook;
    return hook;
}
} // namespace test_hooks

namespace {

// The note names in the Randomize panel use the same octave numbering as the grid (middle C is C3).
constexpr int kMiddleCOctave = 3;

int shiftedPitch(int pitch, int by) { return juce::jlimit(0, 127, pitch + by); }

} // namespace

// Runs `op` on a copy whose active pitches are the ones that play, then writes the result back as written pitches.
SequencerPattern
StepGridComponent::inPlayedDomain(const SequencerPattern& pattern,
                                  const std::function<SequencerPattern(const SequencerPattern&)>& op) const {
    auto played = pattern;
    const int n = std::min(stepCount(), kMaxSequencerSteps);
    for (int i = 0; i < n; ++i)
        played.steps[(size_t)i].pitch = (std::uint8_t)shiftedPitch(pattern.steps[(size_t)i].pitch, transpose_);
    auto result = op(played);
    for (int i = 0; i < n; ++i)
        result.steps[(size_t)i].pitch = (std::uint8_t)shiftedPitch(result.steps[(size_t)i].pitch, -transpose_);
    return result;
}

void StepGridComponent::updateToolAvailability() {
    const int n = stepCount();
    const bool canFit = ops::fitToScaleWouldChange(
        inPlayedDomain(module_.getPattern(), [](const SequencerPattern& p) { return p; }), n, scale_);
    tools_.setAvailability(ops::canDouble(n), canFit);
}

// Writes `next`, records one undo step and glides the bars from the pattern as it was. `newSteps` (Double) is set in
// the same step.
void StepGridComponent::commitPatternTool(const SequencerPattern& next, std::optional<int> newSteps) {
    const auto beforePattern = module_.getPattern();
    const auto beforeState = module_.getExtraState();
    const float oldSteps =
        stepsParam_ != nullptr ? static_cast<juce::RangedAudioParameter*>(stepsParam_)->getValue() : 0.0f;
    module_.setPattern(next);
    if (newSteps.has_value() && stepsParam_ != nullptr)
        stepsParam_->setValueNotifyingHost(stepsParam_->convertTo0to1((float)*newSteps));
    seenGeneration_ = module_.patternGeneration();
    lastSeen_ = module_.getPattern();
    syncSettings();
    refreshPitchRange();
    startGlide(beforePattern);

    if (newSteps.has_value() && stepsParam_ != nullptr && hooks_.recordEditWithParameter)
        hooks_.recordEditWithParameter(beforeState, module_.getExtraState(), stepsParam_->paramID, oldSteps,
                                       static_cast<juce::RangedAudioParameter*>(stepsParam_)->getValue());
    else
        commit(beforeState);
    updateToolAvailability();
    repaint();
}

void StepGridComponent::applyTool(Tool tool) {
    const auto pattern = module_.getPattern();
    const int n = stepCount();
    switch (tool) {
    case Tool::Randomize:
        openRandomizePanel();
        return;
    case Tool::ShiftLeft:
        commitPatternTool(ops::shiftLeft(pattern, n), std::nullopt);
        return;
    case Tool::ShiftRight:
        commitPatternTool(ops::shiftRight(pattern, n), std::nullopt);
        return;
    case Tool::Reverse:
        commitPatternTool(ops::reversed(pattern, n), std::nullopt);
        return;
    case Tool::Double:
        if (ops::canDouble(n))
            commitPatternTool(ops::doubled(pattern, n), n * 2);
        return;
    case Tool::Up:
    case Tool::Down: {
        const int direction = tool == Tool::Up ? 1 : -1;
        commitPatternTool(
            inPlayedDomain(pattern,
                           [&](const SequencerPattern& p) { return ops::movedByScaleStep(p, n, scale_, direction); }),
            std::nullopt);
        return;
    }
    case Tool::FitToScale:
        if (tools_.getButton(Tool::FitToScale).isEnabled())
            commitPatternTool(
                inPlayedDomain(pattern, [&](const SequencerPattern& p) { return ops::fitToScale(p, n, scale_); }),
                std::nullopt);
        return;
    }
}

void StepGridComponent::roll(const ops::RandomizeSettings& settings) {
    randomize_ = settings;
    const int n = stepCount();
    commitPatternTool(inPlayedDomain(module_.getPattern(),
                                     [&](const SequencerPattern& p) {
                                         return ops::randomized(p, n, scale_, settings,
                                                                juce::Random::getSystemRandom());
                                     }),
                      std::nullopt);
}

// The popover starts two octaves up from the key's root in octave 2 (C2 to C4 with no key, D2 to D4 in D; the grid's
// numbering), widened to take in any note the pattern already plays. A key change before the range was touched
// moves it to the new root; a range the user set stays.
void StepGridComponent::seedRandomizeRange() {
    const int root = scale_.isChromatic() ? 0 : ((scale_.rootPitchClass % 12) + 12) % 12;
    if (randomizeSeeded_ && (root == seededRoot_ || randomizeRangeTouched()))
        return;
    randomizeSeeded_ = true;
    seededRoot_ = root;
    int lo = 48 + root;
    int hi = 72 + root;
    for (int i = 0; i < std::min(stepCount(), kMaxSequencerSteps); ++i) {
        lo = std::min(lo, playedPitchOf(i));
        hi = std::max(hi, playedPitchOf(i));
    }
    randomize_.minPitch = lo;
    randomize_.maxPitch = hi;
    seededRange_ = {lo, hi};
}

void StepGridComponent::openRandomizePanel() {
    seedRandomizeRange();
    auto panel = std::make_unique<StepGridRandomizePanel>(randomize_, scale_, kMiddleCOctave);
    juce::Component::SafePointer<StepGridComponent> safe(this);
    panel->onSettingsChanged = [safe](const ops::RandomizeSettings& settings) {
        if (safe != nullptr)
            safe->randomize_ = settings;
    };
    panel->onRoll = [safe](const ops::RandomizeSettings& settings) {
        if (safe != nullptr)
            safe->roll(settings);
    };
    if (auto& hook = test_hooks::randomizePanelHookForTest()) {
        hook(std::move(panel));
        return;
    }
    juce::CallOutBox::launchAsynchronously(std::move(panel), tools_.getButton(Tool::Randomize).getScreenBounds(),
                                           nullptr);
}

} // namespace synth::ui
