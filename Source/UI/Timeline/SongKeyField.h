#pragma once

#include "Music/MusicalScale.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/KeyField.h"
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
 *  quietly, on the audition track. docs/timeline/song-key.md. */
class SongKeyField
    : public KeyField
    , private juce::Timer {
public:
    SongKeyField();
    ~SongKeyField() override;

    /** Non-owning; null makes the field show No key and picks do nothing. */
    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }
    /** Where the user's own scales are read from; optional. */
    void setApplicationProperties(juce::ApplicationProperties* props) noexcept { appProperties_ = props; }

    /** Message thread: re-reads the song key from the transport (the bar's 10 Hz poll). */
    void refreshFromTransport();

    /** How long each pitch class sounds in the song (Detect from notes); unset hides the button. */
    std::function<std::array<double, 12>()> pitchClassWeights;
    /** The track a scale audition plays on, resolved once per audition; nullopt = silent. */
    std::function<std::optional<synth::TrackId>()> resolveAuditionTrack;
    /** Plays or ends one audition note on `track`. */
    std::function<void(synth::TrackId track, int pitch, int velocity, bool on)> auditionNote;
    /** Launches the picker; unset = a CallOutBox from the field. Tests replace it to keep the picker in hand. */
    std::function<void(std::unique_ptr<juce::Component> picker, juce::Rectangle<int> screenArea)> pickerLauncher;

    /** Opens the key picker (the field's click, Space, Return, Down, and the Set song key command). */
    void openPicker();
    /** The open picker, or null. */
    KeyPicker* getOpenPicker() const noexcept;

    /** Starts (or restarts) playing `scale` once up the octave; ends any audition already sounding. */
    void startAudition(const MusicalScale& scale);
    /** Ends the audition, sending the note-off it still owes. */
    void stopAudition();
    bool isAuditioning() const noexcept { return auditionTrack_.has_value(); }
    /** Test seam: plays the next audition step at once. */
    void advanceAuditionForTest() { timerCallback(); }

    static constexpr int kAuditionStepMs = 140;
    static constexpr int kAuditionVelocity = 48;

private:
    void timerCallback() override;
    void pick(const std::optional<MusicalScale>& key);

    synth::TransportService* transport_ = nullptr;
    juce::ApplicationProperties* appProperties_ = nullptr;
    juce::Component::SafePointer<juce::Component> openPicker_;

    std::vector<int> auditionPitches_;
    size_t auditionStep_ = 0;
    int soundingPitch_ = -1;
    std::optional<synth::TrackId> auditionTrack_;

    JUCE_DECLARE_NON_COPYABLE(SongKeyField)
};

} // namespace synth::ui
