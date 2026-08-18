#pragma once

#include "ModuleBase.h"
#include <atomic>
#include <juce_dsp/juce_dsp.h>
#include <optional>

class FilterModule : public ModuleBase {
public:
    // -------------------------------------------------------------------------
    // Channel map
    //
    // Audio L: ch0 in mono, ch0-7 in poly — both as input and as output, unchanged since the
    // module was mono. Shared CV: ch1-3 (mono) / ch8-10 (poly), also unchanged.
    //
    // Audio R (#219) is a dedicated block at kRightBase, again in AND out: this is a processor, so
    // the right leg needs an input jack as well as an output. R deliberately does NOT live on ch1
    // the way an FX Dual I/O pair does — ch1 is the Cutoff CV input, and moving it would break
    // every saved patch that modulates cutoff.
    // -------------------------------------------------------------------------
    static constexpr int kNumVoices = 8;
    static constexpr int kPolyCVBase = kNumVoices;                // shared CV block in poly mode
    static constexpr int kNumCVInputs = 3;                        // Cutoff, Resonance, Drive
    static constexpr int kNumInputs = kPolyCVBase + kNumCVInputs; // 11
    static constexpr int kRightBase = kNumInputs;                 // Audio R block starts here
    static constexpr int kNumChannels = kRightBase + kNumVoices;  // 19, in and out
    static constexpr int kLegCount = 2;                           // 0 = Audio L, 1 = Audio R
    static constexpr int kNumVisibleInputs = 2 + kNumCVInputs;    // Audio L, Audio R, then CV

    /** Raw channel carrying CV jack `cv` (0 = Cutoff, 1 = Resonance, 2 = Drive). Mono packs the CV
        block directly above the single audio channel (1-3); poly puts it above the voice fan (8-10). */
    static constexpr int cvChannelFor(int cv, bool poly) { return poly ? (kPolyCVBase + cv) : (1 + cv); }

    /** Raw head channel of audio leg `leg` (0 = L, 1 = R). */
    static constexpr int legBaseChannel(int leg) { return leg == 0 ? 0 : kRightBase; }

    FilterModule()
        // 19 in / 19 out: Audio L on 0-7, shared CV on 8-10, Audio R on 11-18. Declaring the
        // outputs above every CV input channel also makes JUCE hand this node private copies of
        // shared CV buffers, so the end-of-block CV clear can only ever zero our own copy.
        : ModuleBase("Filter", kNumChannels, kNumChannels) {
        addParameter(cutoffParam = new juce::AudioParameterFloat("cutoff", "Cutoff", 20.0f, 20000.0f, 440.0f));
        addParameter(resonanceParam = new juce::AudioParameterFloat("resonance", "Resonance", 0.0f, 1.0f, 0.1f));
        addParameter(driveParam = new juce::AudioParameterFloat("drive", "Drive", 1.0f, 10.0f, 1.0f));
        addParameter(filterTypeParam = new juce::AudioParameterChoice(
                         "filterType", "Filter Type",
                         juce::StringArray{"LPF24", "LPF12", "HPF24", "HPF12", "BPF24", "BPF12", "Notch"}, 0));
        addParameter(polyParam = new juce::AudioParameterBool("poly", "Poly", false));
        addOutputLevelParameter();
        addMuteParameter();
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        lastSampleRate = sampleRate;
        juce::dsp::ProcessSpec monoSpec = {sampleRate, static_cast<juce::uint32>(samplesPerBlock), 1};
        // Both legs are prepared up front — a module's channel count is fixed for its lifetime, so
        // there is no "stereo on" moment at which it would be safe to allocate the right-hand
        // ladders. Unused legs cost state, not CPU: a silent R input is skipped per block.
        for (int leg = 0; leg < kLegCount; ++leg) {
            for (int v = 0; v < MAX_VOICES; ++v) {
                ladders[leg][v].prepare(monoSpec);
                ladders[leg][v].setEnabled(true);
                svfsForNotch[leg][v].prepare(monoSpec);
            }
        }
        applyFilterType(filterTypeParam->getIndex());
        smoothedCutoff.reset(sampleRate, 0.005);
        smoothedCutoff.setCurrentAndTargetValue(*cutoffParam);
        prepareOutputLevel(sampleRate);
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

        // Audio lives on ch0 (mono) / ch0-7 (poly) plus the matching Audio R block at kRightBase;
        // the CV inputs between them are cleared below and must not be scaled. One shared ramp for
        // both legs — see applyOutputLevelSplit.
        applyOutputLevelSplit(buffer, polyParam->get() ? MAX_VOICES : 1, kRightBase);

        // Push voice 0 to visual buffer
        if (auto* vb = getVisualBuffer()) {
            auto* ch = buffer.getReadPointer(0);
            for (int i = 0; i < numSamples; ++i)
                vb->pushSample(ch[i]);
        }

        // Clear CV channels to prevent leaking to downstream modules. Bounded at kRightBase: the
        // Audio R block sits above the CV inputs, so running to getNumChannels() would erase the
        // right leg we just filtered.
        int cvStartChannel = cvChannelFor(0, polyParam->get());
        for (int ch = cvStartChannel; ch < kRightBase && ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    std::vector<ModulationTarget> getModulationTargets() const override {
        if (polyParam->get())
            return {{"Cutoff", 8}, {"Resonance", 9}, {"Drive", 10}};
        return {{"Cutoff", 1}, {"Resonance", 2}, {"Drive", 3}};
    }
    juce::String getInputPortLabel(int i) const override {
        // Audio R sits next to Audio L rather than after Drive, so the two legs read as a pair.
        // Visible jack order is presentation only — connections persist by raw channel index, so
        // Cutoff/Resonance/Drive moving from jacks 1-3 to 2-4 does not touch a saved patch.
        const juce::String labels[] = {"Audio L", "Audio R", "Cutoff", "Resonance", "Drive"};
        return (i >= 0 && i < kNumVisibleInputs) ? labels[i] : ModuleBase::getInputPortLabel(i);
    }
    juce::String getOutputPortLabel(int i) const override { return i == 1 ? "Audio R" : "Audio L"; }
    int getVisibleInputPortCount() const override { return kNumVisibleInputs; }
    int getVisibleOutputPortCount() const override { return 2; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Filter; }
    ModuleType getModuleType() const override { return ModuleType::Filter; }

    /** Audio L on the voice block (ch0, or ch0-7 in poly), Audio R on its own block at kRightBase,
        and the shared CV inputs between them keeping the raw channels they have always had. Both
        legs are input AND output channels — this module filters what it is handed in place. */
    LogicalPort mapInputChannel(int raw) const override {
        if (auto audio = mapAudioLeg(raw))
            return *audio;

        const bool poly = polyParam->get();
        for (int cv = 0; cv < kNumCVInputs; ++cv) {
            if (raw == cvChannelFor(cv, poly)) {
                LogicalPort p;
                p.visibleJackIndex = 2 + cv; // after Audio L / Audio R
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
        }
        return ModuleBase::mapInputChannel(raw);
    }

    LogicalPort mapOutputChannel(int raw) const override {
        if (auto audio = mapAudioLeg(raw))
            return *audio;

        // Silent pass-through channels (the CV block, and voices 1-7 in mono): addressable but never
        // a poly-bus head. Deliberately not ModuleBase's default, which clamps the raw channel onto
        // a visible jack index and so would advertise mono ch1 (Cutoff CV) as the Audio R head.
        LogicalPort p;
        p.visibleJackIndex = 0;
        p.role = PortRole::Audio;
        p.isPolyGroupHead = false;
        p.polyVoiceSpan = 1;
        return p;
    }

    /** Shared by the input and output maps: both directions use the same two audio blocks. Returns
        nothing when `raw` is not part of either leg. */
    std::optional<LogicalPort> mapAudioLeg(int raw) const {
        const bool poly = polyParam->get();
        const int span = poly ? kNumVoices : 1;

        for (int leg = 0; leg < kLegCount; ++leg) {
            const int base = legBaseChannel(leg);
            if (raw >= base && raw < base + span) {
                LogicalPort p;
                p.visibleJackIndex = leg;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = (raw == base);
                p.polyVoiceSpan = (raw == base) ? span : 1;
                return p;
            }
        }
        return std::nullopt;
    }

    bool isAutoPromotableModTarget(int dstChannel) const override {
        if (polyParam->get())
            return false;
        return ModuleBase::isAutoPromotableModTarget(dstChannel);
    }

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
            ladders[0][0].setCutoffFrequencyHz(f);

            float totalResMod = cvResCh ? cvResCh[i] : 0.0f;
            totalResMod = juce::jlimit(-1.0f, 1.0f, totalResMod);
            float res = juce::jlimit(0.0f, 1.0f, baseRes + totalResMod);
            if (resCVActive)
                modulatedResonance.store(res, std::memory_order_relaxed);
            ladders[0][0].setResonance(res);

            float totalDriveMod = cvDriveCh ? cvDriveCh[i] : 0.0f;
            totalDriveMod = juce::jlimit(-1.0f, 1.0f, totalDriveMod);
            float drive = juce::jlimit(1.0f, 10.0f, baseDrive + (totalDriveMod * 9.0f));
            ladders[0][0].setDrive(drive);

            // Stash the coefficients this sample resolved to so the right leg gets the identical
            // treatment without re-advancing smoothedCutoff — a second getNextValue() walk would
            // put R half a block ahead of L and detune the stereo image.
            if (i < kMaxBlock) {
                cutoffCoeffCache[(size_t)i] = f;
                resCoeffCache[(size_t)i] = res;
                driveCoeffCache[(size_t)i] = drive;
            }

            if (isNotchMode) {
                svfsForNotch[0][0].setCutoffFrequency(f);
                svfsForNotch[0][0].setResonance(0.707f + res * 15.0f);
                svfsForNotch[0][0].setType(juce::dsp::StateVariableTPTFilterType::bandpass);
                float input = audioData[i];
                float filtered = svfsForNotch[0][0].processSample(0, input);
                audioData[i] = input - filtered;
            } else {
                auto sampleBlock = singleChannelBlock.getSubBlock(i, 1);
                juce::dsp::ProcessContextReplacing<float> context(sampleBlock);
                ladders[0][0].process(context);
            }
        }

        // Right leg: same coefficients, its own filter state. Skipped entirely when nothing is
        // patched into Audio R, so a mono insert costs exactly what it did before #219.
        processRightLegMono(buffer, numSamples, numChannels);
    }

    /** Runs the cached per-sample coefficients over the Audio R channel with the right-leg ladder.
        No-op when the R input is silent. */
    void processRightLegMono(juce::AudioBuffer<float>& buffer, int numSamples, int numChannels) {
        if (numChannels <= kRightBase)
            return;
        if (buffer.getRMSLevel(kRightBase, 0, numSamples) < 1e-6f)
            return;

        const int cached = std::min(numSamples, kMaxBlock);
        juce::dsp::AudioBlock<float> block(buffer);
        auto rightChannelBlock = block.getSingleChannelBlock((size_t)kRightBase);
        auto* audioData = buffer.getWritePointer(kRightBase);

        for (int i = 0; i < numSamples; ++i) {
            const size_t idx = (size_t)std::min(i, cached - 1);
            const float f = cutoffCoeffCache[idx];
            const float res = resCoeffCache[idx];

            ladders[1][0].setCutoffFrequencyHz(f);
            ladders[1][0].setResonance(res);
            ladders[1][0].setDrive(driveCoeffCache[idx]);

            if (isNotchMode) {
                svfsForNotch[1][0].setCutoffFrequency(f);
                svfsForNotch[1][0].setResonance(0.707f + res * 15.0f);
                svfsForNotch[1][0].setType(juce::dsp::StateVariableTPTFilterType::bandpass);
                const float input = audioData[i];
                const float filtered = svfsForNotch[1][0].processSample(0, input);
                audioData[i] = input - filtered;
            } else {
                auto sampleBlock = rightChannelBlock.getSubBlock((size_t)i, 1);
                juce::dsp::ProcessContextReplacing<float> context(sampleBlock);
                ladders[1][0].process(context);
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

        // Process only active voices (skip silent channels to save CPU). Both legs fan eight wide:
        // the right leg is its own poly head at kRightBase, not voice 1 relabelled.
        juce::dsp::AudioBlock<float> fullBlock(buffer);
        for (int leg = 0; leg < kLegCount; ++leg) {
            const int legBase = legBaseChannel(leg);
            for (int v = 0; v < voiceCount; ++v) {
                const int ch = legBase + v;
                if (ch >= numChannels)
                    break;

                // Skip silent voices
                if (buffer.getRMSLevel(ch, 0, numSamples) < 1e-6f)
                    continue;

                ladders[leg][v].setCutoffFrequencyHz(f);
                ladders[leg][v].setResonance(res);
                ladders[leg][v].setDrive(drive);

                if (isNotchMode) {
                    svfsForNotch[leg][v].setCutoffFrequency(f);
                    svfsForNotch[leg][v].setResonance(0.707f + res * 15.0f);
                    svfsForNotch[leg][v].setType(juce::dsp::StateVariableTPTFilterType::bandpass);
                    float* audioData = buffer.getWritePointer(ch);
                    for (int i = 0; i < numSamples; ++i) {
                        float input = audioData[i];
                        float filtered = svfsForNotch[leg][v].processSample(0, input);
                        audioData[i] = input - filtered;
                    }
                } else {
                    auto voiceBlock = fullBlock.getSingleChannelBlock((size_t)ch);
                    juce::dsp::ProcessContextReplacing<float> context(voiceBlock);
                    ladders[leg][v].process(context);
                }
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
            for (int leg = 0; leg < kLegCount; ++leg)
                for (int v = 0; v < MAX_VOICES; ++v)
                    ladders[leg][v].setMode(modes[typeIndex]);
        } else if (typeIndex == 6) {
            isNotchMode = true;
        }
    }

    // Pre-allocated CV caches to avoid heap allocation in audio thread
    static constexpr int kMaxBlock = 4096;
    std::array<float, kMaxBlock> cutoffCVCache{};
    std::array<float, kMaxBlock> resCVCache{};
    std::array<float, kMaxBlock> driveCVCache{};

    // Per-sample coefficients the left leg resolved to this block, replayed on the right leg.
    std::array<float, kMaxBlock> cutoffCoeffCache{};
    std::array<float, kMaxBlock> resCoeffCache{};
    std::array<float, kMaxBlock> driveCoeffCache{};

    // One independent ladder (and notch SVF) per audio leg per voice: a stereo filter has to keep
    // L and R separate all the way through, or the image collapses at the VCF.
    juce::dsp::LadderFilter<float> ladders[kLegCount][MAX_VOICES];
    juce::dsp::StateVariableTPTFilter<float> svfsForNotch[kLegCount][MAX_VOICES];
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
