// DrumKitModule: parameters, the MIDI note map, sample-accurate triggering and the per-sample mix.
// The drum sounds themselves are in DrumVoices.h.

#include "DrumKitModule.h"

namespace {

constexpr std::array<int, 6> kTomNotes = {41, 43, 45, 47, 48, 50};
// Low floor tom up to high tom, in Hz.
constexpr std::array<float, 6> kTomHz = {82.0f, 98.0f, 117.0f, 140.0f, 165.0f, 196.0f};

int tomIndexForNote(int note) {
    for (std::size_t i = 0; i < kTomNotes.size(); ++i)
        if (kTomNotes[i] == note)
            return static_cast<int>(i);
    return -1;
}

} // namespace

DrumKitModule::Drum DrumKitModule::drumForNote(int note) noexcept {
    switch (note) {
    case 35:
    case 36:
        return Drum::Kick;
    case 38:
    case 40:
        return Drum::Snare;
    case 37:
        return Drum::Rim;
    case 39:
        return Drum::Clap;
    case 42:
    case 44:
        return Drum::ClosedHat;
    case 46:
        return Drum::OpenHat;
    case 49:
    case 57:
        return Drum::Crash;
    case 51:
    case 59:
        return Drum::Ride;
    case 56:
        return Drum::Cowbell;
    default:
        return tomIndexForNote(note) >= 0 ? Drum::Tom : Drum::None;
    }
}

juce::AudioParameterFloat* DrumKitModule::addFloat(const char* id, const char* name, float lo, float hi, float def) {
    auto* p = new juce::AudioParameterFloat(id, name, lo, hi, def);
    addParameter(p);
    return p;
}

DrumKitModule::DrumKitModule()
    : ModuleBase("Drum Kit", 0, kNumChannels, StereoAudio::Declared) {
    // Tune is in semitones around the drum's own pitch; decay is the time the drum takes to fall 60 dB.
    kickLevel = addFloat("kickLevel", "Kick level", 0.0f, 1.0f, 0.9f);
    kickTune = addFloat("kickTune", "Kick tune", -12.0f, 12.0f, 0.0f);
    kickDecay = addFloat("kickDecay", "Kick decay", 0.1f, 1.2f, 0.35f);
    snareLevel = addFloat("snareLevel", "Snare level", 0.0f, 1.0f, 0.8f);
    snareTune = addFloat("snareTune", "Snare tune", -12.0f, 12.0f, 0.0f);
    snareDecay = addFloat("snareDecay", "Snare decay", 0.05f, 0.6f, 0.18f);
    clapLevel = addFloat("clapLevel", "Clap level", 0.0f, 1.0f, 0.8f);
    clapTune = addFloat("clapTune", "Clap tune", -12.0f, 12.0f, 0.0f);
    clapDecay = addFloat("clapDecay", "Clap decay", 0.05f, 0.8f, 0.2f);
    hatLevel = addFloat("hatLevel", "Hat level", 0.0f, 1.0f, 0.6f);
    hatTune = addFloat("hatTune", "Hat tune", -12.0f, 12.0f, 0.0f);
    closedDecay = addFloat("closedDecay", "Closed decay", 0.02f, 0.3f, 0.05f);
    openDecay = addFloat("openDecay", "Open decay", 0.1f, 1.5f, 0.45f);
    tomLevel = addFloat("tomLevel", "Tom level", 0.0f, 1.0f, 0.8f);
    tomTune = addFloat("tomTune", "Tom tune", -12.0f, 12.0f, 0.0f);
    tomDecay = addFloat("tomDecay", "Tom decay", 0.1f, 1.0f, 0.3f);
    cymbalLevel = addFloat("cymbalLevel", "Cymbal level", 0.0f, 1.0f, 0.5f);
    cymbalTune = addFloat("cymbalTune", "Cymbal tune", -12.0f, 12.0f, 0.0f);
    cymbalDecay = addFloat("cymbalDecay", "Cymbal decay", 0.3f, 4.0f, 1.5f);
    cowbellLevel = addFloat("cowbellLevel", "Cowbell level", 0.0f, 1.0f, 0.8f);
    cowbellTune = addFloat("cowbellTune", "Cowbell tune", -12.0f, 12.0f, 0.0f);
    cowbellDecay = addFloat("cowbellDecay", "Cowbell decay", 0.1f, 0.6f, 0.25f);
    levelParam = addFloat("level", "Level", 0.0f, 1.0f, 0.7f);
    addMuteParameter();
    enableVisualBuffer(true);
}

void DrumKitModule::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    smoothedLevel_.reset(sampleRate_, 0.01);
    smoothedLevel_.setCurrentAndTargetValue(levelParam->get());
    stopAll();
}

void DrumKitModule::stopAll() noexcept {
    kick_.stop();
    snare_.stop();
    rim_.stop();
    clap_.stop();
    closedHat_.stop();
    openHat_.stop();
    crash_.stop();
    ride_.stop();
    cowbell_.stop();
    for (auto& tom : toms_)
        tom.stop();
}

DrumKitModule::Hit DrumKitModule::hitFor(juce::AudioParameterFloat* level, juce::AudioParameterFloat* tune,
                                         float decaySeconds, float velocity) const {
    Hit hit;
    hit.velocity = juce::jlimit(0.0f, 1.0f, velocity);
    hit.gain = level->get();
    hit.tune = synth::drums::semitoneRatio(tune->get());
    hit.decay = decaySeconds;
    return hit;
}

void DrumKitModule::trigger(int note, float velocity) {
    using synth::drums::MetallicKind;
    switch (drumForNote(note)) {
    case Drum::Kick:
        kick_.trigger(hitFor(kickLevel, kickTune, kickDecay->get(), velocity), sampleRate_);
        break;
    case Drum::Snare:
        snare_.trigger(hitFor(snareLevel, snareTune, snareDecay->get(), velocity), false, sampleRate_);
        break;
    case Drum::Rim:
        rim_.trigger(hitFor(snareLevel, snareTune, snareDecay->get(), velocity), true, sampleRate_);
        break;
    case Drum::Clap:
        clap_.trigger(hitFor(clapLevel, clapTune, clapDecay->get(), velocity), sampleRate_);
        break;
    case Drum::ClosedHat: {
        // The pedal hat (44) is a little shorter and softer than the closed hat (42).
        const bool pedal = note == 44;
        auto hit = hitFor(hatLevel, hatTune, closedDecay->get() * (pedal ? 0.8f : 1.0f), velocity);
        if (pedal)
            hit.gain *= 0.8f;
        openHat_.choke(sampleRate_);
        closedHat_.trigger(hit, MetallicKind::ClosedHat, sampleRate_);
        break;
    }
    case Drum::OpenHat:
        openHat_.trigger(hitFor(hatLevel, hatTune, openDecay->get(), velocity), MetallicKind::OpenHat, sampleRate_);
        break;
    case Drum::Tom: {
        const auto index = static_cast<std::size_t>(tomIndexForNote(note));
        toms_[index].trigger(hitFor(tomLevel, tomTune, tomDecay->get(), velocity), kTomHz[index], sampleRate_);
        break;
    }
    case Drum::Crash:
        crash_.trigger(hitFor(cymbalLevel, cymbalTune, cymbalDecay->get(), velocity), MetallicKind::Crash, sampleRate_);
        break;
    case Drum::Ride:
        ride_.trigger(hitFor(cymbalLevel, cymbalTune, cymbalDecay->get() * 0.8f, velocity), MetallicKind::Ride,
                      sampleRate_);
        break;
    case Drum::Cowbell:
        cowbell_.trigger(hitFor(cowbellLevel, cowbellTune, cowbellDecay->get(), velocity), sampleRate_);
        break;
    case Drum::None:
        break;
    }
}

float DrumKitModule::renderSample() noexcept {
    float sum = 0.0f;
    if (kick_.active())
        sum += kick_.tick(sampleRate_, noise_);
    if (snare_.active())
        sum += snare_.tick(sampleRate_, noise_);
    if (rim_.active())
        sum += rim_.tick(sampleRate_, noise_);
    if (clap_.active())
        sum += clap_.tick(noise_);
    if (closedHat_.active())
        sum += closedHat_.tick(sampleRate_, noise_);
    if (openHat_.active())
        sum += openHat_.tick(sampleRate_, noise_);
    if (crash_.active())
        sum += crash_.tick(sampleRate_, noise_);
    if (ride_.active())
        sum += ride_.tick(sampleRate_, noise_);
    if (cowbell_.active())
        sum += cowbell_.tick(sampleRate_);
    for (auto& tom : toms_)
        if (tom.active())
            sum += tom.tick(sampleRate_);
    sum *= smoothedLevel_.getNextValue();
    // Several drums at full velocity can sum past full scale: round only the excess above 0.9.
    const float mag = std::fabs(sum);
    if (mag > 0.9f)
        sum = std::copysign(0.9f + 0.08f * std::tanh((mag - 0.9f) * 12.5f), sum);
    return sum;
}

void DrumKitModule::processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0 || numSamples <= 0)
        return;

    // A pure source has no dry signal to pass through, so bypass and mute both clear. They stay two
    // branches, and either one cuts every drum that is still ringing.
    if (isBypassed()) {
        buffer.clear();
        stopAll();
        return;
    }
    if (isMuted()) {
        buffer.clear();
        stopAll();
        return;
    }

    smoothedLevel_.setTargetValue(levelParam->get());
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    int position = 0;
    const auto renderTo = [&](int end) {
        for (int i = position; i < end; ++i) {
            const float s = renderSample();
            left[i] = s;
            if (right != nullptr)
                right[i] = s;
        }
        position = end;
    };
    for (const auto metadata : midiMessages) {
        const auto message = metadata.getMessage();
        if (!message.isNoteOn())
            continue; // a drum plays out its own decay; Note-Off and the rest do nothing
        renderTo(juce::jlimit(position, numSamples, metadata.samplePosition));
        trigger(message.getNoteNumber(), message.getFloatVelocity());
    }
    renderTo(numSamples);

    if (auto* vb = getVisualBuffer())
        vb->pushBlock(left, numSamples);
}
