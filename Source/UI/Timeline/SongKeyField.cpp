#include "UI/Timeline/SongKeyField.h"
#include "Music/SongKey.h"
#include "Transport/TransportService.h"
#include "UI/Layout/KeyPicker/KeyPicker.h"

namespace synth::ui {

SongKeyField::SongKeyField() {
    setComponentID("timelineTransportSongKey");
    setTitle("Song key");
    setTooltip("Song key: what sequencers, the piano roll and new clips follow. Click to change.");
    onOpen = [this] { openPicker(); };
}

SongKeyField::~SongKeyField() {
    stopAudition();
    if (auto* picker = getOpenPicker()) {
        picker->onPick = nullptr;
        picker->onAudition = nullptr;
        picker->onClosed = nullptr;
    }
}

void SongKeyField::refreshFromTransport() {
    setKey(transport_ != nullptr ? transport_->getDocumentState().songKey : std::nullopt);
}

KeyPicker* SongKeyField::getOpenPicker() const noexcept { return dynamic_cast<KeyPicker*>(openPicker_.getComponent()); }

// One picker at a time: a second open while one is showing (a double Return) is ignored rather than stacking boxes.
void SongKeyField::openPicker() {
    if (openPicker_ != nullptr)
        return;
    refreshFromTransport();

    KeyPicker::Options options;
    options.current = getKey();
    options.showNoKey = true;
    options.what = "Song key";
    if (appProperties_ != nullptr && appProperties_->getUserSettings() != nullptr)
        options.userScales = parseUserScales(appProperties_->getUserSettings()->getValue(kUserScalesSettingsKey, {}));

    juce::Component::SafePointer<SongKeyField> self(this);
    auto picker = std::make_unique<KeyPicker>(std::move(options), [self](std::optional<MusicalScale> key) {
        if (self != nullptr)
            self->pick(key);
    });
    if (pitchClassWeights) {
        picker->detect = [self]() -> std::optional<MusicalScale> {
            if (self == nullptr || !self->pitchClassWeights)
                return std::nullopt;
            return detectKey(self->pitchClassWeights());
        };
    }
    picker->onAudition = [self](std::optional<MusicalScale> scale) {
        if (self == nullptr)
            return;
        if (scale.has_value())
            self->startAudition(*scale);
        else
            self->stopAudition();
    };
    picker->onClosed = [self] {
        if (self != nullptr)
            self->stopAudition();
    };

    openPicker_ = picker.get();
    const auto area = getScreenBounds();
    if (pickerLauncher)
        pickerLauncher(std::move(picker), area);
    else
        juce::CallOutBox::launchAsynchronously(std::move(picker), area, nullptr);
}

// The key is set on the transport's document state; the transport poll in MainComponent records it as one undo step,
// exactly like a tempo or time-signature edit, and undo puts it back through the same path.
void SongKeyField::pick(const std::optional<MusicalScale>& key) {
    stopAudition();
    if (transport_ != nullptr)
        transport_->setSongKey(key);
    setKey(key);
}

// Root at middle C's octave, then every scale note up to the root an octave above. The track is latched for the
// whole run, so the note-off always goes where its note-on went even if the selection changes mid-run.
void SongKeyField::startAudition(const MusicalScale& scale) {
    stopAudition();
    if (!resolveAuditionTrack || !auditionNote)
        return;
    const auto track = resolveAuditionTrack();
    if (!track.has_value())
        return;
    auditionPitches_.clear();
    const int root = 60 + ((scale.rootPitchClass % 12) + 12) % 12;
    for (int pitch = root; pitch <= root + 12; ++pitch)
        if (scale.contains(pitch))
            auditionPitches_.push_back(pitch);
    if (auditionPitches_.empty())
        return;
    auditionTrack_ = track;
    auditionStep_ = 0;
    timerCallback();
    startTimer(kAuditionStepMs);
}

void SongKeyField::stopAudition() {
    stopTimer();
    if (auditionTrack_.has_value() && soundingPitch_ >= 0 && auditionNote)
        auditionNote(*auditionTrack_, soundingPitch_, 0, false);
    soundingPitch_ = -1;
    auditionTrack_.reset();
    auditionPitches_.clear();
}

void SongKeyField::timerCallback() {
    if (!auditionTrack_.has_value() || !auditionNote) {
        stopAudition();
        return;
    }
    if (soundingPitch_ >= 0)
        auditionNote(*auditionTrack_, soundingPitch_, 0, false);
    soundingPitch_ = -1;
    if (auditionStep_ >= auditionPitches_.size()) {
        stopAudition();
        return;
    }
    soundingPitch_ = auditionPitches_[auditionStep_++];
    auditionNote(*auditionTrack_, soundingPitch_, kAuditionVelocity, true);
}

} // namespace synth::ui
