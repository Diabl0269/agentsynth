// SoundPreviewDecoder.cpp -- the background thread that turns a file into a finished buffer at the output rate. The
// audio thread never sees a file, a reader or a resampler, only the buffer this posts.
#include "SoundBrowser/SoundPreviewPlayer.h"
#include "SoundBrowser/SoundPreviewShared.h"
#include <cmath>

namespace synth::soundbrowser {

namespace {
constexpr int kReadChunk = 65536;
constexpr int kMaxHeldClips = 8;
} // namespace

SoundPreviewPlayer::Decoder::Decoder(SoundPreviewPlayer& owner)
    : juce::Thread("Sound preview decoder")
    , owner_(owner) {
    formats_.registerBasicFormats();
    startThread(juce::Thread::Priority::normal);
}

SoundPreviewPlayer::Decoder::~Decoder() {
    signalThreadShouldExit();
    generation_.fetch_add(1);
    wake_.signal();
    stopThread(5000);
}

void SoundPreviewPlayer::Decoder::request(const juce::File& file, bool alignToBar, std::uint64_t requestId) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        pending_ = Request{file, alignToBar, requestId};
        generation_.fetch_add(1);
    }
    wake_.signal();
}

void SoundPreviewPlayer::Decoder::cancel() {
    const std::lock_guard<std::mutex> lock(mutex_);
    pending_.reset();
    generation_.fetch_add(1);
}

bool SoundPreviewPlayer::Decoder::waitIdle(int timeoutMs) const {
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32)timeoutMs;
    for (;;) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (!pending_.has_value() && !busy_.load())
                return true;
        }
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;
        juce::Thread::sleep(2);
    }
}

void SoundPreviewPlayer::Decoder::run() {
    while (!threadShouldExit()) {
        wake_.wait(-1);
        if (threadShouldExit())
            return;
        std::optional<Request> request;
        std::uint64_t generation = 0;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            request = pending_;
            pending_.reset();
            generation = generation_.load();
            busy_.store(request.has_value());
        }
        if (!request.has_value())
            continue;
        auto clip = decode(*request, generation);
        if (clip != nullptr && !superseded(generation) && owner_.heldClips() < kMaxHeldClips) {
            Command command;
            command.kind = Command::Kind::Play;
            command.requestId = request->id;
            command.alignToBar = request->alignToBar;
            owner_.post(command, clip);
        }
        busy_.store(false);
        const std::lock_guard<std::mutex> lock(mutex_);
        if (pending_.has_value())
            wake_.signal();
    }
}

// Reads at most kMaxSeconds, mono or the first two channels, and converts to the output rate with Lagrange
// interpolation; gives up as soon as a newer request arrives.
std::shared_ptr<SoundPreviewPlayer::Clip> SoundPreviewPlayer::Decoder::decode(const Request& request,
                                                                              std::uint64_t generation) {
    const double outRate = owner_.sampleRate_.load(std::memory_order_relaxed);
    if (outRate <= 0.0)
        return nullptr;
    std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(request.file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return nullptr;

    const int channels = (int)juce::jmin<unsigned int>(2, reader->numChannels);
    const auto length =
        (int)juce::jmin<juce::int64>(reader->lengthInSamples, (juce::int64)(kMaxSeconds * reader->sampleRate));
    juce::AudioBuffer<float> source(channels, length + 8); // a few zero samples past the end for the interpolator
    source.clear();
    for (int done = 0; done < length;) {
        if (superseded(generation) || threadShouldExit())
            return nullptr;
        const int n = juce::jmin(kReadChunk, length - done);
        if (!reader->read(&source, done, n, done, true, channels > 1))
            return nullptr;
        done += n;
    }

    auto clip = std::make_shared<Clip>();
    clip->sampleRate = outRate;
    if (std::abs(reader->sampleRate - outRate) < 0.5) {
        clip->data.setSize(channels, length);
        for (int ch = 0; ch < channels; ++ch)
            clip->data.copyFrom(ch, 0, source, ch, 0, length);
        return clip;
    }
    const double ratio = reader->sampleRate / outRate;
    const int outLength = juce::jmax(1, (int)std::floor((double)length / ratio));
    clip->data.setSize(channels, outLength);
    for (int ch = 0; ch < channels; ++ch) {
        juce::LagrangeInterpolator interpolator;
        interpolator.process(ratio, source.getReadPointer(ch), clip->data.getWritePointer(ch), outLength);
    }
    return clip;
}

} // namespace synth::soundbrowser
