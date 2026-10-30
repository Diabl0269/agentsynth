#pragma once

#include "Music/MusicalScale.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/KeyField.h"
#include "UI/Layout/KeyPicker/KeyAuditioner.h"
#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace synth {
class TransportService; // Source/Transport/TransportService.h
} // namespace synth

namespace synth::ui {

class KeyPicker; // Source/UI/Layout/KeyPicker/KeyPicker.h

/** The transport bar's Key field: shows the song key, opens the key picker, and sets the song key on the transport
 *  (one undo step, through the transport poll). Hovering a key in the picker plays its scale once up the octave,
 *  quietly: with the built-in default sound, or through the instrument track the picker's Preview sound combo chose
 *  (kept for the session). docs/timeline/song-key.md. */
class SongKeyField : public KeyField {
public:
    SongKeyField();
    ~SongKeyField() override;

    /** Non-owning; null makes the field show No key and picks do nothing. */
    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }
    /** Where the user's own scales are read from; optional. */
    void setApplicationProperties(juce::ApplicationProperties* props) noexcept {
        appProperties_ = props;
        auditioner_.setSettings(props != nullptr ? props->getUserSettings() : nullptr);
    }

    /** Message thread: re-reads the song key from the transport (the bar's 10 Hz poll). */
    void refreshFromTransport();

    /** How long each pitch class sounds in the song (Detect from notes); unset hides the button. */
    std::function<std::array<double, 12>()> pitchClassWeights;
    /** One instrument track the Preview sound combo offers. */
    struct PreviewTrack {
        synth::TrackId id;
        juce::String name;
    };
    /** The project's instrument tracks, in track order; unset = only the default sound is offered. */
    std::function<std::vector<PreviewTrack>()> previewTracks;
    /** Plays or ends one audition note on `track`. */
    std::function<void(synth::TrackId track, int pitch, int velocity, bool on)> auditionNote;
    /** Launches the picker; unset = a CallOutBox from the field. Tests replace it to keep the picker in hand. */
    std::function<void(std::unique_ptr<juce::Component> picker, juce::Rectangle<int> screenArea)> pickerLauncher;

    /** Opens the key picker (the field's click, Space, Return, Down, and the Set song key command). */
    void openPicker();
    /** The open picker, or null. */
    KeyPicker* getOpenPicker() const noexcept;

    /** Starts (or restarts) playing `scale` once up the octave; ends any audition already sounding. */
    void startAudition(const MusicalScale& scale) { auditioner_.start(scale); }
    /** Ends the audition, sending the note-off it still owes. */
    void stopAudition() { auditioner_.stop(); }
    bool isAuditioning() const noexcept { return auditioner_.isAuditioning(); }
    /** The default sound's player is `getAuditioner().playDefaultSound`; the app sets it. */
    KeyAuditioner& getAuditioner() noexcept { return auditioner_; }
    /** The instrument track the next audition plays through; nullopt = the default sound. Held for the session, never
     *  saved; a track that is gone by the next audition falls back to the default sound. */
    void setPreviewTrack(std::optional<synth::TrackId> track) { previewTrack_ = track; }
    std::optional<synth::TrackId> getPreviewTrack() const noexcept { return previewTrack_; }
    /** Test seam: plays the next audition step at once. */
    void advanceAuditionForTest() { auditioner_.advanceForTest(); }

    static constexpr int kAuditionStepMs = KeyAuditioner::kStepMs;
    static constexpr int kAuditionVelocity = KeyAuditioner::kVelocity;

private:
    void pick(const std::optional<MusicalScale>& key);
    KeyAuditioner::Target resolveAuditionTarget() const;

    synth::TransportService* transport_ = nullptr;
    juce::ApplicationProperties* appProperties_ = nullptr;
    juce::Component::SafePointer<juce::Component> openPicker_;
    std::optional<synth::TrackId> previewTrack_;
    KeyAuditioner auditioner_;

    JUCE_DECLARE_NON_COPYABLE(SongKeyField)
};

} // namespace synth::ui
