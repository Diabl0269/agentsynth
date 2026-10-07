#pragma once

#include <atomic>
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

    int getSize() const { return bufferSize; }

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualBuffer)
};
