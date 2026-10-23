#include "RecordTapModule.h"
#include "../Transport/TransportService.h"

RecordTapModule::RecordTapModule(int ringCapacityFrames)
    // StereoAudio::None — a documented Dual I/O opt-out. The 2-in/2-out shape matches
    // StereoAudio::Auto, but this is a hidden recording tap: its two channels are the take's
    // capture pair, wired by the record flow rather than patched, and it has no card the user can
    // put a jack-layout toggle on. Splitting them would only add a parameter to its saved state.
    : ModuleBase("Rec Tap", kNumChannels, kNumChannels, StereoAudio::None)
    // Everything the audio thread touches is allocated HERE, once (allocateRingNow): processBlock never sizes
    // anything, whatever the block size or the capture format turns out to be.
    , capture_(writerThread_, ringCapacityFrames, /*allocateRingNow*/ true) {
    static_assert(kNumChannels == synth::TakeCaptureWriter::kMaxChannels, "the tap captures what it carries");
    static_assert(kPeakBucketSize == synth::TakeCaptureWriter::kPeakBucketSize, "one bucket size per peaks file");
    enableVisualBuffer(true);
}

RecordTapModule::~RecordTapModule() {
    // Finalises an in-flight take rather than abandoning a half-written WAV, and detaches the writer client
    // before the thread it runs on is stopped.
    if (isCapturing())
        stopCapture();
    writerThread_.stopThread(2000);
}

// A take's WAV header is fixed at startCapture()'s rate. The graph re-prepares every node on a device/sample-rate
// change (AudioEngine::handleStreamFormatChange's callers), so latching here stops the push before the first
// new-rate block; the commit itself stays the message thread's.
void RecordTapModule::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(samplesPerBlock);
    capture_.latchHaltIfRateDiffers(sampleRate);
}

void RecordTapModule::processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ignoreUnused(midiMessages);

    const int numSamples = buffer.getNumSamples();

    // Bypass: this module HAS a dry audio path (it is a pass-through), so the bypassed branch
    // returns without touching the audio channels — see the bypass/mute contract in
    // docs/architecture/module-base.md#bypassmute-contract. There are no CV channels to clear. Capture stops for the
    // duration: a bypassed tap is not part of the signal chain, so recording what flows past it would put audio in the
    // take that the user could not hear.
    if (isBypassed())
        return;

    // Pass-through is literally nothing: the graph hands us the input in the same buffer it wants the output in.
    // The capture only READS it. The anchor is the transport's block-start sample, looked up only until the
    // first block is captured; a failed downcast (a bare unit test, a foreign host) leaves it invalid.
    if (numSamples > 0 && capture_.isCapturing()) {
        juce::int64 blockStart = 0;
        bool anchorValid = false;
        if (capture_.needsAnchor()) {
            if (auto* transport = dynamic_cast<synth::TransportService*>(getPlayHead())) {
                blockStart = transport->getCurrentBlockInfo().blockStartSample;
                anchorValid = true;
            }
        }
        capture_.push(buffer.getArrayOfReadPointers(), buffer.getNumChannels(), numSamples, blockStart, anchorValid);
    }

    // Activity LED: the block's peak, same shape as every other audio module's meter feed.
    if (auto* vb = getVisualBuffer()) {
        if (buffer.getNumChannels() > 0)
            vb->pushBlock(buffer.getReadPointer(0), numSamples);
        else
            vb->pushConstant(0.0f, numSamples);
    }
}

bool RecordTapModule::startCapture(const juce::File& wavFile, const juce::File& peaksFile, double sampleRate,
                                   int numChannels) {
    return capture_.start(wavFile, peaksFile, sampleRate, numChannels);
}

RecordTapModule::TakeResult RecordTapModule::stopCapture() { return capture_.stop(); }

std::vector<float> RecordTapModule::getPeaksForTest() const {
    std::vector<std::pair<float, float>> pairs;
    copyLivePeaks(pairs);
    std::vector<float> flat;
    flat.reserve(pairs.size() * 2);
    for (const auto& pair : pairs) {
        flat.push_back(pair.first);
        flat.push_back(pair.second);
    }
    return flat;
}
