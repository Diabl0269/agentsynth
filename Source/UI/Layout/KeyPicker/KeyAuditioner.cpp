#include "UI/Layout/KeyPicker/KeyAuditioner.h"
#include "Music/SongKey.h"
#include "UI/Layout/KeyPicker/KeyPicker.h"
#include <juce_data_structures/juce_data_structures.h>

namespace synth::ui {

KeyAuditioner::KeyAuditioner() = default;

KeyAuditioner::~KeyAuditioner() { stop(); }

bool KeyAuditioner::isEnabledIn(const juce::PropertiesFile* settings) {
    return settings == nullptr || settings->getBoolValue(kPlayScaleOnBrowseSettingsKey, true);
}

std::vector<int> KeyAuditioner::pitchesFor(const MusicalScale& scale) {
    std::vector<int> pitches;
    const int root = 60 + ((scale.rootPitchClass % 12) + 12) % 12;
    for (int pitch = root; pitch <= root + 12; ++pitch)
        if (scale.contains(pitch))
            pitches.push_back(pitch);
    return pitches;
}

// The target is asked for once and kept: the selection or the open clip can change mid-run, and the note-off still has
// to reach the module the note-on went to. The default sound is one rendered run, so it needs no timer; it is cut on
// stop only while it can still be sounding, so closing a picker later never cuts a Sounds view preview.
void KeyAuditioner::start(const MusicalScale& scale) {
    stop();
    if (!isEnabledIn(settings_))
        return;
    pitches_ = pitchesFor(scale);
    if (pitches_.empty())
        return;
    Target target = resolveTarget ? resolveTarget() : Target{};
    if (!target.note) {
        if (!playDefaultSound)
            return;
        playDefaultSound(pitches_);
        defaultSounding_ = true;
        const int runMs = kStepMs * ((int)pitches_.size() + 3);
        defaultEndMs_ = juce::Time::getMillisecondCounter() + (juce::uint32)runMs;
        return;
    }
    target_ = std::move(target);
    step_ = 0;
    timerCallback();
    startTimer(kStepMs);
}

void KeyAuditioner::stop() {
    stopTimer();
    if (target_.has_value() && soundingPitch_ >= 0 && target_->note)
        target_->note(soundingPitch_, 0, false);
    soundingPitch_ = -1;
    target_.reset();
    pitches_.clear();
    if (defaultSounding_) {
        defaultSounding_ = false;
        const bool stillRinging = (juce::int32)(defaultEndMs_ - juce::Time::getMillisecondCounter()) > 0;
        if (stillRinging && stopDefaultSound)
            stopDefaultSound();
    }
}

void KeyAuditioner::timerCallback() {
    if (!target_.has_value() || !target_->note) {
        stop();
        return;
    }
    if (soundingPitch_ >= 0)
        target_->note(soundingPitch_, 0, false);
    soundingPitch_ = -1;
    if (step_ >= pitches_.size()) {
        stop();
        return;
    }
    soundingPitch_ = pitches_[step_++];
    target_->note(soundingPitch_, kVelocity, true);
}

void KeyAuditioner::wire(KeyPicker& picker) {
    const std::weak_ptr<int> alive = life_;
    picker.onAudition = [this, alive](std::optional<MusicalScale> scale) {
        if (alive.expired())
            return;
        if (scale.has_value())
            start(*scale);
        else
            stop();
    };
    picker.onClosed = [this, alive] {
        if (!alive.expired())
            stop();
    };
}

} // namespace synth::ui
