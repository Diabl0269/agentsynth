#pragma once

#include "ModuleBase.h"
#include <atomic>
#include <juce_dsp/juce_dsp.h>

class FilterModule : public ModuleBase {
public:
    FilterModule()
        : ModuleBase("Filter", 11, 8) { // 0-7: per-voice audio, 8-10: shared CV, outputs 0-7: filtered audio
        addParameter(cutoffParam = new juce::AudioParameterFloat("cutoff", "Cutoff", 20.0f, 20000.0f, 440.0f));
        addParameter(resonanceParam = new juce::AudioParameterFloat("resonance", "Resonance", 0.0f, 1.0f, 0.1f));
        addParameter(driveParam = new juce::AudioParameterFloat("drive", "Drive", 1.0f, 10.0f, 1.0f));
        addParameter(filterTypeParam = new juce::AudioParameterChoice(
                         "filterType", "Filter Type",
                         juce::StringArray{"LPF24", "LPF12", "HPF24", "HPF12", "BPF24", "BPF12", "Notch"}, 0));
        addParameter(polyParam = new juce::AudioParameterBool("poly", "Poly", false));
        addMuteParameter();
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        lastSampleRate = sampleRate;
        juce::dsp::ProcessSpec monoSpec = {sampleRate, static_cast<juce::uint32>(samplesPerBlock), 1};
        for (int v = 0; v < MAX_VOICES; ++v) {
            ladders[v].prepare(monoSpec);
            ladders[v].setEnabled(true);
            svfsForNotch[v].prepare(monoSpec);
        }
        applyFilterType(filterTypeParam->getIndex());
        smoothedCutoff.reset(sampleRate, 0.005);
        smoothedCutoff.setCurrentAndTargetValue(*cutoffParam);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midiMessages*/) override {
        if (isBypassed())
            return;

        if (isMuted()) {
            buffer.clear();
            return;
        }

        smoothedCutoff.setTargetValue(*cutoffParam);
        applyFilterType(filterTypeParam->getIndex());
        float baseRes = *resonanceParam;
        float baseDrive = *driveParam;

        modulatedCutoff.store(*cutoffParam, std::memory_order_relaxed);
        modulatedResonance.store(baseRes, std::memory_order_relaxed);

        int numChannels = buffer.getNumChannels();
        int numSamples = buffer.getNumSamples();
        if (numChannels == 0 || numSamples == 0)
            return;

        if (!polyParam->get()) {
            processMonoMode(buffer, numSamples, numChannels, baseRes, baseDrive);
        } else {
            processPolyMode(buffer, numSamples, numChannels, baseRes, baseDrive);
        }

        // Push voice 0 to visual buffer
        if (auto* vb = getVisualBuffer()) {
            auto* ch = buffer.getReadPointer(0);
            for (int i = 0; i < numSamples; ++i)
                vb->pushSample(ch[i]);
        }

        // Clear CV channels to prevent leaking to downstream modules
        int cvStartChannel = polyParam->get() ? 8 : 1;
        for (int ch = cvStartChannel; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    std::vector<ModulationTarget> getModulationTargets() const override {
        if (polyParam->get())
            return {{"Cutoff", 8}, {"Resonance", 9}, {"Drive", 10}};
        return {{"Cutoff", 1}, {"Resonance", 2}, {"Drive", 3}};
    }
    juce::String getInputPortLabel(int i) const override {
        const juce::String labels[] = {"Audio", "Cutoff", "Resonance", "Drive"};
        return (i >= 0 && i < 4) ? labels[i] : ModuleBase::getInputPortLabel(i);
    }
    juce::String getOutputPortLabel(int) const override { return "Audio"; }
    int getVisibleInputPortCount() const override { return 4; }
    int getVisibleOutputPortCount() const override { return 1; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Filter; }
    ModuleType getModuleType() const override { return ModuleType::Filter; }

    float getCurrentCutoff() const { return modulatedCutoff.load(std::memory_order_relaxed); }
    float getCurrentResonance() const { return *resonanceParam; }
    float getModulatedResonance() const { return modulatedResonance.load(std::memory_order_relaxed); }
    float getCurrentDrive() const { return *driveParam; }
    int getCurrentFilterType() const { return filterTypeParam->getIndex(); }
    double getLastSampleRate() const { return lastSampleRate; }

private:
    static constexpr int MAX_VOICES = 8;

    void processMonoMode(juce::AudioBuffer<float>& buffer, int numSamples, int numChannels, float baseRes,
                         float baseDrive) {
        const float* cvCutoffCh = (numChannels > 1) ? buffer.getReadPointer(1) : nullptr;
        const float* cvResCh = (numChannels > 2) ? buffer.getReadPointer(2) : nullptr;
        const float* cvDriveCh = (numChannels > 3) ? buffer.getReadPointer(3) : nullptr;

        juce::dsp::AudioBlock<float> block(buffer);
        auto singleChannelBlock = block.getSingleChannelBlock(0);
        auto* audioData = buffer.getWritePointer(0);

        bool cutoffCVActive = false;
        bool resCVActive = false;
        if (cvCutoffCh) {
            float rms = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                rms += cvCutoffCh[i] * cvCutoffCh[i];
            cutoffCVActive = (rms / numSamples) > 1e-6f;
        }
        if (cvResCh) {
            float rms = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                rms += cvResCh[i] * cvResCh[i];
            resCVActive = (rms / numSamples) > 1e-6f;
        }

        for (int i = 0; i < numSamples; ++i) {
            float baseCutoff = smoothedCutoff.getNextValue();
            float totalCutoffMod = cvCutoffCh ? cvCutoffCh[i] : 0.0f;
            totalCutoffMod = juce::jlimit(-1.0f, 1.0f, totalCutoffMod);

            float f = baseCutoff;
            if (totalCutoffMod > 0.0f)
                f = baseCutoff * std::pow(20000.0f / baseCutoff, totalCutoffMod);
            else if (totalCutoffMod < 0.0f)
                f = baseCutoff * std::pow(20.0f / baseCutoff, -totalCutoffMod);
            f = juce::jlimit(20.0f, 20000.0f, f);
            if (cutoffCVActive)
                modulatedCutoff.store(f, std::memory_order_relaxed);
            ladders[0].setCutoffFrequencyHz(f);

            float totalResMod = cvResCh ? cvResCh[i] : 0.0f;
            totalResMod = juce::jlimit(-1.0f, 1.0f, totalResMod);
            float res = juce::jlimit(0.0f, 1.0f, baseRes + totalResMod);
            if (resCVActive)
                modulatedResonance.store(res, std::memory_order_relaxed);
            ladders[0].setResonance(res);

            float totalDriveMod = cvDriveCh ? cvDriveCh[i] : 0.0f;
            totalDriveMod = juce::jlimit(-1.0f, 1.0f, totalDriveMod);
            float drive = juce::jlimit(1.0f, 10.0f, baseDrive + (totalDriveMod * 9.0f));
            ladders[0].setDrive(drive);

            if (isNotchMode) {
                svfsForNotch[0].setCutoffFrequency(f);
                svfsForNotch[0].setResonance(0.707f + res * 15.0f);
                svfsForNotch[0].setType(juce::dsp::StateVariableTPTFilterType::bandpass);
                float input = audioData[i];
                float filtered = svfsForNotch[0].processSample(0, input);
                audioData[i] = input - filtered;
            } else {
                auto sampleBlock = singleChannelBlock.getSubBlock(i, 1);
                juce::dsp::ProcessContextReplacing<float> context(sampleBlock);
                ladders[0].process(context);
            }
        }
    }

    void processPolyMode(juce::AudioBuffer<float>& buffer, int numSamples, int numChannels, float baseRes,
                         float baseDrive) {
        // Read shared CV from channels 8-10 into pre-allocated cache
        int ns = std::min(numSamples, 4096);
        std::fill_n(cutoffCVCache.data(), ns, 0.0f);
        std::fill_n(resCVCache.data(), ns, 0.0f);
        std::fill_n(driveCVCache.data(), ns, 0.0f);
        if (numChannels > 8)
            std::copy_n(buffer.getReadPointer(8), ns, cutoffCVCache.data());
        if (numChannels > 9)
            std::copy_n(buffer.getReadPointer(9), ns, resCVCache.data());
        if (numChannels > 10)
            std::copy_n(buffer.getReadPointer(10), ns, driveCVCache.data());

        int voiceCount = std::min(MAX_VOICES, numChannels);

        // Compute shared CV modulation once (use mid-block sample)
        int midSample = ns / 2;
        float cvCut = juce::jlimit(-1.0f, 1.0f, cutoffCVCache[midSample]);
        float cvRes = juce::jlimit(-1.0f, 1.0f, resCVCache[midSample]);
        float cvDrv = juce::jlimit(-1.0f, 1.0f, driveCVCache[midSample]);

        float baseCutoff = *cutoffParam;
        float f = baseCutoff;
        if (cvCut > 0.0f)
            f = baseCutoff * std::pow(20000.0f / baseCutoff, cvCut);
        else if (cvCut < 0.0f)
            f = baseCutoff * std::pow(20.0f / baseCutoff, -cvCut);
        f = juce::jlimit(20.0f, 20000.0f, f);
        float res = juce::jlimit(0.0f, 1.0f, baseRes + cvRes);
        float drive = juce::jlimit(1.0f, 10.0f, baseDrive + (cvDrv * 9.0f));

        modulatedCutoff.store(f, std::memory_order_relaxed);
        modulatedResonance.store(res, std::memory_order_relaxed);
        modulatedDrive.store(drive, std::memory_order_relaxed);

        // Process only active voices (skip silent channels to save CPU)
        juce::dsp::AudioBlock<float> fullBlock(buffer);
        for (int v = 0; v < voiceCount; ++v) {
            // Skip silent voices
            if (buffer.getRMSLevel(v, 0, numSamples) < 1e-6f)
                continue;

            ladders[v].setCutoffFrequencyHz(f);
            ladders[v].setResonance(res);
            ladders[v].setDrive(drive);

            if (isNotchMode) {
                svfsForNotch[v].setCutoffFrequency(f);
                svfsForNotch[v].setResonance(0.707f + res * 15.0f);
                svfsForNotch[v].setType(juce::dsp::StateVariableTPTFilterType::bandpass);
                float* audioData = buffer.getWritePointer(v);
                for (int i = 0; i < numSamples; ++i) {
                    float input = audioData[i];
                    float filtered = svfsForNotch[v].processSample(0, input);
                    audioData[i] = input - filtered;
                }
            } else {
                auto voiceBlock = fullBlock.getSingleChannelBlock((size_t)v);
                juce::dsp::ProcessContextReplacing<float> context(voiceBlock);
                ladders[v].process(context);
            }
        }
    }

    void applyFilterType(int typeIndex) {
        if (typeIndex >= 0 && typeIndex <= 5) {
            isNotchMode = false;
            juce::dsp::LadderFilterMode modes[] = {
                juce::dsp::LadderFilterMode::LPF24, juce::dsp::LadderFilterMode::LPF12,
                juce::dsp::LadderFilterMode::HPF24, juce::dsp::LadderFilterMode::HPF12,
                juce::dsp::LadderFilterMode::BPF24, juce::dsp::LadderFilterMode::BPF12};
            for (int v = 0; v < MAX_VOICES; ++v)
                ladders[v].setMode(modes[typeIndex]);
        } else if (typeIndex == 6) {
            isNotchMode = true;
        }
    }

    // Pre-allocated CV caches to avoid heap allocation in audio thread
    std::array<float, 4096> cutoffCVCache{};
    std::array<float, 4096> resCVCache{};
    std::array<float, 4096> driveCVCache{};

    juce::dsp::LadderFilter<float> ladders[MAX_VOICES];
    juce::dsp::StateVariableTPTFilter<float> svfsForNotch[MAX_VOICES];
    bool isNotchMode = false;
    double lastSampleRate = 44100.0;
    juce::SmoothedValue<float> smoothedCutoff;
    juce::AudioParameterFloat* cutoffParam = nullptr;
    juce::AudioParameterFloat* resonanceParam = nullptr;
    juce::AudioParameterFloat* driveParam = nullptr;
    juce::AudioParameterChoice* filterTypeParam = nullptr;
    juce::AudioParameterBool* polyParam = nullptr;
    std::atomic<float> modulatedCutoff{440.0f};
    std::atomic<float> modulatedResonance{0.1f};
    std::atomic<float> modulatedDrive{1.0f};
};
