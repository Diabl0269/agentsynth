#pragma once

#include "ModuleBase.h"
#include "ModuleFileStateKeys.h"
#include "Project/DeferredAssetLoads.h"
#include <array>
#include <bitset>
#include <cmath>
#include <juce_audio_formats/juce_audio_formats.h>

/**
 * Sample player / granular source.
 *
 * Loads an audio file from disk and plays it back one of two ways, selected by `playMode`:
 *
 *   • **Sample**   — classic one-shot / looping playback of the region from `start` to `end`.
 *                    `pitch` (± 24 semitones), `fine` (± 100 cents) and MIDI note number set the
 *                    playback rate; `loop` wraps back to `start` at `end`; `reverse` plays the
 *                    region backwards (from `end` down to `start`, looping backwards).
 *   • **Granular** — scatters short windowed grains read from around the `start` position, never
 *                    from or past `end`. `grainSize`, `density` and `spray` shape the cloud;
 *                    `reverse` plays each grain backwards.
 *
 * Channel layout (mono module — there is no poly mode):
 *
 *   | raw ch | in                | out                        |
 *   | :----- | :---------------- | :------------------------- |
 *   | 0      | Trigger / Gate    | Audio L                    |
 *   | 1      | Pitch CV          | Audio R                    |
 *   | 2      | Position CV       | silent pass-through        |
 *   | 3      | Grain Size CV     | silent pass-through        |
 *   | 4      | Density CV        | silent pass-through        |
 *   | 5      | Spray CV          | silent pass-through        |
 *   | 6      | Level CV          | silent pass-through        |
 *   | 7      | Root Note CV      | silent pass-through        |
 *   | 8      | End CV            | silent pass-through        |
 *   | 9      | Fine CV           | silent pass-through        |
 *
 * 8 outputs are declared even though only 0-1 carry audio: JUCE's AudioProcessorGraph only makes a
 * private copy of an input channel when `inputChan < numOutputs`, so declaring fewer outputs than
 * the highest CV channel we read would let our post-cache clear scribble on a buffer another node
 * still needs (see OscillatorModule for the same constraint). Root Note CV (ch7), End CV (ch8) and
 * Fine CV (ch9) were APPENDED after every other channel rather than inserted earlier, so no
 * existing saved-patch routing shifts.
 *
 * Thread safety: the sample is owned by a reference-counted SampleData. loadSampleFile() (message
 * thread) publishes a new one under a SpinLock; processBlock() takes the *try*-lock, so the audio
 * thread never blocks — a block that races a load renders silence. Replaced samples stay alive in
 * `retainedSamples` so the audio thread's refcount release can never run a destructor.
 */
class SamplerModule : public ModuleBase {
public:
    // ---- Channel indices -----------------------------------------------------
    static constexpr int kTriggerCh = 0;
    static constexpr int kPitchCVCh = 1;
    static constexpr int kPositionCVCh = 2;
    static constexpr int kGrainSizeCVCh = 3;
    static constexpr int kDensityCVCh = 4;
    static constexpr int kSprayCVCh = 5;
    static constexpr int kLevelCVCh = 6;
    static constexpr int kRootNoteCVCh = 7; // Appended last, see the class comment above
    static constexpr int kEndCVCh = 8;
    static constexpr int kFineCVCh = 9;
    static constexpr int kNumChannels = 10;

    // ---- Limits --------------------------------------------------------------
    static constexpr int kMaxGrains = 24;
    /** Longest file we will pull into RAM. A sampler that silently hangs the UI thread on a
     *  multi-gigabyte file is worse than one that truncates and says so. */
    static constexpr double kMaxSampleSeconds = 120.0;
    /** Largest block we cache CV for; longer blocks reuse the last cached value. */
    static constexpr int kMaxBlock = 4096;
    /** ~1.5 ms at 44.1 kHz — long enough to kill the click, short enough to feel instant. */
    static constexpr int kFadeSamples = 64;

    // ---- Parameter ranges (shared by the ctor and by the CV scaling) ---------
    static constexpr float kMinGrainMs = 5.0f;
    static constexpr float kMaxGrainMs = 500.0f;
    static constexpr float kGrainMsSpan = kMaxGrainMs - kMinGrainMs;
    static constexpr float kMinDensity = 1.0f;
    static constexpr float kMaxDensity = 100.0f;
    static constexpr float kDensitySpan = kMaxDensity - kMinDensity;

    struct SampleData : public juce::ReferenceCountedObject {
        using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;
        juce::AudioBuffer<float> audio;
        double sourceSampleRate = 44100.0;
        juce::String filePath;
        juce::String fileName;
        bool truncated = false;
    };

    SamplerModule()
        // StereoAudio::Declared: its legs ARE the contiguous ch0/ch1 pair, but the module declares
        // seven channels (CV inputs), so the Auto shape test cannot see it. Ships SPLIT.
        : ModuleBase("Sampler", kNumChannels, kNumChannels, StereoAudio::Declared) {
        addParameter(playModeParam = new juce::AudioParameterChoice("playMode", "Mode", {"Sample", "Granular"}, 0));
        addParameter(pitchParam = new juce::AudioParameterFloat("pitch", "Pitch", -24.0f, 24.0f, 0.0f));
        addParameter(rootNoteParam =
                         new juce::AudioParameterInt(juce::ParameterID("rootNote", 1), "Root Note", 0, 127, 60));
        addParameter(loopParam = new juce::AudioParameterBool("loop", "Loop", true));
        addParameter(startParam = new juce::AudioParameterFloat("start", "Start", 0.0f, 1.0f, 0.0f));
        addParameter(grainSizeParam =
                         new juce::AudioParameterFloat("grainSize", "Grain Size", kMinGrainMs, kMaxGrainMs, 80.0f));
        addParameter(densityParam =
                         new juce::AudioParameterFloat("density", "Density", kMinDensity, kMaxDensity, 20.0f));
        addParameter(sprayParam = new juce::AudioParameterFloat("spray", "Spray", 0.0f, 1.0f, 0.1f));
        addParameter(levelParam = new juce::AudioParameterFloat("level", "Level", 0.0f, 1.0f, 0.8f));
        // End of the played region, as a fraction of the sample like `start`. 1 = to the end of the file.
        addParameter(endParam = new juce::AudioParameterFloat("end", "End", 0.0f, 1.0f, 1.0f));
        // Fine tune in cents on top of `pitch`; a playback-rate change, so it is not smoothed.
        addParameter(fineParam = new juce::AudioParameterFloat("fine", "Fine", -100.0f, 100.0f, 0.0f));
        addParameter(reverseParam = new juce::AudioParameterBool("reverse", "Reverse", false));
        // Dual I/O comes from the ctor's StereoAudio::Declared above, defaulting to split — this
        // module has always emitted a real stereo pair. Collapsing uses the FX helpers rather than
        // the split-block ones: one "Audio" jack that owns both raw legs.
        addMuteParameter();
        enableVisualBuffer(true);
    }

    // =========================================================================
    // Sample loading (message thread only)
    // =========================================================================

    /** File-chooser wildcard covering every format JUCE's basic readers handle. */
    static juce::String getSupportedFormatWildcard() {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        return formatManager.getWildcardForAllFormats();
    }

    /** True when some registered format claims this file's extension. Used by the drag-and-drop
     *  targets to decide whether a dropped file is ours *before* opening it — a cheap extension
     *  check, deliberately not a read, so hovering a folder full of files stays free. */
    static bool isSupportedAudioFile(const juce::File& file) {
        const juce::String extension = file.getFileExtension();
        if (extension.isEmpty())
            return false;

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        return formatManager.findFormatForFileExtension(extension) != nullptr;
    }

    /** Reads `file` into memory and publishes it to the audio thread.
     *  Returns false (leaving any previously loaded sample in place) when the file is missing or
     *  no registered format can read it. `fromRestoredState` marks a restore from saved state: a project open,
     * undo/redo, paste or a snippet (see gateAt). */
    bool loadSampleFile(const juce::File& file, bool fromRestoredState = false) {
        ++fileLoadSerial; // a decode still pending for an earlier file no longer applies
        pendingPath.clear();
        return installSample(decodeSampleFile(file), fromRestoredState);
    }

    /** Reads `file` into memory; null when it is missing, unreadable or too short. Any thread: it touches no module. */
    static SampleData::Ptr decodeSampleFile(const juce::File& file) {
        if (!file.existsAsFile())
            return nullptr;

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader == nullptr)
            return nullptr;

        const double readerRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
        const juce::int64 maxFrames = (juce::int64)(kMaxSampleSeconds * readerRate);
        const juce::int64 available = reader->lengthInSamples;
        const int frames = (int)juce::jmin(available, maxFrames);
        if (frames <= 1)
            return nullptr;

        SampleData::Ptr loaded = new SampleData();
        loaded->audio.setSize(juce::jlimit(1, 2, (int)reader->numChannels), frames);
        loaded->audio.clear();
        reader->read(&loaded->audio, 0, frames, 0, true, true);
        loaded->sourceSampleRate = readerRate;
        loaded->filePath = file.getFullPathName();
        loaded->fileName = file.getFileName();
        loaded->truncated = available > maxFrames;
        return loaded;
    }

    /** Publishes a decoded sample to the audio thread: the one install path of a picked file, a restore and a
     *  deferred project-open decode. False (keeping any loaded sample) for null. Message thread only. */
    bool installSample(SampleData::Ptr loaded, bool fromRestoredState) {
        if (loaded == nullptr)
            return false;
        if (loaded->truncated) {
            // One line per load — not a per-block/per-sample log, so it cannot spam the in-app console.
            juce::Logger::writeToLog("SamplerModule: '" + loaded->fileName + "' truncated to " +
                                     juce::String(kMaxSampleSeconds, 0) + "s.");
        }

        // A file the user just picked free-runs until something is patched in; one restored from saved state
        // waits for a trigger or a note, so opening a project never makes a sound by itself.
        restoredFromState.store(fromRestoredState, std::memory_order_relaxed);
        publishSample(loaded);
        return true;
    }

    /** Drops the loaded sample; the module then renders silence. */
    void clearSample() {
        ++fileLoadSerial;
        pendingPath.clear();
        publishSample(nullptr);
    }

    /** Absolute path of the loaded file, or an empty string. Message thread only. */
    juce::String getSampleFilePath() const { return loadedPath; }

    /** File name of the loaded file, or an empty string. Message thread only. */
    juce::String getSampleName() const { return loadedName; }

    /** Bumped on every successful load / clear so UI peak caches know to rebuild. */
    int getSampleGeneration() const noexcept { return sampleGeneration.load(); }

    /** Thread-safe handle on the current sample, for drawing. Returns nullptr when none is loaded
     *  or when a load is publishing right now. */
    SampleData::Ptr getSample() const {
        const juce::SpinLock::ScopedTryLockType lock(sampleLock);
        return lock.isLocked() ? activeSample : SampleData::Ptr();
    }

    /** Normalised playhead (Sample mode) or scan position (Granular mode), 0-1. Updated once per
     *  block so the UI can draw a position line without touching the audio path. */
    float getPlayheadPosition() const noexcept { return playheadNorm.load(); }

    /** MESSAGE THREAD. Tells the module whether any cable feeds its MIDI input. While true the gate
     *  stays low until a Note-On arrives instead of free-running (see gateAt). Published by
     *  AudioEngine::refreshMidiWiring, not a parameter and not saved. */
    void setMidiInputWired(bool wired) noexcept { midiInputWired.store(wired, std::memory_order_relaxed); }
    bool isMidiInputWired() const noexcept { return midiInputWired.load(std::memory_order_relaxed); }

    /** True while playback is running (a sample is loaded and the gate is open). */
    bool isPlaying() const noexcept { return playingFlag.load(); }

    // =========================================================================
    // Non-parameter state (survives undo / preset load — see ModuleBase::getExtraState)
    // =========================================================================

    // A file still decoding (a project opening on screen) is what the module holds: a save or an undo snapshot taken
    // before it lands keeps it.
    juce::var getExtraState() const override {
        const juce::String path = pendingPath.isNotEmpty() ? pendingPath : loadedPath;
        if (path.isEmpty())
            return {};
        juce::DynamicObject::Ptr state = new juce::DynamicObject();
        state->setProperty(synth::module_file_keys::kSampleFile, path);
        return juce::var(state.get());
    }

    void setExtraState(const juce::var& state) override {
        if (auto* obj = state.getDynamicObject()) {
            const juce::String path = obj->getProperty(synth::module_file_keys::kSampleFile).toString();
            if (path.isNotEmpty() && !deferSampleLoad(juce::File(path)))
                loadSampleFile(juce::File(path), /*fromRestoredState=*/true);
        }
    }

    /** True while a project open is still decoding this module's file. Message thread only. */
    bool isSampleLoadPending() const noexcept { return pendingPath.isNotEmpty(); }

    // =========================================================================
    // AudioProcessor
    // =========================================================================

    void prepareToPlay(double sampleRate, int /*samplesPerBlock*/) override {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        // Level multiplies the rendered output, so a per-block automation write is a click.
        // Snapped to the knob at prepare so a static render is unchanged.
        smoothedLevel.reset(currentSampleRate, 0.01);
        smoothedLevel.setCurrentAndTargetValue(levelParam->get());
        resetPlayback();
    }

    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        const int numSamples = buffer.getNumSamples();
        const int numCh = buffer.getNumChannels();
        if (numCh == 0 || numSamples <= 0)
            return;

        // Pure source module: every input is CV/gate, so bypass has no dry signal to pass through.
        // Both branches therefore clear — the documented exception to the bypass/mute contract that
        // OscillatorModule and NoiseModule also take.
        if (isBypassed() || isMuted()) {
            buffer.clear();
            playingFlag.store(false);
            return;
        }

        const int ns = juce::jmin(numSamples, kMaxBlock);

        midiWired = midiInputWired.load(std::memory_order_relaxed) || restoredFromState.load(std::memory_order_relaxed);
        // ---- 1. Cache CV inputs before the buffer is cleared --------------------------------
        // The trigger channel is probed over the WHOLE block, not just the first 64 samples like the
        // CV channels: a gate that rises at sample 100 would otherwise read as an unpatched jack and
        // hand that block to the free-running fallback, firing a note nobody asked for.
        const bool trigConnected =
            cacheChannel(buffer, kTriggerCh, ns, numSamples, triggerCache, /*probeWholeBlock*/ true);
        if (trigConnected)
            triggerEverConnected = true;
        const bool hasPitchCV = cacheChannel(buffer, kPitchCVCh, ns, numSamples, pitchCache);
        const bool hasPosCV = cacheChannel(buffer, kPositionCVCh, ns, numSamples, positionCache);
        const bool hasGrainCV = cacheChannel(buffer, kGrainSizeCVCh, ns, numSamples, grainSizeCache);
        const bool hasDensityCV = cacheChannel(buffer, kDensityCVCh, ns, numSamples, densityCache);
        const bool hasSprayCV = cacheChannel(buffer, kSprayCVCh, ns, numSamples, sprayCache);
        const bool hasLevelCV = cacheChannel(buffer, kLevelCVCh, ns, numSamples, levelCache);
        const bool hasRootNoteCV = cacheChannel(buffer, kRootNoteCVCh, ns, numSamples, rootNoteCVCache);
        const bool hasEndCV = cacheChannel(buffer, kEndCVCh, ns, numSamples, endCache);
        const bool hasFineCV = cacheChannel(buffer, kFineCVCh, ns, numSamples, fineCache);

        // ---- 2. Clear every declared output channel -----------------------------------------
        for (int ch = 0; ch < getTotalNumOutputChannels() && ch < numCh; ++ch)
            buffer.clear(ch, 0, numSamples);

        // ---- 3. Nothing loaded -> silence ----------------------------------------------------
        SampleData::Ptr sample = getSample();
        if (sample == nullptr || sample->audio.getNumSamples() < 2) {
            playingFlag.store(false);
            pushSilenceToVisualBuffer(numSamples);
            return;
        }

        // ---- 4. Render (MIDI note-on/off is consumed sample-accurately below, step 4b) -------
        const int sampleFrames = sample->audio.getNumSamples();
        const int sampleChannels = sample->audio.getNumChannels();
        const float* srcL = sample->audio.getReadPointer(0);
        const float* srcR = sample->audio.getReadPointer(sampleChannels > 1 ? 1 : 0);

        // Source-rate correction: a 48k file on a 44.1k device must read slightly faster than 1.0.
        const double rateRatio = sample->sourceSampleRate / currentSampleRate;
        // Root Note is a discrete "which key is unison pitch" choice, not a per-sample signal (a
        // glide through it would just make every already-playing grain/read-head jump pitch
        // mid-flight) -- read once per block, exactly like Oscillator's Unison/Detune CV. A retrigger
        // mid-block (below) re-reads it too, so a same-block Note-On always uses the current value.
        const float rootNoteCV = hasRootNoteCV ? rootNoteCVCache[0] : 0.0f;
        const float effectiveRootNote = modulateNormalised(*rootNoteParam, (float)rootNoteParam->get(), rootNoteCV);
        // Mutable: a same-block Note-On (legato, or a loop-restart Note-Off+Note-On pair) updates
        // these mid-loop so the retriggered note plays at its own pitch, not the block's stale one.
        float midiSemis = midiEverReceived ? (midiNote - effectiveRootNote) : 0.0f;
        // Pitch and Fine are a playback *rate* (the read head stays continuous through a change), and
        // Start / End / Grain Size / Density / Spray are only consulted when a grain spawns or a loop
        // wraps — discrete events. None of them can put a step in the rendered signal, so all of them
        // are deliberately read raw. Level is the one that scales every sample, and it is smoothed.
        const float basePitch = pitchParam->get();
        const float baseFineSemis = fineParam->get() / 100.0f;
        const float baseEnd = endParam->get();
        const bool reverse = reverseParam->get();
        const float baseStart = startParam->get();
        const float baseGrainMs = grainSizeParam->get();
        const float baseDensity = densityParam->get();
        const float baseSpray = sprayParam->get();
        smoothedLevel.setTargetValue(levelParam->get());
        const bool granular = playModeParam->getIndex() == 1;
        const bool looping = loopParam->get();

        // Expected simultaneous grains, used to keep the cloud's loudness roughly independent of
        // density/size. sqrt() because grains sum incoherently.
        const float grainNorm =
            granular ? 1.0f / std::sqrt(juce::jmax(1.0f, baseDensity * baseGrainMs * 0.001f)) : 1.0f;

        // Pitch only varies per sample when pitch CV is patched, so the common case pays for one
        // std::pow per block instead of one per sample. Mutable for the same reason as midiSemis.
        double baseIncrement = std::pow(2.0, (double)(basePitch + midiSemis + baseFineSemis) / 12.0) * rateRatio;

        float* outL = buffer.getWritePointer(0);
        float* outR = (numCh > 1) ? buffer.getWritePointer(1) : nullptr;

        for (int i = 0; i < numSamples; ++i) {
            const int idx = juce::jmin(i, ns - 1);

            // --- MIDI: consume any note-on/off landing on exactly this sample --------------
            // Sample-accurate on purpose (mirrors ADSRModule's mono branch) -- see
            // applyHeldNoteMessage's doc comment for why a block-net gate drops notes.
            bool midiNoteOnThisSample = false;
            for (const auto metadata : midiMessages) {
                if (metadata.samplePosition != i)
                    continue;
                if (applyHeldNoteMessage(metadata.getMessage())) {
                    midiSemis = midiNote - effectiveRootNote;
                    baseIncrement = std::pow(2.0, (double)(basePitch + midiSemis + baseFineSemis) / 12.0) * rateRatio;
                    midiNoteOnThisSample = true;
                }
            }

            // --- gate ---
            const float endPosition = juce::jlimit(0.0f, 1.0f, baseEnd + (hasEndCV ? endCache[idx] : 0.0f));
            const bool gate = gateAt(idx);
            // The Note-On EVENT retriggers, not an edge in the gate -- see the comment above.
            if ((gate && !lastGate) || (midiNoteOnThisSample && !triggerEverConnected))
                onGateRising(baseStart, hasPosCV ? positionCache[idx] : 0.0f, endPosition, reverse, sampleFrames);
            else if (!gate && lastGate)
                envelopeTarget = 0.0f;
            lastGate = gate;

            // --- per-sample parameter values (base + CV over the parameter's own span) ---
            const double increment =
                (hasPitchCV || hasFineCV)
                    ? std::pow(2.0, (double)(basePitch + midiSemis + (hasPitchCV ? pitchCache[idx] * 24.0f : 0.0f) +
                                             baseFineSemis + (hasFineCV ? fineCache[idx] : 0.0f)) /
                                        12.0) *
                          rateRatio
                    : baseIncrement;
            const float position = juce::jlimit(0.0f, 1.0f, baseStart + (hasPosCV ? positionCache[idx] : 0.0f));
            const float level =
                juce::jlimit(0.0f, 1.0f, smoothedLevel.getNextValue() + (hasLevelCV ? levelCache[idx] : 0.0f));

            float left = 0.0f, right = 0.0f;
            // The played region. An empty one (start at or past end) is silent; end at 100 % keeps the
            // plain file-length bound, so a patch from before End existed plays exactly as it did.
            const double regionStart = (double)position * (double)(sampleFrames - 1);
            const double regionEnd = (double)endPosition * (double)(sampleFrames - 1);
            const bool emptyRegion = regionStart >= regionEnd && endPosition < 1.0f;

            if (granular) {
                const float grainMs = juce::jlimit(
                    kMinGrainMs, kMaxGrainMs, baseGrainMs + (hasGrainCV ? grainSizeCache[idx] * kGrainMsSpan : 0.0f));
                const float density = juce::jlimit(
                    kMinDensity, kMaxDensity, baseDensity + (hasDensityCV ? densityCache[idx] * kDensitySpan : 0.0f));
                const float spray = juce::jlimit(0.0f, 1.0f, baseSpray + (hasSprayCV ? sprayCache[idx] : 0.0f));

                if (envelopeTarget > 0.0f && !emptyRegion) {
                    grainClock -= 1.0;
                    if (grainClock <= 0.0) {
                        spawnGrain(position, spray, grainMs, reverse ? -increment : increment, regionEnd);
                        grainClock += juce::jmax(1.0, currentSampleRate / (double)density);
                    }
                }

                renderGrains(srcL, srcR, sampleFrames, regionEnd, left, right);
                left *= grainNorm;
                right *= grainNorm;
            } else {
                if (playhead >= 0.0 && !emptyRegion) { // an empty region holds the head and stays silent
                    left = interpolate(srcL, sampleFrames, playhead);
                    right = interpolate(srcR, sampleFrames, playhead);

                    if (reverse)
                        advanceReverse(increment, regionStart, regionEnd, looping);
                    else
                        advanceForward(increment, regionStart, regionEnd, looping, sampleFrames);
                }
            }

            // --- click-free start/stop ramp ---
            advanceEnvelope();
            const float gain = envelope * level;
            outL[i] = juce::jlimit(-1.0f, 1.0f, left * gain);
            if (outR != nullptr)
                outR[i] = juce::jlimit(-1.0f, 1.0f, right * gain);

            if (envelope <= 0.0f && envelopeTarget <= 0.0f && !granular)
                playhead = -1.0; // fully faded out: stop reading until the next trigger
        }

        // A MidiBuffer should never carry an event at/past numSamples, but keep heldNotes correct
        // defensively (mirrors ADSRModule) rather than silently dropping it.
        for (const auto metadata : midiMessages)
            if (metadata.samplePosition >= numSamples)
                applyHeldNoteMessage(metadata.getMessage());

        // Position readout for the UI — once per block, never per sample.
        if (granular)
            playheadNorm.store(juce::jlimit(0.0f, 1.0f, baseStart));
        else
            playheadNorm.store(
                playhead >= 0.0 ? juce::jlimit(0.0f, 1.0f, (float)(playhead / (double)(sampleFrames - 1))) : 0.0f);
        playingFlag.store(envelope > 0.0f || envelopeTarget > 0.0f);

        if (auto* vb = getVisualBuffer()) {
            vb->pushBlock(outL, numSamples);
        }
    }

    // =========================================================================
    // Graph / UI metadata
    // =========================================================================

    std::vector<ModulationTarget> getModulationTargets() const override {
        // Trigger (ch 0) is deliberately absent: a gate must arrive un-attenuated, so it must not
        // be auto-wrapped in an attenuverter.
        return {{"Pitch", kPitchCVCh},
                {"Start", kPositionCVCh},
                {"Grain Size", kGrainSizeCVCh},
                {"Density", kDensityCVCh},
                {"Spray", kSprayCVCh},
                {"Level", kLevelCVCh},
                {"Root Note", kRootNoteCVCh, "rootNote"},
                {"End", kEndCVCh, "end"},
                {"Fine", kFineCVCh, "fine"}};
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String labels[] = {"Trig",  "Pitch", "Start",     "Grain Size", "Density",
                                       "Spray", "Level", "Root Note", "End",        "Fine"};
        return (i >= 0 && i < kNumChannels) ? labels[i] : ModuleBase::getInputPortLabel(i);
    }

    juce::String getOutputPortLabel(int i) const override {
        return isDualIO() ? (i == 1 ? "Audio R" : "Audio L") : "Audio";
    }

    int getVisibleInputPortCount() const override { return kNumChannels; }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    // processBlock consumes note-on/off (see "MIDI: note-on retriggers and transposes" above)
    // but never writes to the MIDI buffer.
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::Oscillator; }
    ModuleType getModuleType() const override { return ModuleType::Sampler; }

    LogicalPort mapInputChannel(int raw) const override {
        if (raw >= 0 && raw < kNumChannels) {
            LogicalPort p;
            p.visibleJackIndex = raw;
            p.role = (raw == kTriggerCh) ? PortRole::Gate : PortRole::ModCV;
            p.isPolyGroupHead = true;
            p.polyVoiceSpan = 1;
            return p;
        }
        return ModuleBase::mapInputChannel(raw);
    }

    /** Contiguous ch0/ch1 pair, so this is the FX-style collapse: one "Audio" jack owning both raw
        legs (polyVoiceSpan 2) when Dual I/O is off, separate jacks when it is on. */
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

private:
    struct Grain {
        bool active = false;
        double position = 0.0;
        double increment = 1.0;
        int elapsed = 0;
        int length = 0;
    };

    // =========================================================================
    // Sample publication
    // =========================================================================

    void publishSample(SampleData::Ptr newSample) {
        // Drop anything the audio thread has already let go of (refcount 1 == only this array
        // holds it). Done here, on the message thread, so no destructor ever runs on the audio
        // thread.
        for (int i = retainedSamples.size(); --i >= 0;)
            if (retainedSamples[i]->getReferenceCount() == 1)
                retainedSamples.remove(i);

        if (newSample != nullptr)
            retainedSamples.add(newSample);

        {
            const juce::SpinLock::ScopedLockType lock(sampleLock);
            activeSample = newSample;
        }

        loadedPath = newSample != nullptr ? newSample->filePath : juce::String();
        loadedName = newSample != nullptr ? newSample->fileName : juce::String();
        sampleGeneration.fetch_add(1);
        resetPlayback();
    }

    // Inside a project open on screen (DeferredAssetLoads::active()) the file decodes on a worker and is installed
    // through installSample here later, unless something loaded or cleared the module since (the serial moved) or the
    // module is gone (the weak reference). False outside such an open: the caller decodes now.
    bool deferSampleLoad(const juce::File& file) {
        auto* loads = synth::DeferredAssetLoads::active();
        if (loads == nullptr)
            return false;
        const int serial = ++fileLoadSerial;
        pendingPath = file.getFullPathName();
        juce::WeakReference<SamplerModule> self(this);
        loads->enqueue(synth::DeferredAssetLoads::Kind::Sample, this, [self, serial, file] {
            auto decoded = decodeSampleFile(file);
            return synth::DeferredAssetLoads::Install([self, serial, decoded] {
                auto* module = self.get();
                if (module == nullptr || module->fileLoadSerial != serial)
                    return;
                module->pendingPath.clear();
                module->installSample(decoded, /*fromRestoredState=*/true);
            });
        });
        return true;
    }

    void resetPlayback() {
        playhead = -1.0;
        grainClock = 0.0;
        envelope = 0.0f;
        envelopeTarget = 0.0f;
        lastGate = false;
        triggerEverConnected = false;
        for (auto& g : grains)
            g.active = false;
        playheadNorm.store(0.0f);
        playingFlag.store(false);
    }

    // =========================================================================
    // Gate handling
    // =========================================================================

    /** Gate state for sample `idx`.
     *
     *  Precedence: a trigger cable wins; failing that, MIDI (a MIDI cable is wired, or a Note-On has
     *  been received, or the sample was restored from saved state); failing all of those, the module
     *  free-runs so that dropping it on the canvas and loading a file makes sound without any wiring.
     *  A restored Sampler waits for a note or trigger because the graph is rebuilt cable by cable while
     *  the audio thread keeps rendering, so a Sampler seen mid-load looks unpatched for a moment.
     *
     *  "A cable is connected" is latched (`triggerEverConnected`) rather than re-derived per block:
     *  a gate that is legitimately low reads as an all-zero channel, which is indistinguishable
     *  from an unpatched jack, and re-deriving it every block would make a closed gate silently
     *  hand control back to the free-running fallback. */
    bool gateAt(int idx) const {
        if (triggerEverConnected)
            return triggerCache[(size_t)idx] >= 0.5f;
        if (midiEverReceived || midiWired)
            return heldNotes.any();
        return true;
    }

    /** Updates `heldNotes`/`midiNote` from one MIDI message. Returns whether it was a Note-On --
     *  the caller uses that to force a retrigger (via `onGateRising`) regardless of whether `gateAt`
     *  itself reads a rising edge this sample.
     *
     *  Called once per message at its own exact `samplePosition` (see the processBlock loop), not
     *  batched over the whole block: a block-net gate (the previous design) collapsed two Note-Ons
     *  with no Note-Off between them (legato) into "gate never fell", and a Note-Off immediately
     *  followed by a Note-On in the SAME block (a transport loop restart whose boundary lands inside
     *  this block, see TimelineMidiSourceModule::emitBlock) into "gate never rose" -- both silently
     *  swallowed the new note. Mirrors ADSRModule's mono branch. */
    bool applyHeldNoteMessage(const juce::MidiMessage& message) {
        if (message.isNoteOn()) {
            heldNotes.set(static_cast<size_t>(message.getNoteNumber()));
            midiEverReceived = true;
            midiNote = (float)message.getNoteNumber();
            return true;
        }
        if (message.isNoteOff())
            heldNotes.reset(static_cast<size_t>(message.getNoteNumber()));
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            heldNotes.reset();
        return false;
    }

    void onGateRising(float baseStart, float positionCV, float endPosition, bool reverse, int sampleFrames) {
        const float position = juce::jlimit(0.0f, 1.0f, baseStart + positionCV);
        // A reversed note starts at the end of the region and runs down to `start`.
        playhead = (double)(reverse ? endPosition : position) * (double)(sampleFrames - 1);
        grainClock = 0.0; // fire the first grain immediately
        envelopeTarget = 1.0f;
        // A new note starts at the Level knob's CURRENT value rather than ramping up to it from
        // whatever the last note left behind — the 64-sample fade-in above is what stops the note
        // start clicking, and a second ramp on top of it would just make the attack lag the knob.
        // Level smoothing is there for changes DURING a held note (timeline automation), and this
        // snap does not weaken it.
        smoothedLevel.setCurrentAndTargetValue(levelParam->get());
    }

    void advanceForward(double increment, double regionStart, double regionEnd, bool looping, int sampleFrames) {
        playhead += increment;
        if (playhead >= regionEnd) {
            if (looping) {
                playhead = regionStart;
            } else {
                envelopeTarget = 0.0f; // ramp out, then stop below
                // End pulled in: hold the head on it, so the ramp-out never reads past the region.
                if (regionEnd < (double)(sampleFrames - 1))
                    playhead = regionEnd;
            }
        } else if (playhead < 0.0) {
            playhead = looping ? (double)(sampleFrames - 1) : 0.0;
        }
    }

    void advanceReverse(double increment, double regionStart, double regionEnd, bool looping) {
        playhead = std::min(playhead, regionEnd) - increment; // End moved down mid-note: join the region
        if (playhead < regionStart) {
            if (looping) {
                playhead = regionEnd;
            } else {
                envelopeTarget = 0.0f; // ramp out on the first frame of the region
                playhead = regionStart;
            }
        }
    }

    void advanceEnvelope() {
        const float step = 1.0f / (float)kFadeSamples;
        if (envelope < envelopeTarget)
            envelope = juce::jmin(envelopeTarget, envelope + step);
        else if (envelope > envelopeTarget)
            envelope = juce::jmax(envelopeTarget, envelope - step);
    }

    // =========================================================================
    // Granular engine
    // =========================================================================

    /** `regionEnd` (in frames) is the upper bound of the source: grains start below it and wrap at it. */
    void spawnGrain(float position, float spray, float grainMs, double increment, double regionEnd) {
        Grain* slot = nullptr;
        for (auto& g : grains) {
            if (!g.active) {
                slot = &g;
                break;
            }
        }
        if (slot == nullptr)
            return; // pool exhausted — the cloud is already as dense as it gets

        const float jitter = spray * ((random.nextFloat() * 2.0f) - 1.0f);
        const double span = regionEnd;
        double start = ((double)position + (double)jitter) * span;
        // Wrap rather than clamp, so spray near either end keeps scattering instead of piling up.
        start = std::fmod(start, span);
        if (start < 0.0)
            start += span;

        slot->active = true;
        slot->position = start;
        slot->increment = increment;
        slot->elapsed = 0;
        slot->length = juce::jmax(8, (int)(grainMs * 0.001 * currentSampleRate));
    }

    void renderGrains(const float* srcL, const float* srcR, int sampleFrames, double regionEnd, float& left,
                      float& right) {
        for (auto& g : grains) {
            if (!g.active)
                continue;
            if (regionEnd < 1.0) { // End pulled in to nothing: the cloud has no source left
                g.active = false;
                continue;
            }

            // Hann window — zero at both ends, so grains fade in and out with no discontinuity.
            const float phase = (float)g.elapsed / (float)g.length;
            const float window = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * phase);

            left += interpolate(srcL, sampleFrames, g.position) * window;
            right += interpolate(srcR, sampleFrames, g.position) * window;

            g.position += g.increment;
            if (g.position >= regionEnd || g.position < 0.0)
                g.position = std::fmod(std::fmod(g.position, regionEnd) + regionEnd, regionEnd);

            if (++g.elapsed >= g.length)
                g.active = false;
        }
    }

    // =========================================================================
    // Helpers
    // =========================================================================

    /** 4-point Catmull-Rom. Linear interpolation of a pitched-down sample audibly dulls the top
     *  octave; this costs three extra multiplies and does not. */
    static float interpolate(const float* data, int numFrames, double position) {
        const int i1 = juce::jlimit(0, numFrames - 1, (int)position);
        const float t = (float)(position - (double)(int)position);
        const int i0 = juce::jmax(0, i1 - 1);
        const int i2 = juce::jmin(numFrames - 1, i1 + 1);
        const int i3 = juce::jmin(numFrames - 1, i1 + 2);

        const float y0 = data[i0], y1 = data[i1], y2 = data[i2], y3 = data[i3];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    /** Copies `ch` into `dest` and reports whether it carried anything. A channel that is all-zero
     *  over the probed span is treated as unpatched (the same heuristic NoiseModule uses).
     *  `probeWholeBlock` widens the probe from the first 64 samples to every sample — worth the
     *  extra pass for the gate, where a missed rising edge is an audible wrong note. */
    static bool cacheChannel(const juce::AudioBuffer<float>& buffer, int ch, int ns, int numSamples,
                             std::array<float, kMaxBlock>& dest, bool probeWholeBlock = false) {
        std::fill_n(dest.data(), ns, 0.0f);
        if (ch >= buffer.getNumChannels())
            return false;

        const float* data = buffer.getReadPointer(ch);
        bool active = false;
        const int probe = probeWholeBlock ? numSamples : juce::jmin(numSamples, 64);
        for (int i = 0; i < probe; ++i) {
            if (data[i] != 0.0f) {
                active = true;
                break;
            }
        }
        if (!active)
            return false;

        std::copy_n(data, ns, dest.data());
        return true;
    }

    void pushSilenceToVisualBuffer(int numSamples) {
        if (auto* vb = getVisualBuffer()) {
            vb->pushConstant(0.0f, numSamples);
        }
    }

    // =========================================================================
    // State
    // =========================================================================

    mutable juce::SpinLock sampleLock;
    SampleData::Ptr activeSample;                            // audio thread reads under try-lock
    juce::ReferenceCountedArray<SampleData> retainedSamples; // message thread: keeps old samples alive
    juce::String loadedPath;                                 // message thread only
    juce::String loadedName;                                 // message thread only
    juce::String pendingPath;                                // message thread only: a deferred decode in flight
    int fileLoadSerial = 0;                                  // message thread only: bumped by every load request
    std::atomic<int> sampleGeneration{0};

    double currentSampleRate = 44100.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLevel;
    double playhead = -1.0; // -1 == stopped (Sample mode)
    double grainClock = 0.0;
    float envelope = 0.0f;
    float envelopeTarget = 0.0f;
    bool lastGate = false;
    bool triggerEverConnected = false;
    bool midiEverReceived = false;
    std::atomic<bool> midiInputWired{false}; // message-thread write, audio-thread read
    std::atomic<bool> restoredFromState{
        false};             // sample came from saved state (project open, undo/redo, paste): never free-run
    bool midiWired = false; // audio-thread copy for the current block: a MIDI cable is wired or the sample was restored
    float midiNote = 60.0f;
    std::bitset<128> heldNotes; // keyed by MIDI note number only, channel-agnostic (mirrors ADSRModule)

    std::array<Grain, kMaxGrains> grains{};
    juce::Random random;

    std::atomic<float> playheadNorm{0.0f};
    std::atomic<bool> playingFlag{false};

    // Pre-allocated CV caches — no heap allocation on the audio thread.
    std::array<float, kMaxBlock> triggerCache{};
    std::array<float, kMaxBlock> pitchCache{};
    std::array<float, kMaxBlock> positionCache{};
    std::array<float, kMaxBlock> grainSizeCache{};
    std::array<float, kMaxBlock> densityCache{};
    std::array<float, kMaxBlock> sprayCache{};
    std::array<float, kMaxBlock> levelCache{};
    std::array<float, kMaxBlock> rootNoteCVCache{};
    std::array<float, kMaxBlock> endCache{};
    std::array<float, kMaxBlock> fineCache{};

    juce::AudioParameterChoice* playModeParam = nullptr;
    juce::AudioParameterFloat* pitchParam = nullptr;
    juce::AudioParameterInt* rootNoteParam = nullptr;
    juce::AudioParameterBool* loopParam = nullptr;
    juce::AudioParameterFloat* startParam = nullptr;
    juce::AudioParameterFloat* grainSizeParam = nullptr;
    juce::AudioParameterFloat* densityParam = nullptr;
    juce::AudioParameterFloat* sprayParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;
    juce::AudioParameterFloat* endParam = nullptr;
    juce::AudioParameterFloat* fineParam = nullptr;
    juce::AudioParameterBool* reverseParam = nullptr;

    JUCE_DECLARE_WEAK_REFERENCEABLE(SamplerModule)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerModule)
};
