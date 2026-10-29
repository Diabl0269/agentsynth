#pragma once

#include "Filter/CustomFilterEngine.h"
#include "FrequencyText.h"
#include "LadderCoefficientSetter.h"
#include "ModuleBase.h"
#include "ParameterText.h"
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
    // Audio R is a dedicated block at kRightBase, again in AND out: this is a processor, so
    // the right leg needs an input jack as well as an output. R deliberately does NOT live on ch1
    // the way an FX Dual I/O pair does — ch1 is the Cutoff CV input, and moving it would break
    // every saved patch that modulates cutoff.
    // -------------------------------------------------------------------------
    static constexpr int kNumVoices = 8;
    static constexpr int kPolyCVBase = kNumVoices;                // shared CV block in poly mode
    static constexpr int kNumCVInputs = 3;                        // Cutoff, Resonance, Drive
    static constexpr int kNumInputs = kPolyCVBase + kNumCVInputs; // 11
    static constexpr int kRightBase = kNumInputs;                 // Audio R block starts here
    // Key Track and its Pitch input are appended above Audio R so no saved channel moves: Pitch is a
    // per-voice block (one channel in mono, eight in poly) and Key Track's CV is one shared channel,
    // the same raw channels in both voice modes.
    static constexpr int kPitchBase = kRightBase + kNumVoices;         // 19-26: per-voice pitch in Hz
    static constexpr int kKeyTrackCVChannel = kPitchBase + kNumVoices; // 27
    // The Custom type's CV (Morph X/Y, Shift, Depth) is appended above Key Track, one shared channel
    // each in both voice modes like Key Track: the mono CV block (ch1-3) has the poly voice fan
    // directly above it and the poly block (ch8-10) has Audio R, so neither can grow without
    // renumbering saved channels.
    static constexpr int kMorphXCVChannel = kKeyTrackCVChannel + 1; // 28
    static constexpr int kMorphYCVChannel = kMorphXCVChannel + 1;   // 29
    static constexpr int kShiftCVChannel = kMorphYCVChannel + 1;    // 30
    static constexpr int kDepthCVChannel = kShiftCVChannel + 1;     // 31
    static constexpr int kNumChannels = kDepthCVChannel + 1;        // 32, in and out
    static constexpr float kKeyTrackRefHz = 261.63f;                // middle C: no shift at any Key Track
    static constexpr int kLegCount = 2;                             // 0 = Audio L, 1 = Audio R
    static constexpr int kNumVisibleInputs = 2 + kNumCVInputs;      // Audio L, Audio R, then CV
    static constexpr int kExtraJacks = 6;                           // Pitch, Key Track, Morph X, Morph Y, Shift, Depth
    static constexpr int kCustomTypeIndex = 7;                      // "Custom", appended last
    static constexpr int kLegacyFilterTypeCount = 7;                // choices before Custom existed

    /** Raw channel carrying CV jack `cv` (0 = Cutoff, 1 = Resonance, 2 = Drive). Mono packs the CV
        block directly above the single audio channel (1-3); poly puts it above the voice fan (8-10). */
    static constexpr int cvChannelFor(int cv, bool poly) { return poly ? (kPolyCVBase + cv) : (1 + cv); }

    /** Raw head channel of audio leg `leg` (0 = L, 1 = R). */
    static constexpr int legBaseChannel(int leg) { return leg == 0 ? 0 : kRightBase; }

    FilterModule()
        // 32 in / 32 out: Audio L on 0-7, shared CV on 8-10, Audio R on 11-18, Pitch on 19-26, Key
        // Track CV on 27 and Morph X/Y, Shift, Depth CV on 28-31. Declaring the
        // outputs above every CV input channel also makes JUCE hand this node private copies of
        // shared CV buffers, so the end-of-block CV clear can only ever zero our own copy.
        // StereoAudio::Declared: 32 outputs, so the Auto shape test cannot see this module's stereo
        // pair — its right leg is the kRightBase block, and it ships SPLIT.
        : ModuleBase("Filter", kNumChannels, kNumChannels, StereoAudio::Declared) {
        addParameter(cutoffParam = new juce::AudioParameterFloat("cutoff", "Cutoff",
                                                                 juce::NormalisableRange<float>(20.0f, 20000.0f, 0.01f),
                                                                 440.0f, synth::frequencyAttributes()));
        addParameter(resonanceParam = new juce::AudioParameterFloat("resonance", "Resonance",
                                                                    juce::NormalisableRange<float>(0.0f, 1.0f), 0.1f,
                                                                    synth::fractionPercentAttributes()));
        addParameter(driveParam = new juce::AudioParameterFloat("drive", "Drive", 1.0f, 10.0f, 1.0f));
        addParameter(filterTypeParam = new juce::AudioParameterChoice(
                         "filterType", "Filter Type",
                         juce::StringArray{"LPF24", "LPF12", "HPF24", "HPF12", "BPF24", "BPF12", "Notch", "Custom"},
                         0));
        addParameter(polyParam = new juce::AudioParameterBool("poly", "Poly", false));
        // Key Track: how far the cutoff follows the Pitch input, in percent of one octave per octave
        // around middle C. 0 (default) leaves the cutoff alone, as does an unplugged Pitch jack.
        addParameter(keyTrackParam = new juce::AudioParameterFloat("keyTrack", "Key Track",
                                                                   juce::NormalisableRange<float>(0.0f, 100.0f), 0.0f,
                                                                   synth::percentAttributes()));
        // Custom type only (after Key Track; Level and Mute stay last): the morph cursor over the drawn
        // shapes, a pitch shift of the whole response, and a multiplier on its dB.
        addParameter(morphXParam = new juce::AudioParameterFloat("morphX", "Morph X", 0.0f, 1.0f, 0.5f));
        addParameter(morphYParam = new juce::AudioParameterFloat("morphY", "Morph Y", 0.0f, 1.0f, 0.5f));
        addParameter(shiftParam = new juce::AudioParameterFloat("shift", "Shift",
                                                                juce::NormalisableRange<float>(-48.0f, 48.0f, 0.01f),
                                                                0.0f, synth::semitonesAttributes()));
        addParameter(depthParam =
                         new juce::AudioParameterFloat("depth", "Depth", juce::NormalisableRange<float>(0.0f, 2.0f),
                                                       1.0f, synth::multiplierPercentAttributes()));
        // Dual I/O comes from the ctor's StereoAudio::Declared above (defaults to split: this module
        // filters in stereo). Collapsed, its jack layout is the plain mono one — Audio,
        // Cutoff, Resonance, Drive.
        addOutputLevelParameter();
        addMuteParameter();
        addMixParameter(); // shared output stage: added after Mute so no existing index moves
        addPanParameter();
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
                ladderSetters[leg][v] = {};
                ladders[leg][v].setEnabled(true);
                svfsForNotch[leg][v].prepare(monoSpec);
            }
        }
        applyFilterType(filterTypeParam->getIndex());
        smoothedCutoff.reset(sampleRate, 0.005);
        smoothedCutoff.setCurrentAndTargetValue(*cutoffParam);
        // Resonance and Drive both scale the ladder's feedback/saturation, so an automated step
        // lands straight on the output level. 5 ms matches Cutoff — fast enough to feel immediate
        // on a knob, long enough that a per-block automation write cannot click.
        smoothedResonance.reset(sampleRate, 0.005);
        smoothedResonance.setCurrentAndTargetValue(*resonanceParam);
        smoothedDrive.reset(sampleRate, 0.005);
        smoothedDrive.setCurrentAndTargetValue(*driveParam);
        smoothedKeyTrack.reset(sampleRate, 0.005);
        smoothedKeyTrack.setCurrentAndTargetValue(keyTrackParam->get() * 0.01f);
        prepareOutputLevel(sampleRate);
        customEngine.prepare(sampleRate);
        customWasActive = false;
    }

    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midiMessages*/) override {
        // Borrow each voice's Left into its matching Right (kRightBase + voice), sample-
        // exact, while Dual I/O is split and only Left is patched. Before the bypass/mute branches
        // below so both see a filled Right leg exactly as if the user had cabled it.
        applyLeftRightNormalling(buffer, polyParam->get() ? MAX_VOICES : 1);

        if (isBypassed())
            return;

        if (isMuted()) {
            buffer.clear();
            return;
        }

        smoothedCutoff.setTargetValue(*cutoffParam);
        smoothedResonance.setTargetValue(*resonanceParam);
        smoothedDrive.setTargetValue(*driveParam);
        smoothedKeyTrack.setTargetValue(
            0.01f * modulateNormalised(*keyTrackParam, keyTrackParam->get(), blockCV(buffer, kKeyTrackCVChannel)));
        applyFilterType(filterTypeParam->getIndex());

        modulatedCutoff.store(*cutoffParam, std::memory_order_relaxed);
        modulatedResonance.store(*resonanceParam, std::memory_order_relaxed);

        int numChannels = buffer.getNumChannels();
        int numSamples = buffer.getNumSamples();
        if (numChannels == 0 || numSamples == 0)
            return;

        if (filterTypeParam->getIndex() == kCustomTypeIndex) {
            processCustomMode(buffer, numSamples);
        } else {
            customWasActive = false;
            if (!polyParam->get())
                processMonoMode(buffer, numSamples, numChannels);
            else
                processPolyMode(buffer, numSamples, numChannels);
        }

        // Audio lives on ch0 (mono) / ch0-7 (poly) plus the matching Audio R block at kRightBase;
        // the CV inputs between them are cleared below and must not be scaled. One shared ramp for
        // both legs — see applyOutputLevelSplit.
        applyOutputLevelSplit(buffer, polyParam->get() ? MAX_VOICES : 1, kRightBase);

        // Push voice 0 to visual buffer
        if (auto* vb = getVisualBuffer()) {
            vb->pushBlock(buffer.getReadPointer(0), numSamples);
        }

        // Clear CV channels to prevent leaking to downstream modules. Bounded at kRightBase: the
        // Audio R block sits above the CV inputs, so running to getNumChannels() would erase the
        // right leg we just filtered.
        int cvStartChannel = cvChannelFor(0, polyParam->get());
        for (int ch = cvStartChannel; ch < kRightBase && ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);
        // The Pitch block, Key Track and the Custom CV above Audio R are inputs only: clear them too.
        for (int ch = kPitchBase; ch < kNumChannels && ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    /** The Custom type's four targets are knob-only: no gutter row, a cable lands on the knob's ring. */
    std::vector<ModulationTarget> getModulationTargets() const override {
        if (polyParam->get())
            return {{"Cutoff", 8, "cutoff"},
                    {"Resonance", 9, "resonance"},
                    {"Drive", 10, "drive"},
                    {"Key Track", kKeyTrackCVChannel, "keyTrack"},
                    {"Morph X", kMorphXCVChannel, "morphX", true},
                    {"Morph Y", kMorphYCVChannel, "morphY", true},
                    {"Shift", kShiftCVChannel, "shift", true},
                    {"Depth", kDepthCVChannel, "depth", true}};
        return {{"Cutoff", 1, "cutoff"},
                {"Resonance", 2, "resonance"},
                {"Drive", 3, "drive"},
                {"Key Track", kKeyTrackCVChannel, "keyTrack"},
                {"Morph X", kMorphXCVChannel, "morphX", true},
                {"Morph Y", kMorphYCVChannel, "morphY", true},
                {"Shift", kShiftCVChannel, "shift", true},
                {"Depth", kDepthCVChannel, "depth", true}};
    }
    /** Audio R sits next to Audio L rather than after Drive, so the two legs read as a pair. Visible
        jack order is presentation only — connections persist by raw channel index, so the CV jacks
        shifting as the toggle flips never touches a saved patch. */
    juce::String getInputPortLabel(int i) const override {
        const int audioJacks = splitAudioJackCount();
        if (i >= 0 && i < audioJacks)
            return splitAudioLabel(i);
        const juce::String cvLabels[] = {"Cutoff",  "Resonance", "Drive", "Pitch", "Key Track",
                                         "Morph X", "Morph Y",   "Shift", "Depth"};
        const int cv = i - audioJacks;
        return (cv >= 0 && cv < kNumCVInputs + kExtraJacks) ? cvLabels[cv] : ModuleBase::getInputPortLabel(i);
    }
    juce::String getOutputPortLabel(int i) const override { return splitAudioLabel(i); }
    int getVisibleInputPortCount() const override { return splitAudioJackCount() + kNumCVInputs + kExtraJacks; }
    int getVisibleOutputPortCount() const override { return splitAudioJackCount(); }
    int rightAudioLegChannel() const override { return kRightBase; }
    // Audio L/R are read in place at ch0(-7)/kRightBase(+) above -- a genuine stereo
    // pair, eligible for render-time L->R normalling.
    bool hasStereoAudioInputPair() const override { return true; }
    // Pure audio/CV processor — processBlock's midiMessages parameter is unused.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

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
                p.visibleJackIndex = splitAudioJackCount() + cv; // after the audio jack(s)
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
        }

        // Pitch (per voice in poly, ch19 alone in mono) and Key Track CV, after the three CV jacks.
        const int pitchJack = splitAudioJackCount() + kNumCVInputs;
        const int pitchSpan = poly ? kNumVoices : 1;
        if (raw >= kPitchBase && raw < kPitchBase + pitchSpan) {
            LogicalPort p;
            p.visibleJackIndex = pitchJack;
            p.role = PortRole::Pitch;
            p.isPolyGroupHead = (raw == kPitchBase);
            p.polyVoiceSpan = (raw == kPitchBase) ? pitchSpan : 1;
            return p;
        }
        if (raw >= kKeyTrackCVChannel && raw <= kDepthCVChannel) { // Key Track, Morph X/Y, Shift, Depth
            LogicalPort p;
            p.visibleJackIndex = pitchJack + 1 + (raw - kKeyTrackCVChannel);
            p.role = PortRole::ModCV;
            p.isPolyGroupHead = true;
            p.polyVoiceSpan = 1;
            return p;
        }

        // Unclaimed channels (the poly CV block while in mono, and vice versa) are addressable but
        // never a jack head. Deliberately not ModuleBase's default: it reports isPolyGroupHead for
        // any raw channel below the VISIBLE jack count, so growing to 5 jacks would make raw ch4 a
        // phantom second head on the Drive jack and getJackTargets would hand out two wires.
        LogicalPort p;
        p.visibleJackIndex = 0;
        p.role = PortRole::Other;
        p.isPolyGroupHead = false;
        p.polyVoiceSpan = 1;
        return p;
    }

    LogicalPort mapOutputChannel(int raw) const override {
        if (auto audio = mapAudioLeg(raw))
            return *audio;

        // Silent pass-through channels (the CV block, the Pitch block, and voices 1-7 in mono): addressable but never
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

        const int legs = splitAudioJackCount(); // collapsed: the right block is not exposed
        for (int leg = 0; leg < legs; ++leg) {
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
    /** The Custom controls as set on their knobs (no CV): morph cursor, shift in semitones, depth. */
    float getCurrentMorphX() const { return morphXParam->get(); }
    float getCurrentMorphY() const { return morphYParam->get(); }
    float getCurrentShift() const { return shiftParam->get(); }
    float getCurrentDepth() const { return depthParam->get(); }
    double getLastSampleRate() const { return lastSampleRate; }

    /** Message thread only: the Custom type's drawn shapes. */
    const synth::CustomFilterShapes& getCustomShapes() const { return customEngine.getShapes(); }
    void setCustomShapes(const synth::CustomFilterShapes& shapes) { customEngine.setShapes(shapes); }
    /** Changes whenever the shape list is replaced, whoever replaced it. */
    int getCustomShapesGeneration() const { return customEngine.getShapesGeneration(); }
    /** Custom kernels designed so far (tests and diagnostics). */
    int getCustomDesignCount() const { return customEngine.getDesignCount(); }
    /** Message thread only: whether (and how) each poly note gets its own place on the morph map. Setting it
        bumps getCustomShapesGeneration(). */
    const synth::CustomFilterPerNote& getCustomPerNote() const { return customEngine.getPerNote(); }
    void setCustomPerNote(const synth::CustomFilterPerNote& perNote) { customEngine.setPerNote(perNote); }
    /** Audio thread / tests: the per-voice cursors of the per-note modes. */
    const synth::CustomFilterVoiceCursors& getCustomVoiceCursors() const { return customEngine.getVoiceCursors(); }

    /** The Custom shapes plus "perNote" (when it is not the default), or nothing while both are the defaults. Model
        output uses setUntrustedExtraState. */
    juce::var getExtraState() const override {
        const auto& shapes = customEngine.getShapes();
        const auto& perNote = customEngine.getPerNote();
        if (shapes.isDefault() && perNote.isDefault())
            return {};
        juce::var state = shapes.toVar();
        if (!perNote.isDefault())
            if (auto* obj = state.getDynamicObject())
                obj->setProperty("perNote", perNote.toVar());
        return state;
    }
    void setExtraState(const juce::var& state) override {
        customEngine.setShapes(synth::CustomFilterShapes::fromVar(state));
        customEngine.setPerNote(synth::CustomFilterPerNote::fromVar(state.getProperty("perNote", {})));
    }
    /** The shape list is pure bounded data (curve points, map coordinates, a short whitelisted name), so
     *  model-authored patches may set it too; anything unusable leaves the current shapes alone. Of "perNote"
     *  only the mode and the smoothness are read, never the key map. */
    void setUntrustedExtraState(const juce::var& state) override {
        synth::CustomFilterShapes parsed;
        if (synth::CustomFilterShapes::fromUntrustedVar(state, parsed))
            customEngine.setShapes(parsed);
        auto perNote = customEngine.getPerNote();
        if (synth::CustomFilterPerNote::fromUntrustedVar(state.getProperty("perNote", {}), perNote))
            customEngine.setPerNote(perNote);
    }
    /** Host-state blobs written before "Custom" was appended decode over the old choice count. */
    int legacyChoiceCount(const juce::String& paramId) const override {
        return paramId == "filterType" ? kLegacyFilterTypeCount : ModuleBase::legacyChoiceCount(paramId);
    }

private:
    static constexpr int MAX_VOICES = 8;

    void processMonoMode(juce::AudioBuffer<float>& buffer, int numSamples, int numChannels) {
        const float* cvCutoffCh = (numChannels > 1) ? buffer.getReadPointer(1) : nullptr;
        const float* cvResCh = (numChannels > 2) ? buffer.getReadPointer(2) : nullptr;
        const float* cvDriveCh = (numChannels > 3) ? buffer.getReadPointer(3) : nullptr;
        const float* pitchCh = (numChannels > kPitchBase) ? buffer.getReadPointer(kPitchBase) : nullptr;

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
            const float baseRes = smoothedResonance.getNextValue();
            const float baseDrive = smoothedDrive.getNextValue();
            float totalCutoffMod = cvCutoffCh ? cvCutoffCh[i] : 0.0f;
            totalCutoffMod = juce::jlimit(-1.0f, 1.0f, totalCutoffMod);

            float f = baseCutoff;
            if (totalCutoffMod > 0.0f)
                f = baseCutoff * std::pow(20000.0f / baseCutoff, totalCutoffMod);
            else if (totalCutoffMod < 0.0f)
                f = baseCutoff * std::pow(20.0f / baseCutoff, -totalCutoffMod);
            f = juce::jlimit(20.0f, 20000.0f, f);
            // Key Track: after the cutoff CV, per sample (the smoother is advanced even while the
            // Pitch jack is empty so it never lags a later patch-in).
            const float keyTracked = keyTrackedCutoff(f, smoothedKeyTrack.getNextValue(), pitchCh ? pitchCh[i] : 0.0f);
            if (cutoffCVActive || keyTracked != f)
                modulatedCutoff.store(keyTracked, std::memory_order_relaxed);
            f = keyTracked;
            ladderSetters[0][0].setCutoff(ladders[0][0], f);

            float totalResMod = cvResCh ? cvResCh[i] : 0.0f;
            totalResMod = juce::jlimit(-1.0f, 1.0f, totalResMod);
            float res = juce::jlimit(0.0f, 1.0f, baseRes + totalResMod);
            if (resCVActive)
                modulatedResonance.store(res, std::memory_order_relaxed);
            ladderSetters[0][0].setResonance(ladders[0][0], res);

            float totalDriveMod = cvDriveCh ? cvDriveCh[i] : 0.0f;
            totalDriveMod = juce::jlimit(-1.0f, 1.0f, totalDriveMod);
            float drive = juce::jlimit(1.0f, 10.0f, baseDrive + (totalDriveMod * 9.0f));
            ladderSetters[0][0].setDrive(ladders[0][0], drive);

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
        // patched into Audio R, so a mono insert pays for one leg only.
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

            ladderSetters[1][0].setCutoff(ladders[1][0], f);
            ladderSetters[1][0].setResonance(ladders[1][0], res);
            ladderSetters[1][0].setDrive(ladders[1][0], driveCoeffCache[idx]);

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

    void processPolyMode(juce::AudioBuffer<float>& buffer, int numSamples, int numChannels) {
        // Poly mode already resolves cutoff/resonance/drive once per block (one ladder per voice,
        // coefficients set per block), so the smoothers advance a whole block at a time here
        // instead of per sample. They still turn an automation step into a ramp across blocks.
        const float baseCutoffPoly = smoothedCutoff.getCurrentValue();
        const float baseRes = smoothedResonance.getCurrentValue();
        const float baseDrive = smoothedDrive.getCurrentValue();
        const float keyTrack = smoothedKeyTrack.getCurrentValue();
        smoothedKeyTrack.skip(numSamples);
        smoothedCutoff.skip(numSamples);
        smoothedResonance.skip(numSamples);
        smoothedDrive.skip(numSamples);

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

        float baseCutoff = baseCutoffPoly;
        float f = baseCutoff;
        if (cvCut > 0.0f)
            f = baseCutoff * std::pow(20000.0f / baseCutoff, cvCut);
        else if (cvCut < 0.0f)
            f = baseCutoff * std::pow(20.0f / baseCutoff, -cvCut);
        f = juce::jlimit(20.0f, 20000.0f, f);
        float res = juce::jlimit(0.0f, 1.0f, baseRes + cvRes);
        float drive = juce::jlimit(1.0f, 10.0f, baseDrive + (cvDrv * 9.0f));

        modulatedCutoff.store(keyTrackedCutoff(f, keyTrack, blockEndPitch(buffer, 0)), std::memory_order_relaxed);
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

                // Key Track moves each voice's cutoff by its own pitch (the value its Pitch channel
                // ends the block on); both legs of a voice share it.
                const float voiceF = keyTrackedCutoff(f, keyTrack, blockEndPitch(buffer, v));
                ladderSetters[leg][v].setCutoff(ladders[leg][v], voiceF);
                ladderSetters[leg][v].setResonance(ladders[leg][v], res);
                ladderSetters[leg][v].setDrive(ladders[leg][v], drive);

                if (isNotchMode) {
                    svfsForNotch[leg][v].setCutoffFrequency(voiceF);
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

    /** Custom type: every audio channel of both legs through one shared kernel, or with a per-note mode on, one
        kernel per voice (both legs of a voice share it; mono is voice 0, so it takes exactly poly voice 0's path).
        Morph X/Y, Shift and Depth (and their CV) are read once per block; Key Track adds a shift by the Pitch input
        in mono only. Cutoff (with its CV, as the
        other types sweep it) moves the drawn response along frequency and Resonance adds a bell at the cutoff;
        both are shared by every voice. Drive is ignored. History is cleared on entering the mode or changing the voice
       count, so a stale tail from an earlier stint never plays. */
    void processCustomMode(juce::AudioBuffer<float>& buffer, int numSamples) {
        const bool poly = polyParam->get();
        if (!customWasActive || poly != customWasPoly)
            customEngine.resetState();
        customWasActive = true;
        customWasPoly = poly;

        synth::CustomFilterEngine::Controls c;
        c.morphX = modulateNormalised(*morphXParam, morphXParam->get(), blockCV(buffer, kMorphXCVChannel));
        c.morphY = modulateNormalised(*morphYParam, morphYParam->get(), blockCV(buffer, kMorphYCVChannel));
        c.shiftSemitones = modulateNormalised(*shiftParam, shiftParam->get(), blockCV(buffer, kShiftCVChannel));
        c.depth = modulateNormalised(*depthParam, depthParam->get(), blockCV(buffer, kDepthCVChannel));
        const float cutoffCV = juce::jlimit(-1.0f, 1.0f, blockCV(buffer, cvChannelFor(0, poly)));
        const float resonanceCV = blockCV(buffer, cvChannelFor(1, poly));
        c.cutoffHz = sweepCutoff(smoothedCutoff.getCurrentValue(), cutoffCV);
        c.resonance = juce::jlimit(0.0f, 1.0f, smoothedResonance.getCurrentValue() + resonanceCV);
        smoothedCutoff.skip(numSamples);
        smoothedResonance.skip(numSamples);
        modulatedCutoff.store(c.cutoffHz, std::memory_order_relaxed);
        modulatedResonance.store(c.resonance, std::memory_order_relaxed);
        const float keyTrack =
            0.01f * modulateNormalised(*keyTrackParam, keyTrackParam->get(), blockCV(buffer, kKeyTrackCVChannel));
        const float pitch = blockEndPitch(buffer, 0);
        if (!poly && keyTrack > 0.0f && pitch > 0.0f)
            c.shiftSemitones += 12.0f * keyTrack * std::log2(pitch / kKeyTrackRefHz);

        const int voices = poly ? kNumVoices : 1;
        std::array<float*, synth::CustomFilterEngine::kMaxChannels> channels{};
        for (int leg = 0; leg < kLegCount; ++leg)
            for (int v = 0; v < voices; ++v)
                if (const int ch = legBaseChannel(leg) + v; ch < buffer.getNumChannels())
                    channels[(size_t)(leg * kNumVoices + v)] = buffer.getWritePointer(ch);
        readVoiceInputs(buffer, numSamples, voices);
        customEngine.process(channels.data(), (int)channels.size(), numSamples, c, voiceInputs.data(), voices);
    }

    /** What the per-note modes need from each of the first `voices` voices before filtering: its Pitch at the
        block's first and last sample, and whether either leg carries sound (above about -80 dBFS). The Filter has no
        gate, so these are how it spots a note (CustomFilterVoiceCursors::detectNote). Mono reads voice 0 only: its
        ch1-7 are not voices. */
    void readVoiceInputs(const juce::AudioBuffer<float>& buffer, int numSamples, int voices) {
        for (int v = 0; v < voices; ++v) {
            auto& in = voiceInputs[(size_t)v];
            const int pitchCh = kPitchBase + v;
            in.pitchStartHz = pitchCh < buffer.getNumChannels() ? buffer.getReadPointer(pitchCh)[0] : 0.0f;
            in.pitchEndHz = blockEndPitch(buffer, v);
            in.audible = false;
            for (int leg = 0; leg < kLegCount && !in.audible; ++leg)
                if (const int ch = legBaseChannel(leg) + v; ch < buffer.getNumChannels())
                    in.audible = buffer.getMagnitude(ch, 0, numSamples) > kVoiceAudibleLevel;
        }
    }

    /** `base` swept by a Cutoff CV in -1..1: +1 reaches 20 kHz, -1 reaches 20 Hz, exponentially. */
    static float sweepCutoff(float base, float cv) {
        base = juce::jlimit(20.0f, 20000.0f, base);
        if (cv > 0.0f)
            return base * std::pow(20000.0f / base, cv);
        if (cv < 0.0f)
            return base * std::pow(20.0f / base, -cv);
        return base;
    }

    /** Cutoff after Key Track: `f` times (pitch / middle C) ^ keyTrack, i.e. `keyTrack` octaves of
        cutoff movement per octave of pitch around middle C, clamped to the filter's range. Returns `f`
        untouched (not even re-clamped) at Key Track 0 or with no pitch (<= 0, the empty jack). */
    static float keyTrackedCutoff(float f, float keyTrack, float pitchHz) {
        if (keyTrack <= 0.0f || pitchHz <= 0.0f)
            return f;
        return juce::jlimit(20.0f, 20000.0f, f * std::pow(pitchHz / kKeyTrackRefHz, keyTrack));
    }

    /** Voice `v`'s Pitch (Hz) at the end of the block; 0 when the graph handed us no such channel. */
    static float blockEndPitch(const juce::AudioBuffer<float>& buffer, int v) {
        const int ch = kPitchBase + v;
        return (ch < buffer.getNumChannels() && buffer.getNumSamples() > 0)
                   ? buffer.getReadPointer(ch)[buffer.getNumSamples() - 1]
                   : 0.0f;
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
    synth::LadderCoefficientSetter ladderSetters[kLegCount][MAX_VOICES];
    juce::dsp::StateVariableTPTFilter<float> svfsForNotch[kLegCount][MAX_VOICES];
    bool isNotchMode = false;
    double lastSampleRate = 44100.0;
    juce::SmoothedValue<float> smoothedCutoff;
    juce::SmoothedValue<float> smoothedResonance;
    juce::SmoothedValue<float> smoothedDrive;
    juce::SmoothedValue<float> smoothedKeyTrack; // 0..1
    juce::AudioParameterFloat* cutoffParam = nullptr;
    juce::AudioParameterFloat* resonanceParam = nullptr;
    juce::AudioParameterFloat* driveParam = nullptr;
    juce::AudioParameterChoice* filterTypeParam = nullptr;
    juce::AudioParameterBool* polyParam = nullptr;
    juce::AudioParameterFloat* keyTrackParam = nullptr;
    juce::AudioParameterFloat* morphXParam = nullptr;
    juce::AudioParameterFloat* morphYParam = nullptr;
    juce::AudioParameterFloat* shiftParam = nullptr;
    juce::AudioParameterFloat* depthParam = nullptr;
    synth::CustomFilterEngine customEngine;
    std::array<synth::CustomFilterEngine::VoiceInput, kNumVoices> voiceInputs{}; // audio thread
    static constexpr float kVoiceAudibleLevel = 1.0e-4f;
    bool customWasActive = false; // audio thread
    bool customWasPoly = false;   // audio thread
    std::atomic<float> modulatedCutoff{440.0f};
    std::atomic<float> modulatedResonance{0.1f};
    std::atomic<float> modulatedDrive{1.0f};
};
