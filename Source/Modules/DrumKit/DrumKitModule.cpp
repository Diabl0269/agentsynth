// DrumKitModule: parameters, the MIDI note map, the block loop and the per-sample mix. The drum sounds are in
// DrumVoices.h, the pads in DrumKitModulePads.cpp (message thread) and DrumKitModulePadAudio.cpp (triggering), the
// recorded kit in DrumKitModuleRecorded.cpp.

#include "DrumKitModule.h"
#include "Modules/ParameterText.h"

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
        return synth::drums::tomIndexForSound(note) >= 0 ? Drum::Tom : Drum::None;
    }
}

juce::AudioParameterFloat* DrumKitModule::addFloat(const char* id, const char* name, float lo, float hi, float def) {
    auto* p = new juce::AudioParameterFloat(id, name, juce::NormalisableRange<float>(lo, hi), def,
                                            (lo == 0.0f && hi == 1.0f) ? synth::fractionPercentAttributes()
                                                                       : juce::AudioParameterFloatAttributes());
    addParameter(p);
    return p;
}

DrumKitModule::DrumKitModule()
    : ModuleBase("Drum Kit", 0, kNumChannels, StereoAudio::Declared) {
    // Tune is in semitones around the drum's own pitch; decay is the time the drum takes to fall 60 dB. These are the
    // group parameters: each one writes its value to its group's pads (see addGroupLink).
    using P = PadParam;
    const auto group = [this](const char* id, const char* name, float lo, float hi, float def, P field, Group g,
                              int hats = 0) { addGroupLink(addFloat(id, name, lo, hi, def), field, g, hats); };
    group("kickLevel", "Kick level", 0.0f, 1.0f, 0.9f, P::Level, Group::Kick);
    group("kickTune", "Kick tune", -12.0f, 12.0f, 0.0f, P::Tune, Group::Kick);
    group("kickDecay", "Kick decay", 0.1f, 1.2f, 0.35f, P::Decay, Group::Kick);
    group("snareLevel", "Snare level", 0.0f, 1.0f, 0.8f, P::Level, Group::Snare);
    group("snareTune", "Snare tune", -12.0f, 12.0f, 0.0f, P::Tune, Group::Snare);
    group("snareDecay", "Snare decay", 0.05f, 0.6f, 0.18f, P::Decay, Group::Snare);
    group("clapLevel", "Clap level", 0.0f, 1.0f, 0.8f, P::Level, Group::Clap);
    group("clapTune", "Clap tune", -12.0f, 12.0f, 0.0f, P::Tune, Group::Clap);
    group("clapDecay", "Clap decay", 0.05f, 0.8f, 0.2f, P::Decay, Group::Clap);
    group("hatLevel", "Hat level", 0.0f, 1.0f, 0.6f, P::Level, Group::Hat);
    group("hatTune", "Hat tune", -12.0f, 12.0f, 0.0f, P::Tune, Group::Hat);
    group("closedDecay", "Closed decay", 0.02f, 0.3f, 0.05f, P::Decay, Group::Hat, 1);
    group("openDecay", "Open decay", 0.1f, 1.5f, 0.45f, P::Decay, Group::Hat, 2);
    group("tomLevel", "Tom level", 0.0f, 1.0f, 0.8f, P::Level, Group::Tom);
    group("tomTune", "Tom tune", -12.0f, 12.0f, 0.0f, P::Tune, Group::Tom);
    group("tomDecay", "Tom decay", 0.1f, 1.0f, 0.3f, P::Decay, Group::Tom);
    group("cymbalLevel", "Cymbal level", 0.0f, 1.0f, 0.5f, P::Level, Group::Cymbal);
    group("cymbalTune", "Cymbal tune", -12.0f, 12.0f, 0.0f, P::Tune, Group::Cymbal);
    group("cymbalDecay", "Cymbal decay", 0.3f, 4.0f, 1.5f, P::Decay, Group::Cymbal);
    group("cowbellLevel", "Cowbell level", 0.0f, 1.0f, 0.8f, P::Level, Group::Cowbell);
    group("cowbellTune", "Cowbell tune", -12.0f, 12.0f, 0.0f, P::Tune, Group::Cowbell);
    group("cowbellDecay", "Cowbell decay", 0.1f, 0.6f, 0.25f, P::Decay, Group::Cowbell);
    levelParam = addFloat("level", "Level", 0.0f, 1.0f, 0.7f);
    addMuteParameter();
    addRecordedParameters(); // after the mute switch, so every older parameter keeps its index
    addPadParameters();      // after the group parameters, so a restore sets a group before its saved pads
    pads_ = synth::drums::defaultPadTable();
    publishPads();
    enableVisualBuffer(true);
}

DrumKitModule::~DrumKitModule() {
    for (int i = 0; i < numLinks_; ++i)
        links_[(size_t)i].param->removeListener(this);
    for (auto* r : recorded_)
        r->removeListener(this);
}

void DrumKitModule::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    smoothedLevel_.reset(sampleRate_, 0.01);
    smoothedLevel_.setCurrentAndTargetValue(levelParam->get());
    recordedPool_.prepare(sampleRate_);
    stopAll();
}

void DrumKitModule::stopAll() noexcept {
    releaseAllInstrumentPads(0);
    for (auto& voice : voices_)
        voice.stop();
    recordedPool_.stopAll();
}

// Sums the synthesized pad voices in the old kit's voice order (the shared noise source is read in that order).
float DrumKitModule::synthSample() noexcept {
    float sum = 0.0f;
    if (heldPads_ == nullptr)
        return sum;
    for (const auto index : heldPads_->tickOrder) {
        auto& voice = voices_[(size_t)index];
        if (voice.synthActive())
            sum += voice.tickSynth(sampleRate_, noise_);
    }
    return sum;
}

// Several drums at full velocity can sum past full scale: round only the excess above 0.9.
float DrumKitModule::limited(float sum) noexcept {
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
        releaseKit();
        return;
    }
    if (isMuted()) {
        buffer.clear();
        stopAll();
        releaseKit();
        return;
    }

    smoothedLevel_.setTargetValue(levelParam->get());
    acquireKit();
    acquirePads();
    pollGroupLinks();
    eventOffset_ = 0;
    auditioning_ = true;
    if (const auto audition = auditionMask_.exchange(0, std::memory_order_acq_rel); audition != 0)
        for (int i = 0; i < synth::drums::kMaxPads; ++i)
            if ((audition >> i) & 1u)
                triggerPad(i, 0.8f);
    auditioning_ = false;
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    int position = 0;
    const auto renderTo = [&](int end) {
        for (; position < end; position += kRenderChunk)
            renderChunk(left + position, right != nullptr ? right + position : nullptr,
                        juce::jmin(kRenderChunk, end - position));
        position = end;
    };
    for (const auto metadata : midiMessages) {
        const auto message = metadata.getMessage();
        const int at = juce::jlimit(position, numSamples, metadata.samplePosition);
        // A drum plays out its own decay; only an instrument pad's note ends on a Note-Off.
        if (message.isNoteOff())
            releaseInstrumentNote(message.getNoteNumber(), at);
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            releaseAllInstrumentPads(at);
        if (!message.isNoteOn())
            continue;
        renderTo(at);
        eventOffset_ = at;
        triggerNote(message.getNoteNumber(), message.getFloatVelocity());
    }
    renderTo(numSamples);
    advanceAuditionReleases(numSamples);

    if (auto* vb = getVisualBuffer())
        vb->pushBlock(left, numSamples);
}
