// Monophonic pitch tracking (YIN) for a sung or hummed take: mono audio in, one frame of pitch, clarity and level per
// 10 ms out. Pure DSP: no JUCE types, no document types, no allocation per frame. The stateful PitchTracker takes
// audio in blocks of any size and gives the same frames as one call, so a live monitor can stream into it later.
#pragma once

#include <cstddef>
#include <vector>

namespace synth::melody {

struct PitchFrame {
    double timeSec; // centre of the analysis window, in seconds from the first sample
    float midi;     // fractional MIDI pitch; meaningless when !voiced
    float clarity;  // 0..1, how periodic the window is (1 - the normalised difference at the chosen period)
    float rmsDb;    // window level in dBFS
    bool voiced;
};

struct PitchTrackerSettings {
    double analysisRate = 16000.0; // audio is resampled to this (never up) before tracking
    int window = 1024;             // analysis window in samples at the analysis rate
    int hop = 160;                 // samples between frames (10 ms at 16 kHz)
    double minHz = 60.0;
    double maxHz = 1000.0;
    float threshold = 0.15f;       // absolute threshold on the normalised difference
    float minClarity = 0.75f;      // a voiced frame is at least this periodic
    float relativeGateDb = 40.0f;  // and within this many dB of the loudest frame
    float absoluteGateDb = -60.0f; // and louder than this
};

class PitchTracker {
public:
    explicit PitchTracker(const PitchTrackerSettings& settings = {});

    /** Starts a new take at `sampleRate`; forgets all audio and the loudest level seen. */
    void reset(double sampleRate);
    /** Appends the frames `numSamples` more mono samples complete to `out`. Voicing uses the loudest frame so far, so
     *  it is decided the same way however the audio is split into blocks. */
    void push(const float* mono, int numSamples, std::vector<PitchFrame>& out);

    /** Re-applies the relative level gate against the loudest frame of the whole take. */
    static void applyPeakGate(std::vector<PitchFrame>& frames, const PitchTrackerSettings& settings);

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process(double x) noexcept;
    };

    void pushAnalysed(float sample, std::vector<PitchFrame>& out);
    void analyseFrame(const float* window, std::vector<PitchFrame>& out);

    PitchTrackerSettings settings_;
    double sampleRate_ = 44100.0;
    double rate_ = 16000.0; // the rate actually analysed
    double ratio_ = 1.0;    // input samples per analysed sample
    bool resampling_ = false;
    Biquad lowpass_[2];
    double inputIndex_ = 0.0;
    double nextPosition_ = 0.0;
    double previous_ = 0.0;
    std::vector<float> buffer_;
    std::size_t bufferStart_ = 0;
    long long frameCount_ = 0;
    int minLag_ = 0;
    int maxLag_ = 0;
    float peakDb_ = -200.0f;
    std::vector<float> normalised_;
};

/** Tracks a whole take: the frames of `numSamples` mono samples at `sampleRate`, voiced against the loudest frame. */
std::vector<PitchFrame> trackPitch(const float* mono, int numSamples, double sampleRate,
                                   const PitchTrackerSettings& settings = {});

} // namespace synth::melody
