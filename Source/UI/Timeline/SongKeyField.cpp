#include "UI/Timeline/SongKeyField.h"
#include "Music/SongKey.h"
#include "Transport/TransportService.h"
#include "UI/Layout/KeyPicker/KeyAuditioner.h"
#include "UI/Layout/KeyPicker/KeyPicker.h"
#include <algorithm>

namespace synth::ui {

SongKeyField::SongKeyField()
    : auditioner_(std::make_unique<KeyAuditioner>()) {
    setComponentID("timelineTransportSongKey");
    setTitle("Song key");
    setTooltip("Song key: what sequencers, the piano roll and new clips follow. Click to change.");
    onOpen = [this] { openPicker(); };
    // Asked once per run: the previewed instrument track while it is still in the project, else the default sound.
    auditioner_->resolveTarget = [this]() -> KeyAuditioner::Target {
        if (!previewTrack_.has_value() || !auditionNote || !previewTracks)
            return {};
        const auto track = *previewTrack_;
        const auto tracks = previewTracks();
        const bool listed = std::any_of(tracks.begin(), tracks.end(), [track](const auto& t) { return t.id == track; });
        if (!listed)
            return {};
        return {[this, track](int pitch, int velocity, bool on) {
            if (auditionNote)
                auditionNote(track, pitch, velocity, on);
        }};
    };
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

    // The Preview sound combo lists the project's instrument tracks as they are now; a track that was chosen and is
    // gone is shown as the default sound, which is also what its next audition plays.
    const auto tracks = previewTracks ? previewTracks() : std::vector<PreviewTrack>{};
    options.showPreviewSound = true;
    for (size_t i = 0; i < tracks.size(); ++i) {
        options.previewTracks.push_back(tracks[i].name);
        if (previewTrack_.has_value() && *previewTrack_ == tracks[i].id)
            options.previewSelected = (int)i + 1;
    }

    // Enabled only while a song key is set: it fits to that key, not to a key the list is highlighting.
    options.showFitProject = (bool)fitProjectToKey;
    options.fitProjectEnabled = getKey().has_value();

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
    picker->onFitProject = [self] {
        if (self == nullptr || !self->fitProjectToKey || self->transport_ == nullptr)
            return;
        if (const auto key = self->transport_->getDocumentState().songKey)
            self->fitProjectToKey(*key);
    };
    auditioner_->wire(*picker);
    picker->onPreviewSoundChanged = [self, tracks = std::move(tracks)](int choice) {
        if (self != nullptr)
            self->setPreviewTrack(choice >= 1 && (size_t)choice <= tracks.size()
                                      ? std::optional<synth::TrackId>(tracks[(size_t)choice - 1].id)
                                      : std::nullopt);
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

// The chosen track is used only while the project still lists it; otherwise (or with no choice) the run plays the
// default sound, even when a track is selected. The target is built once per run by the auditioner.
void SongKeyField::setApplicationProperties(juce::ApplicationProperties* props) {
    appProperties_ = props;
    auditioner_->setSettings(props != nullptr ? props->getUserSettings() : nullptr);
}

void SongKeyField::startAudition(const MusicalScale& scale) { auditioner_->start(scale); }
void SongKeyField::stopAudition() { auditioner_->stop(); }
bool SongKeyField::isAuditioning() const noexcept { return auditioner_->isAuditioning(); }
void SongKeyField::advanceAuditionForTest() { auditioner_->advanceForTest(); }

} // namespace synth::ui
