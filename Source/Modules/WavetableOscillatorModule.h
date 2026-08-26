#pragma once

#include "ModuleBase.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

/**
    Wavetable oscillator with a scannable "3D" table (Serum / Vital style).

    A wavetable is a stack of single-cycle frames. The Position parameter scans
    continuously through the stack, cross-fading between adjacent frames — that scan is
    what makes the table three-dimensional (phase x frame x amplitude). Six built-in
    tables ship with the module and any audio file can be loaded as a custom table.

    Anti-aliasing: every frame is stored as a mip pyramid. Mip m is band-limited to
    mipHarmonicLimit(m) harmonics, so the render path picks a mip whose highest harmonic
    still sits below Nyquist for the note being played. No oversampling and no per-sample
    filtering is needed — the band limiting is baked into the tables.

    Threading: built-in tables are immutable and shared process-wide. A wavetable loaded
    from disk is built on the message thread and handed to the audio thread through a
    pending/retired slot pair guarded by a SpinLock. The audio thread never allocates and
    never frees a table (see publishLoadedTable / adoptPendingTable).
*/
class WavetableOscillatorModule : public ModuleBase {
public:
    // -------------------------------------------------------------------------
    // Channel map
    //
    // Visible input jacks. In mono mode a jack's index IS its raw channel; in poly mode
    // channels 0-7 are the per-voice pitch fan and the shared mod-CV block starts at
    // kPolyModCVBase, so jack j (j >= 1) lands on kPolyModCVBase + j - 1.
    //
    // New jacks are only ever APPENDED — Position..Level keep the channel numbers they had
    // in #172 so patches saved before this change still route to the same targets.
    // -------------------------------------------------------------------------
    enum Jack {
        kJackPitch = 0, // mono: shares ch0 with Audio L, so mono pitch comes from MIDI
        kJackPosition,
        kJackOctave,
        kJackCoarse,
        kJackFine,
        kJackLevel,
        kJackWarp,   // Warp Amount
        kJackPhase,  // retrigger phase
        kJackRand,   // random-phase amount
        kJackDetune, // unison detune
        kJackSpread, // unison phase spread
        kJackWidth,  // unison stereo width
        kJackBlend,  // unison blend (centre voice vs. the detuned stack)
        kJackSub,    // sub-oscillator level
        kJackPan,
        kJackSync, // audio-rate input for Hard Sync / Ring Mod / AM
        kNumJacks
    };

    static constexpr int kNumVoices = 8;
    static constexpr int kNumModCV = kNumJacks - 1;               // every jack except Pitch
    static constexpr int kPolyModCVBase = kNumVoices;             // poly shared-CV block start
    static constexpr int kNumInputs = kPolyModCVBase + kNumModCV; // 23
    static constexpr int kRightBase = kNumInputs;                 // Audio R block starts here
    static constexpr int kNumOutputs = kRightBase + kNumVoices;   // 31

    /** Raw channel carrying jack `jack`'s CV, for the current voice mode. */
    static constexpr int modCVChannelFor(int jack, bool poly) { return poly ? (kPolyModCVBase + jack - 1) : jack; }

    // -------------------------------------------------------------------------
    // Warp / voicing / import modes
    // -------------------------------------------------------------------------
    /** Table-read warps, applied between mip selection and output.
        `Off` must stay index 0 so an old preset without the parameter defaults to no warp. */
    enum class Warp { Off = 0, Sync, BendPlus, BendMinus, PWM, Asym, Flip, Mirror, Quantize, Remap, Formant, Count };

    /** Interval stack applied across the unison voices, on top of Detune. */
    enum class Stack { Detune = 0, Octave, PowerChord, Twelfth, Major, Minor, Count };

    /** What the Sync jack does to the oscillator. */
    enum class SyncMode { Off = 0, HardSync, RingMod, AM, Count };

    /** How an audio file is cut into single-cycle frames on import. */
    enum class ImportMode {
        Auto = 0,
        Fixed256,
        Fixed512,
        Fixed1024,
        Fixed2048,
        SingleCycle,
        PitchDetect,
        Spectral,
        Count
    };

    /** Frame-read interpolation quality. */
    enum class Interpolation { Linear = 0, Hermite, Count };

    /** Frame size implied by an import mode, or 0 when the mode decides at import time. */
    static constexpr int fixedFrameSizeFor(ImportMode mode) {
        switch (mode) {
        case ImportMode::Fixed256:
            return 256;
        case ImportMode::Fixed512:
            return 512;
        case ImportMode::Fixed1024:
            return 1024;
        case ImportMode::Fixed2048:
            return 2048;
        default:
            return 0;
        }
    }

    // -------------------------------------------------------------------------
    // Table geometry
    // -------------------------------------------------------------------------
    static constexpr int kFrameSize = 2048;                 // samples per single-cycle frame (mip 0)
    static constexpr int kMaxFrames = 64;                   // frames retained from a loaded file
    static constexpr int kBuiltInFrames = 32;               // frames per built-in table
    static constexpr int kNumMips = 11;                     // 1023 harmonics down to 1
    static constexpr int kMaxHarmonic = 1023;               // highest harmonic mip 0 can hold
    static constexpr int kNumBuiltIns = 6;                  // Basic Shapes .. Digital
    static constexpr int kLoadedTableChoice = kNumBuiltIns; // "Loaded File" choice index

    /** Samples stored for mip level m. Never drops below 64 so that linear interpolation
        of the stored frame stays well conditioned. */
    static constexpr int mipLength(int m) { return ((kFrameSize >> m) > 64) ? (kFrameSize >> m) : 64; }

    /** Highest harmonic present in mip level m (also bounded by that mip's own Nyquist). */
    static constexpr int mipHarmonicLimit(int m) {
        const int byLevel = (kMaxHarmonic + 1) >> m; // 1024, 512, ... 1
        const int byNyquist = mipLength(m) / 2 - 1;
        return byLevel < byNyquist ? byLevel : byNyquist;
    }

    /** FFT order (log2 length) used to synthesise mip level m. */
    static constexpr int mipOrder(int m) {
        int order = 0;
        while ((1 << order) < mipLength(m))
            ++order;
        return order;
    }

    /** One wavetable: numFrames single-cycle frames, each stored as a mip pyramid.
        Immutable once built. */
    struct Wavetable {
        int numFrames = 1;
        juce::String name;
        juce::String sourcePath; // empty for built-ins
        std::array<std::vector<float>, kNumMips> mips;

        const float* frameData(int mip, int frame) const {
            return mips[(size_t)mip].data() + (size_t)frame * (size_t)mipLength(mip);
        }
    };

    using TablePtr = std::shared_ptr<const Wavetable>;

    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------
    WavetableOscillatorModule()
        // kNumInputs in: 8 per-voice pitch CV (0-7) + 15 shared mod CV. More outputs than inputs are
        // declared for the same reason as OscillatorModule: JUCE's AudioProcessorGraph only makes a
        // private copy of an input buffer when inputChan < getTotalNumOutputChannels(). Declaring
        // kNumOutputs stops our post-render clear of the CV channels from corrupting a CV source that
        // also feeds another node. The channels above the audio blocks are silent pass-throughs.
        //
        // StereoAudio::Declared: with more than two outputs the Auto shape test cannot see the stereo
        // pair — Audio R is the kRightBase block, not ch1. Ships SPLIT.
        : ModuleBase("Wavetable", kNumInputs, kNumOutputs, StereoAudio::Declared) {
        addParameter(tableParam = new juce::AudioParameterChoice(
                         "table", "Table",
                         {"Basic Shapes", "Harmonic Sweep", "Pulse", "Formant", "Bell", "Digital", "Loaded File"}, 0));
        addParameter(positionParam = new juce::AudioParameterFloat("position", "Position", 0.0f, 1.0f, 0.0f));
        addParameter(octaveParam = new juce::AudioParameterInt(juce::ParameterID("octave", 1), "Octave", -4, 4, 0));
        addParameter(coarseParam = new juce::AudioParameterInt(juce::ParameterID("coarse", 1), "Coarse", -12, 12, 0));
        addParameter(fineParam = new juce::AudioParameterFloat("fine", "Fine", -100.0f, 100.0f, 0.0f));
        addParameter(levelParam = new juce::AudioParameterFloat("level", "Level", 0.0f, 1.0f, 1.0f));
        addParameter(polyParam = new juce::AudioParameterBool("poly", "Poly", false));
        addParameter(unisonParam = new juce::AudioParameterInt(juce::ParameterID("unison", 1), "Unison", 1, 8, 1));
        addParameter(detuneParam = new juce::AudioParameterFloat("detune", "Detune", 0.0f, 100.0f, 0.0f));

        // ---- Phase 2: warp ----
        addParameter(warpParam = new juce::AudioParameterChoice("warp", "Warp",
                                                                {"Off", "Sync", "Bend +", "Bend -", "PWM", "Asym",
                                                                 "Flip", "Mirror", "Quantize", "Remap", "Formant"},
                                                                0));
        addParameter(warpAmountParam = new juce::AudioParameterFloat("warpAmount", "Warp Amt", 0.0f, 1.0f, 0.0f));

        // ---- Phase 1: phase control ----
        addParameter(phaseParam = new juce::AudioParameterFloat("phase", "Phase", 0.0f, 360.0f, 0.0f));
        addParameter(randomPhaseParam = new juce::AudioParameterFloat("randomPhase", "Rand Phase", 0.0f, 1.0f, 0.0f));
        addParameter(spreadParam = new juce::AudioParameterFloat("spread", "Spread", 0.0f, 1.0f, 0.0f));

        // ---- Phase 3: richer voicing ----
        addParameter(widthParam = new juce::AudioParameterFloat("width", "Width", 0.0f, 1.0f, 0.0f));
        addParameter(blendParam = new juce::AudioParameterFloat("blend", "Blend", 0.0f, 1.0f, 1.0f));
        addParameter(stackParam = new juce::AudioParameterChoice(
                         "stack", "Stack", {"Detune", "Octave", "Power Chord", "12th", "Major", "Minor"}, 0));
        addParameter(subLevelParam = new juce::AudioParameterFloat("subLevel", "Sub", 0.0f, 1.0f, 0.0f));
        addParameter(subOctaveParam = new juce::AudioParameterChoice("subOctave", "Sub Oct", {"-1", "-2"}, 0));
        addParameter(subShapeParam = new juce::AudioParameterChoice("subShape", "Sub Wave", {"Sine", "Square"}, 0));
        addParameter(panParam = new juce::AudioParameterFloat("pan", "Pan", -1.0f, 1.0f, 0.0f));
        // Dual I/O comes from the ctor's StereoAudio::Declared above, defaulting to split — this
        // module has been stereo since #180. Collapsed it shows a single "Audio" jack carrying the
        // left leg, matching every other split-block module (#219).
        addParameter(syncModeParam = new juce::AudioParameterChoice("syncMode", "Sync In",
                                                                    {"Off", "Hard Sync", "Ring Mod", "AM"}, 0));

        // ---- Phase 1 / 4: import + read quality ----
        addParameter(importModeParam = new juce::AudioParameterChoice(
                         "importMode", "Import",
                         {"Auto", "256", "512", "1024", "2048", "Single Cycle", "Pitch Detect", "Spectral"}, 0));
        addParameter(interpolationParam =
                         new juce::AudioParameterChoice("interpolation", "Interp", {"Linear", "Hermite"}, 0));

        addMuteParameter();
        enableVisualBuffer(true);

        // Force the shared built-in pyramid to be built here, on the message thread, so
        // the audio thread only ever reads it.
        builtInTables();
    }

    // -------------------------------------------------------------------------
    // ModuleBase
    // -------------------------------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

        const float initFreq = tunedFrequency(frequencyForMidiNote(voices[0].lastMidiNote));
        for (int v = 0; v < MAX_VOICES; ++v) {
            voices[v].smoothedFreq.reset(currentSampleRate, 0.005);
            voices[v].smoothedFreq.setCurrentAndTargetValue(initFreq);
        }
        smoothedPosition.reset(currentSampleRate, 0.02);
        smoothedPosition.setCurrentAndTargetValue(positionParam->get());
        smoothedLevel.reset(currentSampleRate, 0.02);
        smoothedLevel.setCurrentAndTargetValue(levelParam->get());
        smoothedWarp.reset(currentSampleRate, 0.02);
        smoothedWarp.setCurrentAndTargetValue(warpAmountParam->get());
        smoothedSub.reset(currentSampleRate, 0.02);
        smoothedSub.setCurrentAndTargetValue(subLevelParam->get());
        smoothedPan.reset(currentSampleRate, 0.02);
        smoothedPan.setCurrentAndTargetValue(panParam->get());

        for (int v = 0; v < MAX_VOICES; ++v) {
            voices[v].decimator[0].reset();
            voices[v].decimator[1].reset();
            voices[v].active = false;
        }
        lastSyncSample = 0.0f;
        blockPeakWarpAmount = 0.0f;
    }

    /** Import mode currently selected on the module. */
    ImportMode currentImportMode() const {
        return (ImportMode)juce::jlimit(0, (int)ImportMode::Count - 1, importModeParam->getIndex());
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (buffer.getNumChannels() == 0)
            return;

        // Pure source module: no audio input, so bypass has no dry signal to pass through
        // (same exception as OscillatorModule / PolyMidiModule — see docs/architecture.md).
        if (isBypassed() || isMuted()) {
            buffer.clear();
            return;
        }

        adoptPendingTable();

        for (const auto metadata : midiMessages) {
            const auto msg = metadata.getMessage();
            if (msg.isNoteOn()) {
                voices[0].lastMidiNote = (float)msg.getNoteNumber();
                pendingRetrigger = true; // consumed once the block's unison count is known
            }
        }

        if (polyParam->get())
            processPolyMode(buffer);
        else
            processMonoMode(buffer);
    }

    // -------------------------------------------------------------------------
    // Ports / modulation
    // -------------------------------------------------------------------------
    /** Jack labels, indexed by the Jack enum. */
    static const juce::String* jackLabels() {
        static const juce::String labels[kNumJacks] = {"Pitch", "Position", "Octave", "Coarse", "Fine",   "Level",
                                                       "Warp",  "Phase",    "Rand",   "Detune", "Spread", "Width",
                                                       "Blend", "Sub",      "Pan",    "Sync"};
        return labels;
    }

    std::vector<ModulationTarget> getModulationTargets() const override {
        const bool poly = polyParam->get();
        std::vector<ModulationTarget> targets;
        targets.reserve(kNumJacks);

        // Mono exposes Pitch too (it shares ch0 with Audio L and is ignored at render time, but
        // the graph still lists it so the jack is addressable); poly drives pitch from the fan.
        if (!poly)
            targets.push_back({jackLabels()[kJackPitch], 0});

        for (int jack = 1; jack < kNumJacks; ++jack)
            targets.push_back({jackLabels()[jack], modCVChannelFor(jack, poly)});

        return targets;
    }

    juce::String getInputPortLabel(int i) const override {
        return (i >= 0 && i < kNumJacks) ? jackLabels()[i] : ModuleBase::getInputPortLabel(i);
    }
    juce::String getOutputPortLabel(int i) const override { return splitAudioLabel(i); }
    int getVisibleInputPortCount() const override { return kNumJacks; }
    int getVisibleOutputPortCount() const override { return splitAudioJackCount(); }
    int rightAudioLegChannel() const override { return kRightBase; }

    // processBlock consumes note-on for the mono-mode MIDI pitch fallback, same as
    // OscillatorModule, but never writes to the MIDI buffer.
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::Oscillator; }
    ModuleType getModuleType() const override { return ModuleType::Wavetable; }

    LogicalPort mapInputChannel(int raw) const override {
        LogicalPort p;
        if (polyParam->get()) {
            // Poly: raw 0-7 = per-voice Pitch fan; the shared mod CV block follows it.
            if (raw >= 0 && raw < kNumVoices) {
                p.visibleJackIndex = kJackPitch;
                p.role = PortRole::Pitch;
                p.isPolyGroupHead = (raw == 0);
                p.polyVoiceSpan = (raw == 0) ? kNumVoices : 1;
                return p;
            }
            if (raw >= kPolyModCVBase && raw < kNumInputs) {
                p.visibleJackIndex = raw - kPolyModCVBase + 1; // ch8 -> jack 1 (Position), ...
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
        } else if (raw >= 0 && raw < kNumJacks) {
            // Mono: raw channels map straight onto the visible jacks.
            p.visibleJackIndex = raw;
            p.role = PortRole::ModCV;
            p.isPolyGroupHead = true;
            p.polyVoiceSpan = 1;
            return p;
        }
        return ModuleBase::mapInputChannel(raw);
    }

    /** Audio L lives on the voice block (ch0, or ch0-7 in poly) and Audio R on a dedicated
        block starting at kRightBase, so neither output ever collides with a mod-CV input
        channel. Poly fans both blocks eight wide, which is what lets a poly patch carry a
        stereo unison stack into the Voice Mixer. */
    LogicalPort mapOutputChannel(int raw) const override {
        const bool poly = polyParam->get();
        const int span = poly ? kNumVoices : 1;

        LogicalPort p;
        p.role = PortRole::Audio;
        p.polyVoiceSpan = 1;

        if (raw >= 0 && raw < span) {
            p.visibleJackIndex = 0;
            p.isPolyGroupHead = (raw == 0);
            p.polyVoiceSpan = (raw == 0) ? span : 1;
            return p;
        }
        // Collapsed (Dual I/O off): the right block still renders, it is simply not exposed.
        if (isDualIO() && raw >= kRightBase && raw < kRightBase + span) {
            p.visibleJackIndex = 1;
            p.isPolyGroupHead = (raw == kRightBase);
            p.polyVoiceSpan = (raw == kRightBase) ? span : 1;
            return p;
        }

        // Silent pass-through channels: addressable, but never a poly-bus head.
        p.visibleJackIndex = 0;
        p.isPolyGroupHead = false;
        return p;
    }

    bool isAutoPromotableModTarget(int dstChannel) const override {
        if (polyParam->get())
            return false;
        return ModuleBase::isAutoPromotableModTarget(dstChannel);
    }

    // -------------------------------------------------------------------------
    // State (adds the loaded wavetable path on top of the base parameter dump)
    // -------------------------------------------------------------------------
    void getStateInformation(juce::MemoryBlock& destData) override {
        juce::ValueTree state("ModuleState");
        for (auto* param : getParameters())
            if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
                state.setProperty(p->paramID, p->getValue(), nullptr);

        state.setProperty("wavetableFile", getWavetableFile().getFullPathName(), nullptr);
        state.setProperty("wavetableFolder", wavetableFolder.getFullPathName(), nullptr);
        copyXmlToBinary(*state.createXml(), destData);
    }

    // Non-parameter state that must survive a graph rebuild (undo/redo, preset save/load).
    // This — not getStateInformation — is what AIStateMapper::graphToJSON persists, so the
    // loaded wavetable path has to be published here or it is silently lost on preset load.
    juce::var getExtraState() const override {
        const juce::File file = getWavetableFile();
        if (file == juce::File() && wavetableFolder == juce::File())
            return {};
        juce::DynamicObject::Ptr state = new juce::DynamicObject();
        if (file != juce::File())
            state->setProperty("wavetableFile", file.getFullPathName());
        if (wavetableFolder != juce::File())
            state->setProperty("wavetableFolder", wavetableFolder.getFullPathName());
        return juce::var(state.get());
    }

    void setExtraState(const juce::var& state) override {
        if (auto* obj = state.getDynamicObject()) {
            const juce::String folder = obj->getProperty("wavetableFolder").toString();
            if (folder.isNotEmpty())
                setWavetableFolder(juce::File(folder));

            const juce::String path = obj->getProperty("wavetableFile").toString();
            if (path.isNotEmpty())
                loadWavetableFile(juce::File(path));
        }
    }

    void setStateInformation(const void* data, int sizeInBytes) override {
        auto xmlState = getXmlFromBinary(data, sizeInBytes);
        if (xmlState == nullptr || !xmlState->hasTagName("ModuleState"))
            return;

        const juce::ValueTree state = juce::ValueTree::fromXml(*xmlState);

        const juce::String folder = state.getProperty("wavetableFolder", juce::String()).toString();
        if (folder.isNotEmpty())
            setWavetableFolder(juce::File(folder));

        // Restore the file first: loadWavetableFile() never touches parameters, so the
        // "table" choice restored below stays authoritative. It reads importModeParam, which
        // the loop below has not restored yet — so re-import once the parameters are in.
        const juce::String path = state.getProperty("wavetableFile", juce::String()).toString();

        for (auto* param : getParameters())
            if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
                if (state.hasProperty(p->paramID))
                    param->setValue((float)state.getProperty(p->paramID));

        if (path.isNotEmpty()) {
            const juce::File file(path);
            if (file.existsAsFile())
                loadWavetableFile(file);
        }
    }

    // -------------------------------------------------------------------------
    // Wavetable loading (message thread)
    // -------------------------------------------------------------------------
    /** Reads an audio file as a wavetable and hands it to the audio thread.

        Files whose length is a whole number of kFrameSize frames are split into that many
        frames (the Serum convention); anything shorter is treated as a single cycle and
        resampled to kFrameSize. At most kMaxFrames evenly spaced frames are kept, so a
        256-frame file still spans its whole morph range.

        Returns false and leaves the current table untouched if the file cannot be read.
        Does not change any parameter — callers that want the new table to sound must also
        select the "Loaded File" choice. Message thread only. */
    bool loadWavetableFile(const juce::File& file) {
        if (!file.existsAsFile())
            return false;

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr || reader->numChannels == 0 || reader->lengthInSamples <= 0)
            return false;

        // Cap the read so a pathological file cannot exhaust memory.
        const juce::int64 maxRead = (juce::int64)kMaxFrames * 8 * kFrameSize;
        const int numSamples = (int)std::min<juce::int64>(reader->lengthInSamples, maxRead);

        juce::AudioBuffer<float> raw(1, numSamples);
        raw.clear();
        if (!reader->read(&raw, 0, numSamples, 0, true, reader->numChannels > 1))
            return false;

        auto table = buildTableFromSamples(raw.getReadPointer(0), numSamples, file.getFileNameWithoutExtension(),
                                           file.getFullPathName(), currentImportMode());
        if (table == nullptr)
            return false;

        messageLoadedTable = table;
        publishLoadedTable(std::move(table));
        return true;
    }

    /** True for any extension the wavetable loader can read. */
    static bool isSupportedWavetableFile(const juce::File& file) {
        return file.hasFileExtension("wav;aiff;aif;flac;ogg");
    }

    // -------------------------------------------------------------------------
    // Wavetable folder browser (message thread)
    // -------------------------------------------------------------------------
    /** Points the browser at a directory and rescans it. Selecting a folder does NOT load
        anything — call selectWavetableAt / nextWavetable to actually swap the table. */
    void setWavetableFolder(const juce::File& folder) {
        wavetableFolder = folder;
        folderEntries.clear();
        folderIndex = -1;

        if (!folder.isDirectory())
            return;

        for (const auto& entry :
             juce::RangedDirectoryIterator(folder, /*isRecursive*/ false, "*", juce::File::findFiles)) {
            const juce::File f = entry.getFile();
            if (isSupportedWavetableFile(f))
                folderEntries.add(f);
        }

        // Stable, human-readable ordering so next/prev walks the folder the way a file
        // browser shows it rather than in whatever order the OS enumerated.
        folderEntries.sort();

        // If the currently loaded file lives in this folder, start the cursor on it.
        const juce::File loaded = getWavetableFile();
        for (int i = 0; i < folderEntries.size(); ++i)
            if (folderEntries[i] == loaded)
                folderIndex = i;
    }

    juce::File getWavetableFolder() const { return wavetableFolder; }
    int getFolderWavetableCount() const { return folderEntries.size(); }
    int getFolderIndex() const { return folderIndex; }
    juce::File getFolderWavetable(int index) const {
        return juce::isPositiveAndBelow(index, folderEntries.size()) ? folderEntries[index] : juce::File();
    }

    /** Loads entry `index` of the scanned folder. Returns false (and leaves the cursor
        alone) when the index is out of range or the file cannot be read. */
    bool selectWavetableAt(int index) {
        if (!juce::isPositiveAndBelow(index, folderEntries.size()))
            return false;
        if (!loadWavetableFile(folderEntries[index]))
            return false;
        folderIndex = index;
        return true;
    }

    /** Steps the folder cursor by `delta` entries, wrapping at both ends. Skips over files
        that fail to load so one bad wav cannot wedge the browser. */
    bool stepWavetable(int delta) {
        const int count = folderEntries.size();
        if (count == 0 || delta == 0)
            return false;

        // Start from the current entry (or before the first one, so "next" lands on 0).
        int index = folderIndex;
        for (int attempt = 0; attempt < count; ++attempt) {
            index = ((index + delta) % count + count) % count;
            if (selectWavetableAt(index))
                return true;
        }
        return false;
    }

    bool nextWavetable() { return stepWavetable(1); }
    bool previousWavetable() { return stepWavetable(-1); }

    /** File backing the loaded table, or an invalid File when none is loaded. */
    juce::File getWavetableFile() const {
        return messageLoadedTable != nullptr ? juce::File(messageLoadedTable->sourcePath) : juce::File();
    }

    /** Frame count of the table the module would currently play. */
    int getNumFrames() const {
        const auto* wt = selectedTableForMessageThread();
        return wt != nullptr ? wt->numFrames : 0;
    }

    /** Display name of the table the module would currently play. */
    juce::String getWavetableName() const {
        const auto* wt = selectedTableForMessageThread();
        return wt != nullptr ? wt->name : juce::String();
    }

    /** True when a file-backed table is available for the "Loaded File" choice. */
    bool hasLoadedWavetable() const { return messageLoadedTable != nullptr; }

    /** Current scan position (the Position parameter), 0..1. */
    float getScanPosition() const { return positionParam->get(); }

    /** Fills `out` with numPoints samples of the frame at scan position `position`, for UI
        display. Message thread only. */
    void getDisplayWaveformAt(std::vector<float>& out, int numPoints, float position) const {
        out.assign((size_t)std::max(2, numPoints), 0.0f);
        const auto* wt = selectedTableForMessageThread();
        if (wt == nullptr || wt->numFrames <= 0)
            return;

        const float posFrames = juce::jlimit(0.0f, 1.0f, position) * (float)(wt->numFrames - 1);
        const int n = (int)out.size();

        // Draw what the oscillator actually plays, warp included — a Warp knob you cannot see
        // move is a knob users do not trust.
        const Warp warp = (Warp)juce::jlimit(0, (int)Warp::Count - 1, warpParam->getIndex());
        const float amount = warpAmountParam->get();
        const bool hermite = interpolationParam->getIndex() == 1;

        for (int i = 0; i < n; ++i)
            out[(size_t)i] = readWarped(*wt, 0, posFrames, (float)i / (float)n, warp, amount, hermite);
    }

    /** Changes to this value mean the drawn waveform changed shape. The display's repaint
        gate folds it in so a Warp tweak redraws without an unconditional per-tick repaint. */
    int getWarpSignature() const {
        return warpParam->getIndex() * 1024 + (int)std::round(warpAmountParam->get() * 200.0f) * 2 +
               interpolationParam->getIndex();
    }

    /** Fills `out` with the frame currently under the scan position. Message thread only. */
    void getDisplayWaveform(std::vector<float>& out, int numPoints) const {
        getDisplayWaveformAt(out, numPoints, positionParam->get());
    }

private:
    static constexpr int MAX_VOICES = kNumVoices;
    static constexpr int MAX_UNISON = 8;
    static constexpr int kMaxBlock = 4096;

    // ---- Oversampling -------------------------------------------------------
    // Warps that introduce a discontinuity or an amplitude nonlinearity generate harmonics the
    // mip pyramid cannot pre-empt, so those modes render at kOversample x and are decimated
    // back down. The whole voice — every unison sub-oscillator plus the sub — is summed at the
    // oversampled rate and decimated once, which is valid because decimation is linear and
    // costs one filter per voice instead of one per sub-oscillator.
    static constexpr int kOversample = 4;
    static constexpr int kDecimTaps = 33;

    /** Windowed-sinc decimation FIR, cutting at the base-rate Nyquist. Built once and shared:
        the coefficients are constant, only the per-voice delay line is stateful. */
    static const std::array<float, kDecimTaps>& decimationKernel() {
        static const std::array<float, kDecimTaps> kernel = [] {
            std::array<float, kDecimTaps> k{};
            const double cutoff = 0.5 / (double)kOversample; // cycles/sample at the oversampled rate
            const double centre = (double)(kDecimTaps - 1) * 0.5;
            double sum = 0.0;
            for (int i = 0; i < kDecimTaps; ++i) {
                const double x = (double)i - centre;
                const double sinc = (std::abs(x) < 1.0e-9)
                                        ? 2.0 * cutoff
                                        : std::sin(2.0 * juce::MathConstants<double>::pi * cutoff * x) /
                                              (juce::MathConstants<double>::pi * x);
                // Blackman window — ~-74 dB stopband, enough that the folded residue stays far
                // below the 5% ceiling HighNotesDoNotAlias holds every warp mode to.
                const double t = (double)i / (double)(kDecimTaps - 1);
                const double w = 0.42 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * t) +
                                 0.08 * std::cos(4.0 * juce::MathConstants<double>::pi * t);
                const double v = sinc * w;
                k[(size_t)i] = (float)v;
                sum += v;
            }
            if (sum > 1.0e-9)
                for (auto& v : k)
                    v = (float)((double)v / sum); // unity DC gain
            return k;
        }();
        return kernel;
    }

    /** Per-voice delay line for the decimator. Push kOversample sub-samples, then read once. */
    struct Decimator {
        std::array<float, kDecimTaps> history{};
        int writePos = 0;

        void reset() {
            history.fill(0.0f);
            writePos = 0;
        }

        inline void push(float x) {
            history[(size_t)writePos] = x;
            if (++writePos == kDecimTaps)
                writePos = 0;
        }

        inline float read() const {
            const auto& k = decimationKernel();
            float acc = 0.0f;
            int idx = writePos; // oldest sample
            for (int i = 0; i < kDecimTaps; ++i) {
                acc += history[(size_t)idx] * k[(size_t)i];
                if (++idx == kDecimTaps)
                    idx = 0;
            }
            return acc;
        }
    };

    struct VoiceState {
        float phase[MAX_UNISON]{};       // slave phase, what actually reads the table
        float masterPhase[MAX_UNISON]{}; // drives Sync's reset and Formant's window
        float subPhase = 0.0f;
        juce::SmoothedValue<float> smoothedFreq;
        float lastMidiNote = 69.0f;
        bool active = false;    // poly: was this voice sounding last block?
        Decimator decimator[2]; // one per stereo leg — panning happens before decimation

        /** Restarts every sub-oscillator at the configured retrigger phase. `spread`
            decorrelates the unison stack, `randomAmount` adds per-note jitter — without
            either, a unison stack or a poly chord attacks perfectly phase-correlated and
            comb-filters itself. */
        void resetPhases(float startPhase, float spread, float randomAmount, int unisonCount, juce::Random& rng) {
            const int count = std::max(1, unisonCount);
            for (int u = 0; u < MAX_UNISON; ++u) {
                float p = startPhase;
                if (count > 1)
                    p += spread * (float)u / (float)count;
                if (randomAmount > 0.0f)
                    p += randomAmount * rng.nextFloat();
                p -= std::floor(p);
                phase[u] = p;
                masterPhase[u] = p;
            }
            subPhase = startPhase - std::floor(startPhase);
            decimator[0].reset();
            decimator[1].reset();
        }
    };

    // -------------------------------------------------------------------------
    // Table synthesis (message thread only)
    // -------------------------------------------------------------------------
    /** Builds mip pyramids from harmonic spectra. Owns one FFT per distinct mip length and
        self-calibrates the inverse-transform gain, so the resulting tables do not depend on
        the platform FFT engine's normalisation convention. */
    class TableBuilder {
    public:
        TableBuilder() {
            for (int order = kMinOrder; order <= kMaxOrder; ++order)
                ffts[(size_t)(order - kMinOrder)] = std::make_unique<juce::dsp::FFT>(order);

            work.resize(2 * kFrameSize, 0.0f);

            // A unit fundamental in the spectrum must come out as a unit-amplitude sine.
            for (int m = 0; m < kNumMips; ++m) {
                const int len = mipLength(m);
                std::fill(work.begin(), work.begin() + 2 * len, 0.0f);
                work[3] = -1.0f; // bin 1, imaginary part
                fftFor(m).performRealOnlyInverseTransform(work.data());

                float peak = 0.0f;
                for (int i = 0; i < len; ++i)
                    peak = std::max(peak, std::abs(work[(size_t)i]));
                invScale[m] = peak > 0.0f ? 1.0f / peak : 1.0f;
            }
        }

        /** Allocates a table with room for numFrames frames at every mip level. */
        static std::unique_ptr<Wavetable> allocate(int numFrames, const juce::String& name,
                                                   const juce::String& sourcePath) {
            auto wt = std::make_unique<Wavetable>();
            wt->numFrames = std::max(1, numFrames);
            wt->name = name;
            wt->sourcePath = sourcePath;
            for (int m = 0; m < kNumMips; ++m)
                wt->mips[(size_t)m].assign((size_t)wt->numFrames * (size_t)mipLength(m), 0.0f);
            return wt;
        }

        /** Renders one frame's whole mip pyramid from cosine/sine harmonic amplitudes.
            cosAmp/sinAmp are indexed by harmonic number (index 0 unused). */
        void renderFrame(Wavetable& wt, int frame, const float* cosAmp, const float* sinAmp, int maxHarmonic) {
            for (int m = 0; m < kNumMips; ++m) {
                const int len = mipLength(m);
                const int limit = std::min(mipHarmonicLimit(m), maxHarmonic);

                std::fill(work.begin(), work.begin() + 2 * len, 0.0f);
                for (int h = 1; h <= limit; ++h) {
                    work[(size_t)(2 * h)] = cosAmp[h];
                    work[(size_t)(2 * h + 1)] = -sinAmp[h];
                }

                fftFor(m).performRealOnlyInverseTransform(work.data());

                float* dst = wt.mips[(size_t)m].data() + (size_t)frame * (size_t)len;
                const float scale = invScale[m];
                for (int i = 0; i < len; ++i)
                    dst[i] = work[(size_t)i] * scale;
            }
        }

        /** Analyses one single-cycle frame of kFrameSize samples into harmonic amplitudes.
            Bin 0 (DC) is discarded so loaded tables cannot introduce a DC offset. */
        void analyseFrame(const float* cycle, float* cosAmp, float* sinAmp) {
            analyseCycle(cycle, kFrameSize, cosAmp, sinAmp);
        }

        /** Analyses a single cycle of `length` samples (a power of two in [64, kFrameSize]).

            Analysing at the source's own frame size — rather than resampling the cycle up to
            kFrameSize first — keeps the import exact: a 256-sample frame carries at most 127
            harmonics and they are read straight off a 256-point FFT, with none of the HF droop
            linear upsampling would introduce. The absolute FFT scale differs between sizes, but
            every frame of one import shares a size and normalise() rescales the table at the
            end, so only the relative amplitudes matter. */
        void analyseCycle(const float* cycle, int length, float* cosAmp, float* sinAmp) {
            const int len = juce::jlimit(mipLength(kNumMips - 1), kFrameSize, length);
            const int order = orderForLength(len);
            const int usable = (1 << order);

            std::fill(work.begin(), work.end(), 0.0f);
            std::copy_n(cycle, usable, work.begin());
            fftByOrder(order).performRealOnlyForwardTransform(work.data(), true);

            std::fill_n(cosAmp, kMaxHarmonic + 1, 0.0f);
            std::fill_n(sinAmp, kMaxHarmonic + 1, 0.0f);

            const int topHarmonic = std::min(kMaxHarmonic, usable / 2 - 1);
            for (int h = 1; h <= topHarmonic; ++h) {
                cosAmp[h] = work[(size_t)(2 * h)];
                sinAmp[h] = -work[(size_t)(2 * h + 1)];
            }
        }

        /** Collapses a spectrum onto sine phase, keeping magnitudes.

            This is the "spectral" import: every frame is resynthesised zero-phase, so scanning
            the stack cross-fades magnitudes instead of beating phase-incoherent frames against
            each other. It is what makes a table sampled from unrelated cycles morph smoothly. */
        static void collapseToSinePhase(float* cosAmp, float* sinAmp) {
            for (int h = 1; h <= kMaxHarmonic; ++h) {
                const float mag = std::sqrt(cosAmp[h] * cosAmp[h] + sinAmp[h] * sinAmp[h]);
                cosAmp[h] = 0.0f;
                sinAmp[h] = mag;
            }
        }

        /** Scales the whole table so mip 0 peaks at 1.0. Keeps relative frame levels. */
        static void normalise(Wavetable& wt) {
            float peak = 0.0f;
            for (float v : wt.mips[0])
                peak = std::max(peak, std::abs(v));
            if (peak <= 1.0e-9f)
                return;

            const float gain = 1.0f / peak;
            for (auto& mip : wt.mips)
                for (float& v : mip)
                    v *= gain;
        }

    private:
        static constexpr int kMinOrder = 6;  // shortest mip is 64 samples
        static constexpr int kMaxOrder = 11; // longest mip is kFrameSize samples

        /** Largest order whose length still fits in `length` (which callers clamp into range). */
        static int orderForLength(int length) {
            int order = kMinOrder;
            while (order < kMaxOrder && (1 << (order + 1)) <= length)
                ++order;
            return order;
        }

        juce::dsp::FFT& fftFor(int mip) { return fftByOrder(mipOrder(mip)); }
        juce::dsp::FFT& fftByOrder(int order) {
            return *ffts[(size_t)(juce::jlimit(kMinOrder, kMaxOrder, order) - kMinOrder)];
        }

        std::unique_ptr<juce::dsp::FFT> ffts[kMaxOrder - kMinOrder + 1];
        float invScale[kNumMips]{};
        std::vector<float> work;
    };

    // ---- Built-in harmonic specs -------------------------------------------
    /** Sine-phase harmonic amplitude of one of the four classic shapes. */
    static float classicShapeHarmonic(int shape, int h) {
        const float pi = juce::MathConstants<float>::pi;
        switch (shape) {
        case 0: // Sine
            return h == 1 ? 1.0f : 0.0f;
        case 1: { // Triangle
            if (h % 2 == 0)
                return 0.0f;
            const int k = (h - 1) / 2;
            const float sign = (k % 2 == 0) ? 1.0f : -1.0f;
            return sign * 8.0f / (pi * pi * (float)h * (float)h);
        }
        case 2: { // Saw
            const float sign = (h % 2 == 1) ? 1.0f : -1.0f;
            return sign * 2.0f / (pi * (float)h);
        }
        case 3: // Square
            return (h % 2 == 1) ? 4.0f / (pi * (float)h) : 0.0f;
        default:
            return 0.0f;
        }
    }

    /** Fills the harmonic spectrum of one frame of built-in table `tableIndex`. */
    static void builtInSpectrum(int tableIndex, int frame, int numFrames, float* cosAmp, float* sinAmp) {
        std::fill_n(cosAmp, kMaxHarmonic + 1, 0.0f);
        std::fill_n(sinAmp, kMaxHarmonic + 1, 0.0f);

        const float t = (numFrames > 1) ? (float)frame / (float)(numFrames - 1) : 0.0f;

        switch (tableIndex) {
        case 0: { // Basic Shapes — sine -> triangle -> saw -> square
            const float p = t * 3.0f;
            const int seg = std::min(2, (int)p);
            const float f = p - (float)seg;
            for (int h = 1; h <= kMaxHarmonic; ++h) {
                const float a = classicShapeHarmonic(seg, h);
                const float b = classicShapeHarmonic(seg + 1, h);
                sinAmp[h] = a + (b - a) * f;
            }
            break;
        }
        case 1: { // Harmonic Sweep — a Gaussian band of partials walking up the series
            const float centre = 1.0f + t * 23.0f;
            const float sigma = 1.6f;
            for (int h = 1; h <= 64; ++h) {
                const float d = ((float)h - centre) / sigma;
                sinAmp[h] = std::exp(-0.5f * d * d) + 0.25f / (float)h;
            }
            break;
        }
        case 2: { // Pulse — duty cycle sweeping from a narrow spike to a square
            const float pi = juce::MathConstants<float>::pi;
            const float width = 0.04f + t * 0.46f;
            for (int h = 1; h <= 256; ++h)
                cosAmp[h] = (4.0f / (pi * (float)h)) * std::sin(pi * (float)h * width);
            break;
        }
        case 3: { // Formant — two moving resonant peaks over a 1/h tilt
            const float c1 = 2.0f + t * 6.0f;
            const float c2 = 10.0f + t * 14.0f;
            for (int h = 1; h <= 64; ++h) {
                const float d1 = ((float)h - c1) / 2.0f;
                const float d2 = ((float)h - c2) / 3.0f;
                const float peaks = std::exp(-0.5f * d1 * d1) + 0.6f * std::exp(-0.5f * d2 * d2);
                sinAmp[h] = (peaks + 0.05f) / (float)h;
            }
            break;
        }
        case 4: { // Bell — sparse stretched partials, brightening across the scan
            const int partials[] = {1, 2, 3, 5, 7, 11, 13, 17};
            const int numPartials = (int)(sizeof(partials) / sizeof(partials[0]));
            const float decay = 0.55f - 0.45f * t;
            for (int i = 0; i < numPartials; ++i) {
                const float amp = std::exp(-decay * (float)i);
                if (i % 2 == 0)
                    sinAmp[partials[i]] = amp;
                else
                    cosAmp[partials[i]] = amp; // alternating phase gives the metallic beating
            }
            break;
        }
        default: { // Digital — deterministic pseudo-random spectra, brightening across the scan
            juce::Random rng(0x5EED0000 + frame);
            const int topHarmonic = 8 + (int)(t * 56.0f);
            for (int h = 1; h <= topHarmonic; ++h) {
                const float amp = rng.nextFloat() / (float)h;
                const float phase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
                cosAmp[h] = amp * std::cos(phase);
                sinAmp[h] = amp * std::sin(phase);
            }
            break;
        }
        }
    }

    static const std::array<TablePtr, kNumBuiltIns>& builtInTables() {
        // Function-local static: built once, on whichever thread constructs the first
        // WavetableOscillatorModule (always the message thread), then read-only forever.
        static const std::array<TablePtr, kNumBuiltIns> tables = buildBuiltInTables();
        return tables;
    }

    static std::array<TablePtr, kNumBuiltIns> buildBuiltInTables() {
        static const char* const names[kNumBuiltIns] = {"Basic Shapes", "Harmonic Sweep", "Pulse",
                                                        "Formant",      "Bell",           "Digital"};
        TableBuilder builder;
        std::vector<float> cosAmp((size_t)kMaxHarmonic + 1, 0.0f);
        std::vector<float> sinAmp((size_t)kMaxHarmonic + 1, 0.0f);

        std::array<TablePtr, kNumBuiltIns> out{};
        for (int i = 0; i < kNumBuiltIns; ++i) {
            auto wt = TableBuilder::allocate(kBuiltInFrames, names[i], {});
            for (int f = 0; f < kBuiltInFrames; ++f) {
                builtInSpectrum(i, f, kBuiltInFrames, cosAmp.data(), sinAmp.data());
                builder.renderFrame(*wt, f, cosAmp.data(), sinAmp.data(), kMaxHarmonic);
            }
            TableBuilder::normalise(*wt);
            out[(size_t)i] = TablePtr(std::move(wt));
        }
        return out;
    }

    /** Splits mono sample data into single-cycle frames and builds their mip pyramids.

        `mode` decides how the file is cut:
          - Auto          whole kFrameSize blocks when the file is long enough, else one cycle
          - Fixed256..2048 whole blocks of that size, analysed at their own size
          - SingleCycle   the entire file resampled to one cycle
          - PitchDetect   autocorrelation finds the period, then blocks of that period
          - Spectral      like Auto, but every frame is resynthesised zero-phase */
    static TablePtr buildTableFromSamples(const float* samples, int numSamples, const juce::String& name,
                                          const juce::String& sourcePath, ImportMode mode) {
        if (samples == nullptr || numSamples <= 0)
            return nullptr;

        // ---- Decide the source frame size ----
        int frameLen = fixedFrameSizeFor(mode);
        if (mode == ImportMode::SingleCycle) {
            frameLen = numSamples; // one frame spanning the file
        } else if (mode == ImportMode::PitchDetect) {
            frameLen = detectPeriod(samples, numSamples);
        } else if (frameLen == 0) {
            frameLen = kFrameSize; // Auto / Spectral keep the Serum convention
        }
        frameLen = std::max(2, std::min(frameLen, numSamples));

        const int available = numSamples / frameLen;
        const int sourceFrames = std::max(1, available);
        const int numFrames = std::min(kMaxFrames, sourceFrames);

        // A power-of-two frame that fits the analysis buffer can be analysed at its own size;
        // anything else (an odd pitch-detected period, a whole-file single cycle, or a frame
        // longer than kFrameSize) is resampled into kFrameSize first. The upper bound is load
        // bearing: `cycle` is exactly kFrameSize long, so copying a longer frame into it
        // verbatim would run off the end.
        const bool analyseNative = available >= 1 && isPowerOfTwo(frameLen) && frameLen >= 64 && frameLen <= kFrameSize;
        const int analysisLen = analyseNative ? frameLen : kFrameSize;

        TableBuilder builder;
        std::vector<float> cycle((size_t)kFrameSize, 0.0f);
        std::vector<float> cosAmp((size_t)kMaxHarmonic + 1, 0.0f);
        std::vector<float> sinAmp((size_t)kMaxHarmonic + 1, 0.0f);
        auto wt = TableBuilder::allocate(numFrames, name, sourcePath);

        for (int f = 0; f < numFrames; ++f) {
            if (available >= 1) {
                // Evenly spaced source frames, so a long file still spans its morph range.
                const int src = (numFrames > 1) ? (int)((juce::int64)f * (sourceFrames - 1) / (numFrames - 1)) : 0;
                const float* segment = samples + (size_t)src * (size_t)frameLen;
                if (analyseNative)
                    std::copy_n(segment, frameLen, cycle.begin());
                else
                    resample(segment, frameLen, cycle.data(), kFrameSize);
            } else {
                // Shorter than one frame: treat the whole file as a single cycle.
                resample(samples, numSamples, cycle.data(), kFrameSize);
            }

            builder.analyseCycle(cycle.data(), analysisLen, cosAmp.data(), sinAmp.data());
            if (mode == ImportMode::Spectral)
                TableBuilder::collapseToSinePhase(cosAmp.data(), sinAmp.data());
            builder.renderFrame(*wt, f, cosAmp.data(), sinAmp.data(), kMaxHarmonic);
        }

        TableBuilder::normalise(*wt);
        return TablePtr(std::move(wt));
    }

    static bool isPowerOfTwo(int v) { return v > 0 && (v & (v - 1)) == 0; }

    /** Linear-resamples srcLen samples into exactly dstLen samples. */
    static void resample(const float* samples, int srcLen, float* out, int dstLen) {
        if (srcLen <= 0 || dstLen <= 0)
            return;
        for (int i = 0; i < dstLen; ++i) {
            const float pos = (float)i * (float)srcLen / (float)dstLen;
            const int i0 = std::min(srcLen - 1, (int)pos);
            const int i1 = std::min(srcLen - 1, i0 + 1);
            const float frac = pos - (float)i0;
            out[i] = samples[i0] + (samples[i1] - samples[i0]) * frac;
        }
    }

    /** Estimates the waveform period in samples by normalised autocorrelation.

        Only the first few thousand samples are searched — a wavetable source is periodic from
        the start, and bounding the search keeps the O(n·lag) scan cheap. Falls back to
        kFrameSize when nothing correlates well enough, so a non-periodic file still imports. */
    static int detectPeriod(const float* samples, int numSamples) {
        constexpr int kMinPeriod = 16;
        constexpr int kMaxPeriod = 4096;
        const int window = std::min(numSamples, kMaxPeriod * 2);
        const int maxLag = std::min(kMaxPeriod, window / 2);
        if (maxLag <= kMinPeriod)
            return kFrameSize;

        double energy = 0.0;
        for (int i = 0; i < window; ++i)
            energy += (double)samples[i] * samples[i];
        if (energy <= 1.0e-12)
            return kFrameSize;

        int bestLag = 0;
        double bestScore = 0.0;
        for (int lag = kMinPeriod; lag <= maxLag; ++lag) {
            const int n = window - lag;
            double corr = 0.0, normA = 0.0, normB = 0.0;
            for (int i = 0; i < n; ++i) {
                const double a = samples[i], b = samples[i + lag];
                corr += a * b;
                normA += a * a;
                normB += b * b;
            }
            const double denom = std::sqrt(normA * normB);
            if (denom <= 1.0e-12)
                continue;
            const double score = corr / denom;

            // The margin is what stops the classic octave error: a periodic signal correlates
            // just as well at 2x and 3x its period, and without it floating-point noise decides
            // which multiple wins. Requiring a clearly better score keeps the SHORTEST lag,
            // which is the actual period.
            if (score > bestScore + 1.0e-3) {
                bestScore = score;
                bestLag = lag;
            }
        }

        // 0.9 keeps clearly periodic sources and rejects noise, where the best lag is arbitrary.
        return (bestScore > 0.9 && bestLag >= kMinPeriod) ? bestLag : kFrameSize;
    }

    // -------------------------------------------------------------------------
    // Table handoff, message thread <-> audio thread
    // -------------------------------------------------------------------------
    /** Publishes a freshly built table. Reclaims the slot the audio thread retired first,
        so the audio thread never has to free anything. */
    void publishLoadedTable(TablePtr table) {
        TablePtr unconsumed, reclaimed;
        {
            const juce::SpinLock::ScopedLockType lock(tableLock);
            unconsumed = std::move(pendingTable);
            reclaimed = std::move(retiredTable);
            pendingTable = std::move(table);
        }
        // unconsumed / reclaimed are released here, on this (message) thread.
    }

    /** Audio thread: take a published table if one is waiting. Pointer moves only — no
        allocation, no deallocation. Skipped (and retried next block) when the message
        thread is mid-publish or has not reclaimed the previous table yet. */
    void adoptPendingTable() {
        const juce::SpinLock::ScopedTryLockType lock(tableLock);
        if (!lock.isLocked() || pendingTable == nullptr || retiredTable != nullptr)
            return;

        retiredTable = std::move(audioLoadedTable);
        audioLoadedTable = std::move(pendingTable);
    }

    /** Table the audio thread should render, honouring the Table choice and falling back
        to the first built-in when "Loaded File" is selected with nothing loaded. */
    const Wavetable* audioTable() const {
        const int choice = tableParam->getIndex();
        if (choice == kLoadedTableChoice)
            return audioLoadedTable != nullptr ? audioLoadedTable.get() : builtInTables()[0].get();
        return builtInTables()[(size_t)juce::jlimit(0, kNumBuiltIns - 1, choice)].get();
    }

    /** Message-thread mirror of audioTable(), for UI queries and state save. */
    const Wavetable* selectedTableForMessageThread() const {
        const int choice = tableParam->getIndex();
        if (choice == kLoadedTableChoice)
            return messageLoadedTable != nullptr ? messageLoadedTable.get() : builtInTables()[0].get();
        return builtInTables()[(size_t)juce::jlimit(0, kNumBuiltIns - 1, choice)].get();
    }

    // -------------------------------------------------------------------------
    // Render helpers
    // -------------------------------------------------------------------------
    static float frequencyForMidiNote(float midiNote) { return 440.0f * std::pow(2.0f, (midiNote - 69.0f) / 12.0f); }

    /** Applies the Octave / Coarse / Fine tuning parameters to a base frequency. */
    float tunedFrequency(float baseHz) const {
        const float semis = (float)octaveParam->get() * 12.0f + (float)coarseParam->get() + fineParam->get() / 100.0f;
        return semis != 0.0f ? baseHz * std::pow(2.0f, semis / 12.0f) : baseHz;
    }

    /** Linear read of one stored frame at a normalised phase, wrapping at the end. */
    static float readFrame(const Wavetable& wt, int mip, int frame, float phase) {
        const int len = mipLength(mip);
        const float fp = phase * (float)len;
        int i0 = (int)fp;
        if (i0 < 0)
            i0 = 0;
        if (i0 >= len)
            i0 = len - 1;
        const int i1 = (i0 + 1 == len) ? 0 : i0 + 1;
        const float frac = fp - (float)i0;
        const float* d = wt.frameData(mip, frame);
        return d[i0] + (d[i1] - d[i0]) * frac;
    }

    /** 4-point Catmull-Rom (Hermite) read of one stored frame.

        The coarse mips are only 64-256 samples long, where linear interpolation between
        stored points visibly droops the top harmonics; the cubic fit follows the curve
        instead of chording it, at the cost of three extra taps per read. */
    static float readFrameHermite(const Wavetable& wt, int mip, int frame, float phase) {
        const int len = mipLength(mip);
        const float fp = phase * (float)len;
        int i1 = (int)fp;
        if (i1 < 0)
            i1 = 0;
        if (i1 >= len)
            i1 = len - 1;
        const float t = fp - (float)i1;

        const float* d = wt.frameData(mip, frame);
        const int mask = len - 1; // every mip length is a power of two
        const float y0 = d[(i1 - 1) & mask];
        const float y1 = d[i1];
        const float y2 = d[(i1 + 1) & mask];
        const float y3 = d[(i1 + 2) & mask];

        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    static float readFrameWith(const Wavetable& wt, int mip, int frame, float phase, bool hermite) {
        return hermite ? readFrameHermite(wt, mip, frame, phase) : readFrame(wt, mip, frame, phase);
    }

    /** Bilinear read: interpolates within the frame (phase) and between frames (scan). */
    static float sampleTable(const Wavetable& wt, int mip, float posFrames, float phase, bool hermite = false) {
        const int last = wt.numFrames - 1;
        int f0 = (int)posFrames;
        if (f0 < 0)
            f0 = 0;
        if (f0 >= last)
            return readFrameWith(wt, mip, last, phase, hermite);

        const float frac = posFrames - (float)f0;
        const float a = readFrameWith(wt, mip, f0, phase, hermite);
        const float b = readFrameWith(wt, mip, f0 + 1, phase, hermite);
        return a + (b - a) * frac;
    }

    /** Finest mip whose highest harmonic still clears Nyquist for a phase increment of
        dt cycles/sample. */
    static int selectMip(float dt) {
        if (!(dt > 0.0f))
            return 0;
        const int maxHarmonic = std::max(1, (int)(0.5f / dt));
        for (int m = 0; m < kNumMips; ++m)
            if (mipHarmonicLimit(m) <= maxHarmonic)
                return m;
        return kNumMips - 1;
    }

    // -------------------------------------------------------------------------
    // Warp
    //
    // A warp reshapes the table read AFTER the mip has been chosen, so it can reintroduce
    // exactly the aliasing the pyramid exists to prevent. Two defences, applied together:
    //
    //   1. warpRateFactor() reports how much faster than 1x a mode can sweep the table at its
    //      steepest point. selectMip() is fed dt * factor, so the stored frame is already
    //      band-limited for the fastest read the warp will perform.
    //   2. warpNeedsOversampling() flags the modes whose output has a step or an amplitude
    //      nonlinearity — those generate harmonics no amount of input band-limiting can
    //      prevent, so they render at kOversample x and are filtered on the way back down.
    //
    // Anything added here must be covered by HighNotesDoNotAlias, which sweeps every mode.
    // -------------------------------------------------------------------------

    /** Steepest phase-map slope a mode reaches, i.e. the factor by which it can outrun a
        plain 1x table read. Modes that leave the read rate alone report 1. */
    static float warpRateFactor(Warp mode, float amount) {
        const float a = juce::jlimit(0.0f, 1.0f, amount);
        switch (mode) {
        case Warp::Sync:
            return 1.0f + a * 7.0f;
        case Warp::BendPlus:
        case Warp::BendMinus:
            return 1.0f + a;
        case Warp::Asym: {
            const float w = asymBreakpoint(a);
            return 0.5f / std::min(w, 1.0f - w);
        }
        case Warp::Mirror:
            return 1.0f + a;
        case Warp::Remap:
            return 1.0f; // a staircase is flat between steps; the steps are what oversampling handles
        case Warp::Formant:
            return 1.0f + a * 3.0f;
        case Warp::Off:
        case Warp::PWM:
        case Warp::Flip:
        case Warp::Quantize:
        case Warp::Count:
        default:
            return 1.0f;
        }
    }

    /** True for modes whose output is discontinuous or amplitude-nonlinear. */
    static bool warpNeedsOversampling(Warp mode, float amount) {
        if (amount <= 0.0f)
            return false;
        switch (mode) {
        case Warp::Sync:     // slave phase jumps when the master wraps
        case Warp::Flip:     // hard amplitude fold
        case Warp::Quantize: // amplitude staircase
        case Warp::Remap:    // phase staircase
            return true;
        default:
            return false;
        }
    }

    static float asymBreakpoint(float amount) { return 0.5f - juce::jlimit(0.0f, 1.0f, amount) * 0.4f; }

    /** Largest warp amount whose sped-up table read still lands below Nyquist.

        Mip selection band-limits the HARMONICS of a read, but a mode like Sync also multiplies
        the read's own fundamental — at 8x, a 4 kHz note reads at 33 kHz, which no mip can
        rescue. Backing the amount off at extreme pitches keeps the anti-aliasing guarantee
        instead of trading it for a knob that goes all the way up. At musical pitches the clamp
        never binds: an 8x Sync only starts costing amount above about 1.8 kHz.

        The bound is the BASE Nyquist, deliberately not the oversampled one. The oversampling
        headroom exists for the harmonics a warp's discontinuity throws off, and those get
        filtered away on the way back down; the sped-up read's own fundamental has to stay
        audible after that filter. Spending the headroom here instead would let Sync push the
        slave past 22 kHz, where the decimator removes it and the knob just fades to silence. */
    static float clampWarpAmount(Warp mode, float amount, float dt) {
        if (!(dt > 0.0f))
            return amount;

        const float maxRate = 0.45f / dt;
        const auto limitFor = [&](float perUnit) { return juce::jlimit(0.0f, amount, (maxRate - 1.0f) / perUnit); };

        switch (mode) {
        case Warp::Sync:
            return limitFor(7.0f);
        case Warp::Formant:
            return limitFor(3.0f);
        case Warp::BendPlus:
        case Warp::BendMinus:
        case Warp::Mirror:
            return limitFor(1.0f);
        case Warp::Asym: {
            // rate = 0.5 / (0.5 - 0.4a), so a = (0.5 - 0.5/rate) / 0.4
            const float maxA = (maxRate > 1.0f) ? ((0.5f - 0.5f / maxRate) / 0.4f) : 0.0f;
            return juce::jlimit(0.0f, amount, maxA);
        }
        default:
            return amount;
        }
    }

    static float wrapPhase(float p) { return p - std::floor(p); }

    /** Maps the running phase through the mode's phase distortion. `master` is the
        undistorted phase; the return value is what reads the table. */
    static float warpPhaseMap(Warp mode, float master, float amount) {
        const float a = juce::jlimit(0.0f, 1.0f, amount);
        const float p = wrapPhase(master);

        switch (mode) {
        case Warp::Sync:
            return wrapPhase(p * (1.0f + a * 7.0f));

        case Warp::BendPlus:
            // Slope runs 1-a .. 1+a, so the read never outpaces warpRateFactor().
            return (1.0f - a) * p + a * p * p;

        case Warp::BendMinus:
            return (1.0f - a) * p + a * p * (2.0f - p);

        case Warp::Asym: {
            const float w = asymBreakpoint(a);
            return (p < w) ? (0.5f * p / w) : (0.5f + 0.5f * (p - w) / (1.0f - w));
        }

        case Warp::Mirror: {
            const float mirrored = (p < 0.5f) ? (2.0f * p) : (2.0f * (1.0f - p));
            return p + (mirrored - p) * a;
        }

        case Warp::Remap: {
            const int steps = 4 + (int)((1.0f - a) * 60.0f);
            const float stepped = std::floor(p * (float)steps) / (float)steps;
            return p + (stepped - p) * a;
        }

        case Warp::Formant:
            return wrapPhase(p * (1.0f + a * 3.0f));

        case Warp::Off:
        case Warp::PWM:
        case Warp::Flip:
        case Warp::Quantize:
        case Warp::Count:
        default:
            return p;
        }
    }

    /** Amplitude-domain half of a warp, applied to the value read from the table. */
    static float warpSample(Warp mode, float s, float amount, float master) {
        const float a = juce::jlimit(0.0f, 1.0f, amount);
        switch (mode) {
        case Warp::Flip: {
            // Wavefolder: drive, then reflect anything past +/-1 back inside.
            float driven = s * (1.0f + a * 3.0f);
            for (int i = 0; i < 4 && (driven > 1.0f || driven < -1.0f); ++i)
                driven = (driven > 1.0f) ? (2.0f - driven) : (-2.0f - driven);
            return s + (driven - s) * a;
        }
        case Warp::Quantize: {
            const float steps = 2.0f + (1.0f - a) * 30.0f;
            const float stepped = std::round(s * steps) / steps;
            return s + (stepped - s) * a;
        }
        case Warp::Formant: {
            // Raised-cosine window over the master cycle keeps the sped-up read continuous at
            // the cycle edge, which is what makes this a formant shift rather than a buzz.
            const float w = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * wrapPhase(master));
            return s * (1.0f - a + a * w);
        }
        default:
            return s;
        }
    }

    /** Reads the table for one sub-oscillator, applying the active warp.
        PWM is handled here rather than in warpPhaseMap because it needs two table reads. */
    static float readWarped(const Wavetable& wt, int mip, float posFrames, float master, Warp mode, float amount,
                            bool hermite) {
        if (mode == Warp::Off || amount <= 0.0f)
            return sampleTable(wt, mip, posFrames, wrapPhase(master), hermite);

        if (mode == Warp::PWM) {
            // Classic PWM: the wave minus a phase-shifted copy of itself. Subtracting two reads
            // of the same band-limited table cannot add harmonics, so this mode is alias-free
            // by construction and needs neither a mip nudge nor oversampling.
            const float shift = 0.5f - amount * 0.49f;
            const float a = sampleTable(wt, mip, posFrames, wrapPhase(master), hermite);
            const float b = sampleTable(wt, mip, posFrames, wrapPhase(master + shift), hermite);
            return a - b;
        }

        const float phase = warpPhaseMap(mode, master, amount);
        const float raw = sampleTable(wt, mip, posFrames, wrapPhase(phase), hermite);
        return warpSample(mode, raw, amount, master);
    }

    // -------------------------------------------------------------------------
    // Unison stacking
    // -------------------------------------------------------------------------
    /** Semitone offset of unison voice `u` for the selected stack mode. Voice 0 always stays
        at the root so Blend can fade the stack against an unshifted centre. */
    static float stackSemitones(Stack mode, int u, int unisonCount) {
        if (mode == Stack::Detune || unisonCount <= 1 || u == 0)
            return 0.0f;

        switch (mode) {
        case Stack::Octave:
            return 12.0f * (float)(u % 3);
        case Stack::PowerChord: {
            static const float steps[] = {0.0f, 7.0f, 12.0f, 19.0f};
            return steps[u % 4];
        }
        case Stack::Twelfth: {
            static const float steps[] = {0.0f, 19.0f, 12.0f, 31.0f};
            return steps[u % 4];
        }
        case Stack::Major: {
            static const float steps[] = {0.0f, 4.0f, 7.0f, 12.0f};
            return steps[u % 4];
        }
        case Stack::Minor: {
            static const float steps[] = {0.0f, 3.0f, 7.0f, 12.0f};
            return steps[u % 4];
        }
        default:
            return 0.0f;
        }
    }

    /** Equal-power stereo placement for unison voice `u`. width 0 collapses to centre. */
    static void unisonPanGains(int u, int unisonCount, float width, float& gainL, float& gainR) {
        float pan = 0.0f;
        if (unisonCount > 1)
            pan = width * (2.0f * (float)u / (float)(unisonCount - 1) - 1.0f);
        panGains(pan, gainL, gainR);
    }

    // The balance pan law itself lives on ModuleBase::panGains — Oscillator and Filter grew
    // Audio L/R blocks of their own in #219 and all three modules must place a signal identically.

    static bool isChannelActive(const juce::AudioBuffer<float>& buffer, int ch, int numSamples) {
        if (ch >= buffer.getNumChannels())
            return false;
        const float* data = buffer.getReadPointer(ch);
        const int checkLen = std::min(numSamples, 64);
        float energy = 0.0f;
        for (int i = 0; i < checkLen; ++i)
            energy += data[i] * data[i];
        return (energy / (float)checkLen) > 1.0e-6f;
    }

    /** Everything about a block that is the same for every voice. Gathered once in
        process*Mode so the per-voice renderer does not re-read parameters eight times. */
    struct BlockSettings {
        int unisonCount = 1;
        float detuneCents = 0.0f;
        Warp warp = Warp::Off;
        Stack stack = Stack::Detune;
        SyncMode syncMode = SyncMode::Off;
        Interpolation interpolation = Interpolation::Linear;
        bool pitchModulated = false;
        bool oversample = false;
        float subOctaveRatio = 0.5f; // -1 octave
        float subPosition = 0.0f;    // scan position of the sub's shape in Basic Shapes
        // Per-block voicing gains (width/blend move slowly; their CV lands at block rate).
        float uniGainL[MAX_UNISON]{};
        float uniGainR[MAX_UNISON]{};
        float uniRatio[MAX_UNISON]{};
        float uniNormalise = 1.0f;
    };

    /** Renders one voice into `outL` / `outR`. freqRamp holds the (already smoothed)
        per-sample base frequency in Hz; positionRamp / levelRamp hold the smoothed parameter
        values that the corresponding CV is added to. Scratch arrays only hold `cacheLen`
        entries, so blocks longer than kMaxBlock hold the last cached CV/ramp value for the
        remainder (same behaviour as OscillatorModule). */
    void renderVoice(const Wavetable& wt, VoiceState& voice, float* outL, float* outR, int numSamples, int cacheLen,
                     const BlockSettings& bs) {
        const float invSampleRate = 1.0f / (float)currentSampleRate;
        const int oversample = bs.oversample ? kOversample : 1;
        const float subStep = 1.0f / (float)oversample;

        // The ramp is monotone, so its highest-frequency end bounds the harmonic content for
        // the whole block — pick the mip from that end to stay alias-free.
        const float mipFreq = std::max(freqRamp[0], freqRamp[(size_t)(cacheLen - 1)]);

        // Worst-case warp rate over the block, so the mip is chosen for the fastest read the
        // warp will ever perform rather than for its value at sample 0.
        const float blockDt = juce::jlimit(20.0f, 20000.0f, mipFreq) * invSampleRate;
        const float warpRate = warpRateFactor(bs.warp, clampWarpAmount(bs.warp, blockPeakWarpAmount, blockDt));

        // Dividing by the oversample factor targets the OVERSAMPLED Nyquist: those extra
        // harmonics are representable while we render at kOversample x, and the decimator
        // takes them back out on the way down. Band-limiting to the base Nyquist here instead
        // would collapse a hard-synced table to a sine before the warp ever saw it.
        const float mipRateScale = warpRate / (float)oversample;

        int uniMip[MAX_UNISON];
        for (int u = 0; u < bs.unisonCount; ++u)
            uniMip[u] = selectMip(blockDt * bs.uniRatio[u] * mipRateScale);

        const auto& subTable = *builtInTables()[0];
        const float subPosFrames = bs.subPosition * (float)(subTable.numFrames - 1);
        const float lastFrame = (float)(wt.numFrames - 1);
        const bool hermite = bs.interpolation == Interpolation::Hermite;

        for (int s = 0; s < numSamples; ++s) {
            const int idx = std::min(s, cacheLen - 1);

            float freq = freqRamp[(size_t)idx];
            if (bs.pitchModulated) {
                float semis = 0.0f;
                if (hasCV(kJackOctave))
                    semis += std::round(cvAt(kJackOctave, idx) * 4.0f) * 12.0f;
                if (hasCV(kJackCoarse))
                    semis += std::round(cvAt(kJackCoarse, idx) * 12.0f);
                if (hasCV(kJackFine))
                    semis += cvAt(kJackFine, idx);
                if (semis != 0.0f)
                    freq *= std::pow(2.0f, semis / 12.0f);
            }
            const float dt = juce::jlimit(20.0f, 20000.0f, freq) * invSampleRate;

            const float position = juce::jlimit(0.0f, 1.0f, positionRamp[(size_t)idx] + cvAt(kJackPosition, idx));
            const float posFrames = position * lastFrame;
            const float level = juce::jlimit(0.0f, 1.0f, levelRamp[(size_t)idx] + cvAt(kJackLevel, idx));
            const float warpAmount =
                clampWarpAmount(bs.warp, juce::jlimit(0.0f, 1.0f, warpRamp[(size_t)idx] + cvAt(kJackWarp, idx)), dt);
            const float subLevel = juce::jlimit(0.0f, 1.0f, subRamp[(size_t)idx] + cvAt(kJackSub, idx));

            float panL, panR;
            panGains(juce::jlimit(-1.0f, 1.0f, panRamp[(size_t)idx] + cvAt(kJackPan, idx)), panL, panR);

            // Hard sync resets every sub-oscillator on the master's rising zero crossing.
            if (bs.syncMode == SyncMode::HardSync && syncResetCache[(size_t)idx]) {
                for (int u = 0; u < MAX_UNISON; ++u) {
                    voice.phase[u] = 0.0f;
                    voice.masterPhase[u] = 0.0f;
                }
            }

            float accL = 0.0f, accR = 0.0f;
            for (int k = 0; k < oversample; ++k) {
                float subL = 0.0f, subR = 0.0f;

                for (int u = 0; u < bs.unisonCount; ++u) {
                    const float uniDt = dt * bs.uniRatio[u] * subStep;
                    const int mip = bs.pitchModulated ? selectMip(dt * bs.uniRatio[u] * mipRateScale) : uniMip[u];

                    const float v = readWarped(wt, mip, posFrames, voice.masterPhase[u], bs.warp, warpAmount, hermite);
                    subL += v * bs.uniGainL[u];
                    subR += v * bs.uniGainR[u];

                    voice.masterPhase[u] += uniDt;
                    // `while`, not `if`: at very low sample rates a single note can advance more
                    // than a full cycle per sample, and one subtraction would leave phase >= 1.
                    while (voice.masterPhase[u] >= 1.0f)
                        voice.masterPhase[u] -= 1.0f;
                    voice.phase[u] = voice.masterPhase[u];
                }

                subL *= bs.uniNormalise;
                subR *= bs.uniNormalise;

                // Sub-oscillator: read out of the band-limited Basic Shapes table rather than
                // generated naively, so it inherits the same mip anti-aliasing as everything else.
                if (subLevel > 0.0f) {
                    const float subDt = dt * bs.subOctaveRatio * subStep;
                    const int subMip = selectMip(dt * bs.subOctaveRatio);
                    const float sv = sampleTable(subTable, subMip, subPosFrames, voice.subPhase, hermite) * subLevel;
                    subL += sv;
                    subR += sv;
                    voice.subPhase += subDt;
                    while (voice.subPhase >= 1.0f)
                        voice.subPhase -= 1.0f;
                }

                if (bs.oversample) {
                    voice.decimator[0].push(subL);
                    voice.decimator[1].push(subR);
                } else {
                    accL = subL;
                    accR = subR;
                }
            }

            if (bs.oversample) {
                accL = voice.decimator[0].read();
                accR = voice.decimator[1].read();
            }

            // Ring mod / AM multiply the finished voice, so they apply to the whole stack.
            if (bs.syncMode == SyncMode::RingMod || bs.syncMode == SyncMode::AM) {
                const float sync = cvAt(kJackSync, idx);
                const float gain = (bs.syncMode == SyncMode::RingMod) ? sync : (0.5f + 0.5f * sync);
                accL *= gain;
                accR *= gain;
            }

            outL[s] = accL * level * panL;
            outR[s] = accR * level * panR;
        }
    }

    /** Fills the per-sample parameter ramps for this block. */
    void fillParameterRamps(int numSamples) {
        smoothedPosition.setTargetValue(positionParam->get());
        smoothedLevel.setTargetValue(levelParam->get());
        smoothedWarp.setTargetValue(warpAmountParam->get());
        smoothedSub.setTargetValue(subLevelParam->get());
        smoothedPan.setTargetValue(panParam->get());

        float peakWarp = 0.0f;
        for (int s = 0; s < numSamples; ++s) {
            positionRamp[(size_t)s] = smoothedPosition.getNextValue();
            levelRamp[(size_t)s] = smoothedLevel.getNextValue();
            warpRamp[(size_t)s] = smoothedWarp.getNextValue();
            subRamp[(size_t)s] = smoothedSub.getNextValue();
            panRamp[(size_t)s] = smoothedPan.getNextValue();

            peakWarp = std::max(peakWarp, juce::jlimit(0.0f, 1.0f, warpRamp[(size_t)s] + cvAt(kJackWarp, s)));
        }

        // Mip selection is per block, so it has to see the block's HIGHEST warp amount — a
        // ramp that ends at full warp must not be band-limited for where it started.
        blockPeakWarpAmount = peakWarp;
    }

    /** Gathers the voice-independent settings for this block, including the unison gain
        table. Width and Blend land at block rate: they are voicing settings whose CV does
        not need sample accuracy, and keeping them out of the inner loop keeps the
        oversampled path affordable. */
    BlockSettings gatherBlockSettings() {
        BlockSettings bs;
        bs.unisonCount = juce::jlimit(1, MAX_UNISON, unisonParam->get());
        bs.warp = (Warp)juce::jlimit(0, (int)Warp::Count - 1, warpParam->getIndex());
        bs.stack = (Stack)juce::jlimit(0, (int)Stack::Count - 1, stackParam->getIndex());
        bs.syncMode = (SyncMode)juce::jlimit(0, (int)SyncMode::Count - 1, syncModeParam->getIndex());
        bs.interpolation =
            (Interpolation)juce::jlimit(0, (int)Interpolation::Count - 1, interpolationParam->getIndex());
        bs.pitchModulated = hasCV(kJackOctave) || hasCV(kJackCoarse) || hasCV(kJackFine);
        bs.oversample = warpNeedsOversampling(bs.warp, blockPeakWarpAmount);
        bs.subOctaveRatio = (subOctaveParam->getIndex() == 1) ? 0.25f : 0.5f;
        bs.subPosition = (subShapeParam->getIndex() == 1) ? 1.0f : 0.0f; // Basic Shapes: sine .. square

        bs.detuneCents = juce::jlimit(0.0f, 100.0f, detuneParam->get() + cvAt(kJackDetune, 0) * 100.0f);
        const float width = juce::jlimit(0.0f, 1.0f, widthParam->get() + cvAt(kJackWidth, 0));
        const float blend = juce::jlimit(0.0f, 1.0f, blendParam->get() + cvAt(kJackBlend, 0));

        float gainSum = 0.0f;
        for (int u = 0; u < bs.unisonCount; ++u) {
            const float cents =
                (bs.unisonCount > 1) ? bs.detuneCents * (2.0f * (float)u / (float)(bs.unisonCount - 1) - 1.0f) : 0.0f;
            const float semis = stackSemitones(bs.stack, u, bs.unisonCount);
            bs.uniRatio[u] = std::pow(2.0f, semis / 12.0f + cents / 1200.0f);

            // Blend fades the detuned/stacked voices against an always-present centre, so
            // turning it down thins the chorus without changing the fundamental's level.
            const float voiceGain = (u == 0) ? 1.0f : blend;
            float gl, gr;
            unisonPanGains(u, bs.unisonCount, width, gl, gr);
            bs.uniGainL[u] = gl * voiceGain;
            bs.uniGainR[u] = gr * voiceGain;
            gainSum += voiceGain;
        }

        // At width 0 / blend 1 every unison voice contributes unity to both legs, so this
        // reduces to the 1/unisonCount average #172 used — Audio L keeps its old level.
        bs.uniNormalise = (gainSum > 0.0f) ? (1.0f / gainSum) : 1.0f;
        return bs;
    }

    /** Marks the samples where the Sync input crosses zero going up. Computed once per block
        because every voice needs the same crossings, and the voices render one after another. */
    void buildSyncResets(int cacheLen, SyncMode mode) {
        if (mode != SyncMode::HardSync || !hasCV(kJackSync)) {
            std::fill_n(syncResetCache.data(), cacheLen, false);
            return;
        }
        for (int s = 0; s < cacheLen; ++s) {
            const float v = cvAt(kJackSync, s);
            syncResetCache[(size_t)s] = (lastSyncSample <= 0.0f && v > 0.0f);
            lastSyncSample = v;
        }
    }

    /** Restarts a voice's oscillators at the configured retrigger phase. Phase / Rand /
        Spread are sampled at the note-on instant — they shape the attack, not the sustain. */
    void retriggerVoice(VoiceState& voice, int unisonCount) {
        const float startPhase = juce::jlimit(0.0f, 1.0f, phaseParam->get() / 360.0f + cvAt(kJackPhase, 0));
        const float spread = juce::jlimit(0.0f, 1.0f, spreadParam->get() + cvAt(kJackSpread, 0));
        const float randomAmount = juce::jlimit(0.0f, 1.0f, randomPhaseParam->get() + cvAt(kJackRand, 0));
        voice.resetPhases(startPhase, spread, randomAmount, unisonCount, phaseRandom);
    }

    /** Fills freqRamp with a voice's smoothed per-sample frequency in Hz. */
    void fillFrequencyRamp(VoiceState& voice, float targetHz, int numSamples) {
        voice.smoothedFreq.setTargetValue(targetHz);
        for (int s = 0; s < numSamples; ++s)
            freqRamp[(size_t)s] = voice.smoothedFreq.getNextValue();
    }

    // -------------------------------------------------------------------------
    // Mono mode (voice 0, MIDI driven)
    // -------------------------------------------------------------------------
    void processMonoMode(juce::AudioBuffer<float>& buffer) {
        const int numSamples = buffer.getNumSamples();
        const int numCh = buffer.getNumChannels();
        if (numSamples <= 0)
            return;
        const int cacheLen = std::max(1, std::min(numSamples, kMaxBlock));

        // Mono jack 0 (Pitch) shares channel 0 with Audio L, so — exactly as in
        // OscillatorModule — pitch CV is ignored in mono mode; MIDI drives the pitch.
        cacheModCV(buffer, /*poly*/ false, cacheLen, numSamples);

        // Clearing the CV channels here is safe because they were cached above and the module
        // declares kNumOutputs outputs, so JUCE hands us a private copy of any CV buffer that
        // is also consumed downstream. Do NOT reduce the output count.
        for (int ch = 0; ch < getTotalNumOutputChannels() && ch < numCh; ++ch)
            buffer.clear(ch, 0, numSamples);

        const Wavetable* wt = audioTable();
        if (wt == nullptr)
            return;

        fillParameterRamps(cacheLen);
        const BlockSettings bs = gatherBlockSettings();
        buildSyncResets(cacheLen, bs.syncMode);

        if (pendingRetrigger) {
            retriggerVoice(voices[0], bs.unisonCount);
            pendingRetrigger = false;
        }

        fillFrequencyRamp(voices[0], tunedFrequency(frequencyForMidiNote(voices[0].lastMidiNote)), cacheLen);

        const int rightCh = kRightBase;
        float* outL = buffer.getWritePointer(0);
        float* outR = (rightCh < numCh) ? buffer.getWritePointer(rightCh) : scratchRight.data();
        renderVoice(*wt, voices[0], outL, outR, numSamples, cacheLen, bs);

        pushToVisualBuffer(buffer, numSamples);
    }

    // -------------------------------------------------------------------------
    // Poly mode (voices 0-7, pitch CV in Hz on channels 0-7)
    // -------------------------------------------------------------------------
    void processPolyMode(juce::AudioBuffer<float>& buffer) {
        const int numSamples = buffer.getNumSamples();
        const int numCh = buffer.getNumChannels();
        if (numSamples <= 0)
            return;
        const int cacheLen = std::max(1, std::min(numSamples, kMaxBlock));

        for (int v = 0; v < MAX_VOICES; ++v)
            pitchCV[(size_t)v] = (v < numCh) ? buffer.getReadPointer(v)[0] : 0.0f;

        cacheModCV(buffer, /*poly*/ true, cacheLen, numSamples);

        // See the note in processMonoMode: safe because the CVs are cached and the module
        // declares kNumOutputs outputs.
        for (int ch = 0; ch < getTotalNumOutputChannels() && ch < numCh; ++ch)
            buffer.clear(ch, 0, numSamples);

        const Wavetable* wt = audioTable();
        if (wt == nullptr)
            return;

        fillParameterRamps(cacheLen);
        const BlockSettings bs = gatherBlockSettings();
        buildSyncResets(cacheLen, bs.syncMode);

        for (int v = 0; v < MAX_VOICES && v < numCh; ++v) {
            float freq = pitchCV[(size_t)v];
            if (freq < 20.0f && v == 0)
                freq = frequencyForMidiNote(voices[0].lastMidiNote); // MIDI fallback for voice 0

            const bool sounding = freq >= 20.0f;
            if (!sounding) {
                voices[v].active = false;
                continue;
            }

            // A voice going from silent to sounding is a note-on: that is where the retrigger
            // phase, the random-phase jitter and the unison spread get applied.
            if (!voices[v].active || (v == 0 && pendingRetrigger)) {
                retriggerVoice(voices[v], bs.unisonCount);
                voices[v].active = true;
            }

            fillFrequencyRamp(voices[v], tunedFrequency(freq), cacheLen);

            const int rightCh = kRightBase + v;
            float* outL = buffer.getWritePointer(v);
            float* outR = (rightCh < numCh) ? buffer.getWritePointer(rightCh) : scratchRight.data();
            renderVoice(*wt, voices[v], outL, outR, numSamples, cacheLen, bs);
        }
        pendingRetrigger = false;

        // Shared CV channels must not leak downstream as audio. The Audio R block sits above
        // them, so clear only the span between the two audio blocks.
        for (int ch = MAX_VOICES; ch < kRightBase && ch < numCh; ++ch)
            buffer.clear(ch, 0, numSamples);

        pushToVisualBuffer(buffer, numSamples);
    }

    /** Snapshots every shared mod-CV channel for this block. Doing it in one pass keeps the
        two voice modes from drifting apart as jacks are added. */
    void cacheModCV(const juce::AudioBuffer<float>& buffer, bool poly, int cacheLen, int numSamples) {
        for (int jack = 1; jack < kNumJacks; ++jack) {
            const int ch = modCVChannelFor(jack, poly);
            const size_t slot = (size_t)(jack - 1);
            cvActive[slot] = isChannelActive(buffer, ch, numSamples);
            if (cvActive[slot] && ch < buffer.getNumChannels())
                std::copy_n(buffer.getReadPointer(ch), cacheLen, cvCache[slot].data());
            else
                std::fill_n(cvCache[slot].data(), cacheLen, 0.0f);
        }
    }

    bool hasCV(int jack) const { return cvActive[(size_t)(jack - 1)]; }
    float cvAt(int jack, int idx) const {
        const size_t slot = (size_t)(jack - 1);
        return cvActive[slot] ? cvCache[slot][(size_t)idx] : 0.0f;
    }

    void pushToVisualBuffer(const juce::AudioBuffer<float>& buffer, int numSamples) {
        if (auto* vb = getVisualBuffer()) {
            const float* ch0 = buffer.getReadPointer(0);
            for (int i = 0; i < numSamples; ++i)
                vb->pushSample(ch0[i]);
        }
    }

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------
    VoiceState voices[MAX_VOICES];
    double currentSampleRate = 44100.0;
    juce::SmoothedValue<float> smoothedPosition;
    juce::SmoothedValue<float> smoothedLevel;
    juce::SmoothedValue<float> smoothedWarp;
    juce::SmoothedValue<float> smoothedSub;
    juce::SmoothedValue<float> smoothedPan;

    // Table handoff
    juce::SpinLock tableLock;    // guards pendingTable / retiredTable only
    TablePtr pendingTable;       // message thread -> audio thread
    TablePtr retiredTable;       // audio thread -> message thread
    TablePtr audioLoadedTable;   // audio thread only
    TablePtr messageLoadedTable; // message thread only (UI queries, state save)

    // Wavetable folder browser (message thread only)
    juce::File wavetableFolder;
    juce::Array<juce::File> folderEntries;
    int folderIndex = -1;

    // Pre-allocated scratch — no heap traffic on the audio thread
    std::array<std::array<float, kMaxBlock>, kNumModCV> cvCache{};
    std::array<bool, kNumModCV> cvActive{};
    std::array<float, kMaxBlock> positionRamp{};
    std::array<float, kMaxBlock> levelRamp{};
    std::array<float, kMaxBlock> warpRamp{};
    std::array<float, kMaxBlock> subRamp{};
    std::array<float, kMaxBlock> panRamp{};
    std::array<float, kMaxBlock> freqRamp{};
    std::array<bool, kMaxBlock> syncResetCache{};
    // Somewhere to dump Audio R when the host hands us fewer channels than we declare.
    std::array<float, kMaxBlock> scratchRight{};
    std::array<float, MAX_VOICES> pitchCV{};

    float blockPeakWarpAmount = 0.0f;
    float lastSyncSample = 0.0f;
    bool pendingRetrigger = false;
    juce::Random phaseRandom{0x5EED1234};

    juce::AudioParameterChoice* tableParam = nullptr;
    juce::AudioParameterFloat* positionParam = nullptr;
    juce::AudioParameterInt* octaveParam = nullptr;
    juce::AudioParameterInt* coarseParam = nullptr;
    juce::AudioParameterFloat* fineParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;
    juce::AudioParameterBool* polyParam = nullptr;
    juce::AudioParameterInt* unisonParam = nullptr;
    juce::AudioParameterFloat* detuneParam = nullptr;
    juce::AudioParameterChoice* warpParam = nullptr;
    juce::AudioParameterFloat* warpAmountParam = nullptr;
    juce::AudioParameterFloat* phaseParam = nullptr;
    juce::AudioParameterFloat* randomPhaseParam = nullptr;
    juce::AudioParameterFloat* spreadParam = nullptr;
    juce::AudioParameterFloat* widthParam = nullptr;
    juce::AudioParameterFloat* blendParam = nullptr;
    juce::AudioParameterChoice* stackParam = nullptr;
    juce::AudioParameterFloat* subLevelParam = nullptr;
    juce::AudioParameterChoice* subOctaveParam = nullptr;
    juce::AudioParameterChoice* subShapeParam = nullptr;
    juce::AudioParameterFloat* panParam = nullptr;
    juce::AudioParameterChoice* syncModeParam = nullptr;
    juce::AudioParameterChoice* importModeParam = nullptr;
    juce::AudioParameterChoice* interpolationParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableOscillatorModule)
};

// Mip geometry sanity: the pyramid must span 1023 harmonics down to 1, and the FFT orders
// the builder instantiates must cover every mip length.
static_assert(WavetableOscillatorModule::mipHarmonicLimit(0) == 1023, "mip 0 must hold 1023 harmonics");
static_assert(WavetableOscillatorModule::mipHarmonicLimit(WavetableOscillatorModule::kNumMips - 1) == 1,
              "the coarsest mip must hold exactly the fundamental");
static_assert(WavetableOscillatorModule::mipOrder(0) == 11, "mip 0 length must be 2048");
static_assert(WavetableOscillatorModule::mipOrder(WavetableOscillatorModule::kNumMips - 1) == 6,
              "the coarsest mip length must be 64");
