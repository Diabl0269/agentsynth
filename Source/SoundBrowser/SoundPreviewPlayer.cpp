// SoundPreviewPlayer.cpp -- the audio-thread half: a small FIFO of commands in, a handful of voices that read finished
// buffers, a gain ramp and the state the UI reads. The decoder thread is SoundPreviewDecoder.cpp.
#include "SoundBrowser/SoundPreviewPlayer.h"
#include "SoundBrowser/SoundPreviewShared.h"
#include <algorithm>
#include <cmath>

namespace synth::soundbrowser {

SoundPreviewPlayer::SoundPreviewPlayer()
    : commands_(new Command[kFifoSize])
    , voices_(new Voice[kMaxVoices]) {
    decoder_ = std::make_unique<Decoder>(*this);
}

SoundPreviewPlayer::~SoundPreviewPlayer() { decoder_.reset(); }

void SoundPreviewPlayer::setGainDb(float db) noexcept {
    gainDb_.store(juce::jlimit(kMinGainDb, kMaxGainDb, db), std::memory_order_relaxed);
}

void SoundPreviewPlayer::setSuspended(bool suspended) noexcept {
    suspended_.store(suspended, std::memory_order_release);
}

std::uint64_t SoundPreviewPlayer::play(const juce::File& file, bool alignToBar) {
    return play(file, alignToBar, LoopTarget{});
}

std::uint64_t SoundPreviewPlayer::play(const juce::File& file, bool alignToBar, const LoopTarget& target) {
    const auto id = ++requestCounter_;
    post({Command::Kind::Stop, nullptr, id, false, 0});
    decoder_->request(file, alignToBar, id, target);
    return id;
}

void SoundPreviewPlayer::stop() {
    decoder_->cancel();
    post({Command::Kind::Stop, nullptr, 0, false, 0});
}

void SoundPreviewPlayer::prepare(double sampleRate, int) {
    sampleRate_.store(sampleRate, std::memory_order_relaxed);
    renderRate_ = sampleRate;
    fadeOutSamples_ = juce::jmax(1, juce::roundToInt(0.005 * sampleRate));
    fadeInSamples_ = juce::jmax(1, juce::roundToInt(0.001 * sampleRate));
    for (int i = 0; i < kMaxVoices; ++i)
        voices_[i] = {};
    gainNow_ = -1.0f;
    publishState();
}

bool SoundPreviewPlayer::waitForDecodeForTests(int timeoutMs) const { return decoder_->waitIdle(timeoutMs); }

int SoundPreviewPlayer::heldClips() const {
    const std::lock_guard<std::mutex> lock(producerMutex_);
    return (int)clips_.size();
}

int SoundPreviewPlayer::heldClipCountForTests() const { return heldClips(); }

// Message or decoder thread. A Play command carries a clip, which gets the next sequence number under the same lock
// that orders the FIFO, so the audio thread meets clips in sequence order.
void SoundPreviewPlayer::post(Command command, const std::shared_ptr<Clip>& owner) {
    const std::lock_guard<std::mutex> lock(producerMutex_);
    const auto floor = liveFloor_.load(std::memory_order_acquire);
    clips_.erase(std::remove_if(clips_.begin(), clips_.end(), [floor](const auto& c) { return c->sequence < floor; }),
                 clips_.end());
    int s1, n1, s2, n2;
    fifo_.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 + n2 < 1)
        return; // a full FIFO drops the command; a clip it carried is released with the next post
    if (command.kind == Command::Kind::Play && owner != nullptr) {
        command.sequence = nextSequence_++;
        owner->sequence = command.sequence;
        command.clip = owner.get();
        clips_.push_back(owner);
    }
    commands_[n1 > 0 ? s1 : s2] = command;
    fifo_.finishedWrite(1);
}

int SoundPreviewPlayer::samplesUntilNextBar(const synth::BlockTimeInfo& info, int numSamples) noexcept {
    const double beatsPerBar = (double)info.timeSigNumerator * 4.0 / (double)juce::jmax(1, info.timeSigDenominator);
    const double bps = info.beatsPerSample();
    if (!(beatsPerBar > 0.0) || !(bps > 0.0))
        return 0;
    const double nextBar = std::ceil(info.startPpq / beatsPerBar - 1.0e-9) * beatsPerBar;
    const double samples = juce::jmax(0.0, (nextBar - info.startPpq) / bps);
    const auto offset = (std::int64_t)std::llround(samples);
    if (info.loopWrapSample >= 0 && offset >= info.loopWrapSample)
        return numSamples; // the loop wraps first: the bar is decided again in the next block
    return (int)juce::jmin<std::int64_t>(offset, std::int64_t(1) << 30);
}

void SoundPreviewPlayer::fadeOutAll() noexcept {
    for (int i = 0; i < kMaxVoices; ++i) {
        auto& v = voices_[i];
        if (v.state == Voice::State::Pending) {
            v = {};
        } else if (v.state == Voice::State::Sounding) {
            v.state = Voice::State::Fading;
            v.fadeLeft = fadeOutSamples_;
        }
    }
}

void SoundPreviewPlayer::startVoice(const Command& command, const synth::BlockTimeInfo& info) noexcept {
    if (command.clip == nullptr || std::abs(command.clip->sampleRate - renderRate_) > 0.5)
        return;
    int slot = -1;
    for (int i = 0; i < kMaxVoices && slot < 0; ++i)
        if (voices_[i].state == Voice::State::Free)
            slot = i;
    if (slot < 0) { // every voice is busy fading: reuse the one nearest its end
        slot = 0;
        for (int i = 1; i < kMaxVoices; ++i)
            if (voices_[i].fadeLeft < voices_[slot].fadeLeft)
                slot = i;
    }
    Voice v;
    v.clip = command.clip;
    v.sequence = command.sequence;
    v.requestId = command.requestId;
    v.state = command.alignToBar && info.playing ? Voice::State::Pending : Voice::State::Sounding;
    voices_[slot] = v;
}

void SoundPreviewPlayer::drainCommands(const synth::BlockTimeInfo& info) noexcept {
    int s1, n1, s2, n2;
    fifo_.prepareToRead(fifo_.getNumReady(), s1, n1, s2, n2);
    const auto handle = [&](int start, int count) {
        for (int i = start; i < start + count; ++i) {
            const auto& c = commands_[i];
            fadeOutAll();
            if (c.kind == Command::Kind::Play) {
                startVoice(c, info);
                lastConsumed_ = juce::jmax(lastConsumed_, c.sequence);
            }
        }
    };
    handle(s1, n1);
    handle(s2, n2);
    fifo_.finishedRead(n1 + n2);
}

void SoundPreviewPlayer::resolvePending(int numSamples, const synth::BlockTimeInfo& info) noexcept {
    for (int i = 0; i < kMaxVoices; ++i) {
        auto& v = voices_[i];
        if (v.state != Voice::State::Pending)
            continue;
        if (!info.playing) {
            v.state = Voice::State::Sounding;
            v.startOffset = 0;
            continue;
        }
        const int offset = samplesUntilNextBar(info, numSamples);
        if (offset < numSamples) {
            v.state = Voice::State::Sounding;
            v.startOffset = offset;
        }
    }
}

void SoundPreviewPlayer::mixVoices(juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept {
    const float db = gainDb_.load(std::memory_order_relaxed);
    const float target = db <= kMinGainDb ? 0.0f : std::pow(10.0f, db / 20.0f);
    if (gainNow_ < 0.0f)
        gainNow_ = target;
    const float step = (target - gainNow_) / (float)numSamples;
    const int outChannels = juce::jmin(numChannels, buffer.getNumChannels());
    float* out0 = outChannels > 0 ? buffer.getWritePointer(0) : nullptr;
    float* out1 = outChannels > 1 ? buffer.getWritePointer(1) : nullptr;
    const float fadeInInv = 1.0f / (float)fadeInSamples_;
    const float fadeOutInv = 1.0f / (float)fadeOutSamples_;

    for (int vi = 0; vi < kMaxVoices; ++vi) {
        auto& v = voices_[vi];
        if (v.state != Voice::State::Sounding && v.state != Voice::State::Fading)
            continue;
        const int total = v.clip->data.getNumSamples();
        const float* l = v.clip->data.getReadPointer(0);
        const float* r = v.clip->data.getNumChannels() > 1 ? v.clip->data.getReadPointer(1) : l;
        const int first = v.startOffset;
        v.startOffset = 0;
        for (int i = first; i < numSamples; ++i) {
            if (v.pos >= total) {
                v = {};
                break;
            }
            float env = 1.0f;
            if (v.fadeInDone < fadeInSamples_)
                env *= (float)++v.fadeInDone * fadeInInv;
            bool lastFadeSample = false;
            if (v.state == Voice::State::Fading) {
                env *= (float)v.fadeLeft * fadeOutInv;
                lastFadeSample = --v.fadeLeft <= 0;
            }
            const float g = (gainNow_ + step * (float)i) * env;
            if (out1 != nullptr) {
                out0[i] += l[v.pos] * g;
                out1[i] += r[v.pos] * g;
            } else if (out0 != nullptr) {
                out0[i] += 0.5f * (l[v.pos] + r[v.pos]) * g;
            }
            ++v.pos;
            if (lastFadeSample) {
                v = {};
                break;
            }
        }
    }
    gainNow_ = target;
}

void SoundPreviewPlayer::publishState() noexcept {
    bool playing = false;
    std::uint64_t id = 0;
    float fraction = 0.0f;
    std::uint64_t floor = lastConsumed_ + 1;
    for (int i = 0; i < kMaxVoices; ++i) {
        const auto& v = voices_[i];
        if (v.state == Voice::State::Free)
            continue;
        floor = juce::jmin(floor, v.sequence);
        if (v.state == Voice::State::Fading)
            continue;
        playing = true;
        id = v.requestId;
        const int total = v.clip->data.getNumSamples();
        fraction = total > 0 ? (float)v.pos / (float)total : 0.0f;
    }
    playing_.store(playing, std::memory_order_relaxed);
    playingId_.store(id, std::memory_order_relaxed);
    playhead_.store(fraction, std::memory_order_relaxed);
    liveFloor_.store(floor, std::memory_order_release);
}

void SoundPreviewPlayer::process(juce::AudioBuffer<float>& buffer, int numChannels,
                                 const synth::BlockTimeInfo& info) noexcept {
    const int numSamples = buffer.getNumSamples();
    drainCommands(info);
    if (suspended_.load(std::memory_order_acquire)) {
        for (int i = 0; i < kMaxVoices; ++i)
            voices_[i] = {};
    } else if (numSamples > 0 && numChannels > 0 && renderRate_ > 0.0) {
        resolvePending(numSamples, info);
        mixVoices(buffer, numChannels, numSamples);
    }
    publishState();
}

} // namespace synth::soundbrowser
