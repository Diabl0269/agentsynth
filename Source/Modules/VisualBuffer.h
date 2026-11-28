#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <juce_core/juce_core.h>

/**
 * A simple thread-safe circular buffer for visualization.
 * Audio thread pushes samples, GUI thread reads them for rendering.
 */
class VisualBuffer {
public:
    static constexpr int DEFAULT_SIZE = 1024;

    VisualBuffer(int size = DEFAULT_SIZE)
        : bufferSize(size)
        , buffer(size) {
        writePos.store(0);
    }

    /** Pushes a single sample into the circular buffer. Audio thread only (the single writer). A
        module pushing a whole block calls pushBlock / pushConstant instead. */
    void pushSample(float sample) {
        int pos = writePos.load();
        buffer[pos].store(sample, std::memory_order_relaxed);
        writePos.store((pos + 1) % bufferSize);
    }

    /** Pushes `numSamples` samples, oldest first, leaving the ring exactly as that many pushSample
        calls would. Audio thread only (the single writer). */
    void pushBlock(const float* samples, int numSamples) {
        int pos = beginBlock(samples, numSamples);
        for (int i = 0; i < numSamples; ++i) {
            buffer[(size_t)pos].store(samples[i], std::memory_order_relaxed);
            if (++pos == bufferSize)
                pos = 0;
        }
        writePos.store(pos, std::memory_order_release);
    }

    /** Pushes the mid, (left + right) / 2, of two legs: a stereo module's scope shows both of them, so a signal held
        on one leg still draws. Same ring result as pushBlock of that mid. Audio thread only (the single writer). */
    void pushStereoBlock(const float* left, const float* right, int numSamples) {
        const int total = numSamples;
        int pos = beginBlock(left, numSamples);
        right += total - numSamples;
        for (int i = 0; i < numSamples; ++i) {
            buffer[(size_t)pos].store((left[i] + right[i]) * 0.5f, std::memory_order_relaxed);
            if (++pos == bufferSize)
                pos = 0;
        }
        writePos.store(pos, std::memory_order_release);
    }

    /** Feeds a stereo module's scope from its output block: the mid of the left leg (channel 0) and `rightChannel` when
        that is a channel of the block (pass 0 for a joined jack, whose legs are the same signal), else the left leg
        alone. Audio thread only (the single writer). */
    void pushLegs(const float* const* channels, int numChannels, int rightChannel, int numSamples) {
        if (numChannels <= 0)
            return;
        if (rightChannel > 0 && rightChannel < numChannels)
            pushStereoBlock(channels[0], channels[rightChannel], numSamples);
        else
            pushBlock(channels[0], numSamples);
    }

    /** The largest magnitude held in the ring (0 when it would draw nothing). Message thread; reads without a lock. */
    float peakMagnitude() const {
        float peak = 0.0f;
        for (const auto& s : buffer)
            peak = std::max(peak, std::abs(s.load(std::memory_order_relaxed)));
        return peak;
    }

    /** Pushes `value` `numSamples` times: an activity LED's block. Same result as pushBlock. */
    void pushConstant(float value, int numSamples) {
        const float* unused = nullptr;
        int pos = beginBlock(unused, numSamples);
        for (int i = 0; i < numSamples; ++i) {
            buffer[(size_t)pos].store(value, std::memory_order_relaxed);
            if (++pos == bufferSize)
                pos = 0;
        }
        writePos.store(pos, std::memory_order_release);
    }

    /** Copies the current buffer state into a destination buffer for rendering.
     */
    void copyTo(std::vector<float>& dest) const {
        int pos = writePos.load();
        int size = std::min((int)dest.size(), bufferSize);

        for (int i = 0; i < size; ++i) {
            // Read starting from current write position (oldest sample)
            int readIdx = (pos + i) % bufferSize;
            dest[i] = buffer[readIdx].load(std::memory_order_relaxed);
        }
    }

    /** The largest absolute sample in the ring, read in place (no copy, no allocation). */
    float peakAbs() const {
        float peak = 0.0f;
        for (const auto& sample : buffer)
            peak = std::max(peak, std::abs(sample.load(std::memory_order_relaxed)));
        return peak;
    }

    int getSize() const { return bufferSize; }

    /** Message thread: a live view (the scope) read the ring just now; call on each of its repaint ticks. */
    void markWatched() noexcept { watchedAtMs.store(juce::Time::getMillisecondCounter(), std::memory_order_relaxed); }
    /** Any thread: a live view read the ring in the last `withinMs`, so a module must keep pushing every block. */
    bool watchedRecently(juce::uint32 withinMs = 250) const noexcept {
        const auto at = watchedAtMs.load(std::memory_order_relaxed);
        return at != 0 && juce::Time::getMillisecondCounter() - at < withinMs;
    }

private:
    // The write position a block starts at. The audio thread is the only writer, so it reads its own
    // position relaxed and publishes it once per block (release) rather than once per sample: a
    // per-sample seq_cst store/load pair costs every module with a scope more than its own DSP. A
    // block longer than the ring skips the samples the ring would overwrite anyway.
    template <typename Ptr>
    int beginBlock(Ptr& samples, int& numSamples) const {
        int pos = writePos.load(std::memory_order_relaxed);
        if (numSamples > bufferSize) {
            const int skipped = numSamples - bufferSize;
            pos = (pos + skipped) % bufferSize;
            if (samples != nullptr)
                samples += skipped;
            numSamples = bufferSize;
        }
        return pos;
    }

    int bufferSize;
    std::vector<std::atomic<float>> buffer;
    std::atomic<int> writePos;
    std::atomic<juce::uint32> watchedAtMs{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualBuffer)
};
