#include "SoundBrowser/SoundKeyDetect.h"
#include "SoundBrowser/SoundNameGuess.h"
#include <cmath>
#include <juce_dsp/juce_dsp.h>

namespace synth::soundbrowser {

namespace {

constexpr int kFftOrder = 14; // 16384 points: about 2.7 Hz per bin at 44.1 kHz, finer than a semitone from 80 Hz up
constexpr int kFftSize = 1 << kFftOrder;
constexpr int kHop = kFftSize / 2;
constexpr double kLowHz = 80.0;
constexpr double kHighHz = 5000.0;
// Below this correlation with the best key profile the audio has no clear tonal centre (drums, noise, a held cluster,
// a lone vocal line): on a real loop library, chord loops scored 0.8 and up, and the two vocals that scored 0.6 had the
// wrong key, so a guess under 0.7 is left out rather than shown.
constexpr double kMinCorrelation = 0.7;
// A chroma this flat (spread over its mean) is noise-like, whatever it correlates with.
constexpr double kMinSpread = 0.15;
constexpr float kSilence = 1.0e-4f; // about -80 dBFS RMS over the analysed audio

// Krumhansl and Kessler (1982), tonic first.
constexpr std::array<double, 12> kMajor{6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr std::array<double, 12> kMinor{6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

double correlation(const Chroma& chroma, const std::array<double, 12>& profile, int tonic) {
    double meanC = 0.0, meanP = 0.0;
    for (int i = 0; i < 12; ++i) {
        meanC += chroma[(size_t)i];
        meanP += profile[(size_t)i];
    }
    meanC /= 12.0;
    meanP /= 12.0;
    double num = 0.0, denC = 0.0, denP = 0.0;
    for (int i = 0; i < 12; ++i) {
        const double c = chroma[(size_t)((tonic + i) % 12)] - meanC;
        const double p = profile[(size_t)i] - meanP;
        num += c * p;
        denC += c * c;
        denP += p * p;
    }
    return denC > 0.0 && denP > 0.0 ? num / std::sqrt(denC * denP) : 0.0;
}

} // namespace

Chroma chromaOf(const float* samples, int numSamples, double sampleRate) {
    Chroma chroma{};
    if (samples == nullptr || numSamples <= 0 || sampleRate <= 0.0)
        return chroma;
    double energy = 0.0;
    for (int i = 0; i < numSamples; ++i)
        energy += (double)samples[i] * samples[i];
    if (std::sqrt(energy / numSamples) < kSilence)
        return chroma;

    // Each FFT bin in the musical range adds its magnitude to the pitch class it is nearest to.
    std::vector<int> pitchClassOfBin((size_t)kFftSize / 2, 0);
    const int firstBin = juce::jmax(1, (int)std::ceil(kLowHz * kFftSize / sampleRate));
    const int lastBin = juce::jmin(kFftSize / 2 - 1, (int)std::floor(kHighHz * kFftSize / sampleRate));
    for (int bin = firstBin; bin <= lastBin; ++bin) {
        const double hz = bin * sampleRate / kFftSize;
        const int midi = (int)std::lround(69.0 + 12.0 * std::log2(hz / 440.0));
        pitchClassOfBin[(size_t)bin] = ((midi % 12) + 12) % 12;
    }

    juce::dsp::FFT fft(kFftOrder);
    juce::dsp::WindowingFunction<float> window((size_t)kFftSize, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<float> frame((size_t)kFftSize * 2);
    // A loop shorter than one frame is analysed once, zero padded.
    for (int start = 0; start == 0 || start + kFftSize <= numSamples; start += kHop) {
        std::fill(frame.begin(), frame.end(), 0.0f);
        const int count = juce::jmin(kFftSize, numSamples - start);
        std::copy(samples + start, samples + start + count, frame.begin());
        window.multiplyWithWindowingTable(frame.data(), (size_t)kFftSize);
        fft.performFrequencyOnlyForwardTransform(frame.data(), true);
        for (int bin = firstBin; bin <= lastBin; ++bin)
            chroma[(size_t)pitchClassOfBin[(size_t)bin]] += (double)frame[(size_t)bin];
    }
    return chroma;
}

juce::String keyFromChroma(const Chroma& chroma) {
    double mean = 0.0;
    for (const double c : chroma)
        mean += c;
    mean /= 12.0;
    if (mean <= 0.0)
        return {};
    double variance = 0.0;
    for (const double c : chroma)
        variance += (c - mean) * (c - mean);
    if (std::sqrt(variance / 12.0) / mean < kMinSpread)
        return {};

    double best = -1.0;
    juce::String key;
    for (int tonic = 0; tonic < 12; ++tonic) {
        for (const bool minor : {false, true}) {
            const double r = correlation(chroma, minor ? kMinor : kMajor, tonic);
            if (r > best) {
                best = r;
                key = noteNameOf(tonic) + (minor ? " minor" : " major");
            }
        }
    }
    return best >= kMinCorrelation ? key : juce::String();
}

juce::String detectKey(juce::AudioFormatReader& reader, double maxSeconds) {
    if (reader.sampleRate <= 0.0 || reader.lengthInSamples <= 0 || reader.numChannels == 0)
        return {};
    const int length = (int)juce::jmin(reader.lengthInSamples, (juce::int64)(maxSeconds * reader.sampleRate));
    const int channels = (int)juce::jmin(2u, reader.numChannels);
    juce::AudioBuffer<float> buffer(channels, length);
    if (!reader.read(&buffer, 0, length, 0, true, channels > 1))
        return {};
    if (channels > 1) {
        buffer.addFrom(0, 0, buffer, 1, 0, length);
        buffer.applyGain(0, 0, length, 0.5f);
    }
    return keyFromChroma(chromaOf(buffer.getReadPointer(0), length, reader.sampleRate));
}

} // namespace synth::soundbrowser
