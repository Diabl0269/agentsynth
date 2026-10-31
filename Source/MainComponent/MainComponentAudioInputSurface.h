#pragma once
#include "MainComponent/MainComponentAudioSettingsSource.h" // the Settings Audio tab's view of the engine, owned beside this one

#include "Timeline/TimelineDoc/TimelineTypes.h"
#include "UI/Timeline/TrackAudioInputSurface.h"
#include <functional>

class AudioEngine;

namespace synth {
class TimelineDoc;
}

namespace synth::maincomponent {

// MainComponent's answer to the routing pane's audio Input row (synth::ui::TrackAudioInputSurface): the engine's
// inputs, levels and input switch-on, plus opening Settings on its Audio tab.
class AudioInputSurface : public synth::ui::TrackAudioInputSurface {
public:
    AudioInputSurface(AudioEngine& engine, const synth::TimelineDoc& doc, std::function<void()> openAudioSettings);

    synth::AudioInputStatus getAudioInputStatus() override;
    float readInputPeak(int channel) override;
    void enableDeviceInputs() override;
    void openAudioSettings() override;
    bool trackHasMixerChannel(synth::TrackId id) override;

private:
    AudioEngine& engine_;
    const synth::TimelineDoc& doc_;
    std::function<void()> openAudioSettings_;
};

} // namespace synth::maincomponent
