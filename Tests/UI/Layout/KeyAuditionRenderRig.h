#pragma once
// The render rig the key-audition render tests share: a started fake device whose block() renders one block through the
// engine's real device callback, and the thresholds that say "heard" and "silent".
#include "../../FakeAudioIODevice.h"
#include "AudioEngine/AudioEngine.h"
#include "Music/SongKey.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace keyaudition_test {

using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

constexpr int kBlock = synth::test::kFakeDeviceBlockSize;

// A started fake device: block() renders one block through the engine's real device callback.
struct Render {
    synth::test::FakeAudioIODevice fake{0, 2};
    AudioEngine& engine;
    explicit Render(AudioEngine& e)
        : engine(e) {
        engine.audioDeviceAboutToStart(&fake);
    }
    ~Render() { engine.audioDeviceStopped(); }
    float block() {
        std::vector<float> left((size_t)kBlock), right((size_t)kBlock);
        float* outs[] = {left.data(), right.data()};
        engine.audioDeviceIOCallbackWithContext(nullptr, 0, outs, 2, kBlock, {});
        float peak = 0.0f;
        for (int i = 0; i < kBlock; ++i)
            peak = std::max({peak, std::abs(left[(size_t)i]), std::abs(right[(size_t)i])});
        return peak;
    }
    /** The loudest of `blocks` blocks. */
    float peak(int blocks) {
        float p = 0.0f;
        for (int i = 0; i < blocks; ++i)
            p = std::max(p, block());
        return p;
    }
    /** Blocks rendered until a block is below `floor` (a bounded loop, not a wait); -1 if it never gets there. */
    int blocksUntilQuiet(float floor, int maxBlocks) {
        for (int i = 0; i < maxBlocks; ++i)
            if (block() < floor)
                return i;
        return -1;
    }
};

inline constexpr float kQuiet = 1.0e-4f;
inline const synth::MusicalScale kMajor = synth::makeScale(0, synth::scalePresetIndex("Major"));

} // namespace keyaudition_test
