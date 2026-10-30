#pragma once

#include "Music/MusicalScale.h"
#include <functional>
#include <juce_events/juce_events.h>
#include <memory>
#include <optional>
#include <vector>

namespace juce {
class PropertiesFile;
}

namespace synth::ui {

class KeyPicker; // Source/UI/Layout/KeyPicker/KeyPicker.h

/** How the app plays the default sound; the key fields never see the audio engine, the app wires these. */
struct DefaultSoundHooks {
    std::function<void(const std::vector<int>& pitches)> play;
    std::function<void()> stop;
};

/** Plays a key's scale once while the user browses a key picker: the root at middle C's octave, every scale note up to
 *  the root an octave above, one step apart, quietly. Each key field owns one. It sends the notes to whatever its
 *  `resolveTarget` names, or plays the built-in default sound when that names nothing, and does nothing while the
 *  "Play the scale when browsing keys" setting is off. Message thread only. docs/timeline/song-key.md#hearing-a-key. */
class KeyAuditioner : private juce::Timer {
public:
    /** Where one run goes. An empty `note` means the default sound. */
    struct Target {
        std::function<void(int pitch, int velocity, bool on)> note;
    };

    KeyAuditioner();
    ~KeyAuditioner() override;

    /** Asked once per run, when it starts; the answer is kept for the whole run so every note-off goes where its
     *  note-on went. Unset = the default sound. */
    std::function<Target()> resolveTarget;
    /** Plays these pitches with the default sound, one step apart; unset = silent. */
    std::function<void(const std::vector<int>& pitches)> playDefaultSound;
    /** Cuts the default sound; called only while a run's sound is still ringing. */
    std::function<void()> stopDefaultSound;

    /** Where the on/off setting is read, at the start of each run; null = on. Non-owning. */
    void setSettings(juce::PropertiesFile* settings) noexcept { settings_ = settings; }
    /** True unless the user turned "Play the scale when browsing keys" off in `settings`. */
    static bool isEnabledIn(const juce::PropertiesFile* settings);

    /** Starts (or restarts) playing `scale`; ends any run already sounding. Does nothing while the setting is off. */
    void start(const MusicalScale& scale);
    /** Ends the run, sending the note-off it still owes. */
    void stop();
    bool isAuditioning() const noexcept { return target_.has_value() || defaultSounding_; }
    /** True while the run goes to the default sound rather than a note target. */
    bool isUsingDefaultSound() const noexcept { return defaultSounding_; }
    /** Hooks `picker`'s hover and close to this auditioner; safe if the auditioner goes first. */
    void wire(KeyPicker& picker);
    /** Test seam: plays the next step at once. */
    void advanceForTest() { timerCallback(); }

    /** The pitches of a run of `scale`: its notes from the root (at middle C's octave) up to the root an octave up. */
    static std::vector<int> pitchesFor(const MusicalScale& scale);

    static constexpr int kStepMs = 140;
    static constexpr int kVelocity = 48;

private:
    void timerCallback() override;

    juce::PropertiesFile* settings_ = nullptr;
    std::vector<int> pitches_;
    size_t step_ = 0;
    int soundingPitch_ = -1;
    std::optional<Target> target_;
    bool defaultSounding_ = false;
    juce::uint32 defaultEndMs_ = 0;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);

    JUCE_DECLARE_NON_COPYABLE(KeyAuditioner)
};

} // namespace synth::ui
