#pragma once

#include "../ModuleBase.h"
#include "WavetableTableBuilder.h"
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
    // Table geometry, the Wavetable storage type and TableBuilder live in
    // WavetableTableBuilder.h/.cpp (FRO73 split, to get this header under the file-size cap).
    // These aliases/forwards keep every existing WavetableOscillatorModule::X call site (this
    // class's own methods, tests, static_asserts below) compiling unchanged.
    static constexpr int kFrameSize = wavetable::kFrameSize;
    static constexpr int kMaxFrames = 64;     // frames retained from a loaded file
    static constexpr int kBuiltInFrames = 32; // frames per built-in table
    static constexpr int kNumMips = wavetable::kNumMips;
    static constexpr int kMaxHarmonic = wavetable::kMaxHarmonic;
    static constexpr int kNumBuiltIns = 6;                  // Basic Shapes .. Digital
    static constexpr int kLoadedTableChoice = kNumBuiltIns; // "Loaded File" choice index

    static constexpr int mipLength(int m) { return wavetable::mipLength(m); }
    static constexpr int mipHarmonicLimit(int m) { return wavetable::mipHarmonicLimit(m); }
    static constexpr int mipOrder(int m) { return wavetable::mipOrder(m); }

    using Wavetable = wavetable::Wavetable;
    using TablePtr = wavetable::TablePtr;
    using TableBuilder = wavetable::TableBuilder;

    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------
    WavetableOscillatorModule();

    // -------------------------------------------------------------------------
    // ModuleBase
    // -------------------------------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;

    /** Import mode currently selected on the module. */
    ImportMode currentImportMode() const {
        return (ImportMode)juce::jlimit(0, (int)ImportMode::Count - 1, importModeParam->getIndex());
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    // -------------------------------------------------------------------------
    // Ports / modulation
    // -------------------------------------------------------------------------
    /** Jack labels, indexed by the Jack enum. */
    static const juce::String* jackLabels();

    std::vector<ModulationTarget> getModulationTargets() const override;

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

    LogicalPort mapInputChannel(int raw) const override;

    /** Audio L lives on the voice block (ch0, or ch0-7 in poly) and Audio R on a dedicated
        block starting at kRightBase, so neither output ever collides with a mod-CV input
        channel. Poly fans both blocks eight wide, which is what lets a poly patch carry a
        stereo unison stack into the Voice Mixer. */
    LogicalPort mapOutputChannel(int raw) const override;

    bool isAutoPromotableModTarget(int dstChannel) const override;

    // -------------------------------------------------------------------------
    // State (adds the loaded wavetable path on top of the base parameter dump)
    // -------------------------------------------------------------------------
    void getStateInformation(juce::MemoryBlock& destData) override;

    // Non-parameter state that must survive a graph rebuild (undo/redo, preset save/load).
    // This — not getStateInformation — is what AIStateMapper::graphToJSON persists, so the
    // loaded wavetable path has to be published here or it is silently lost on preset load.
    juce::var getExtraState() const override;

    void setExtraState(const juce::var& state) override;

    void setStateInformation(const void* data, int sizeInBytes) override;

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
    bool loadWavetableFile(const juce::File& file);

    /** True for any extension the wavetable loader can read. */
    static bool isSupportedWavetableFile(const juce::File& file) {
        return file.hasFileExtension("wav;aiff;aif;flac;ogg");
    }

    // -------------------------------------------------------------------------
    // Wavetable folder browser (message thread)
    // -------------------------------------------------------------------------
    /** Points the browser at a directory and rescans it. Selecting a folder does NOT load
        anything — call selectWavetableAt / nextWavetable to actually swap the table. */
    void setWavetableFolder(const juce::File& folder);

    juce::File getWavetableFolder() const { return wavetableFolder; }
    int getFolderWavetableCount() const { return folderEntries.size(); }
    int getFolderIndex() const { return folderIndex; }
    juce::File getFolderWavetable(int index) const {
        return juce::isPositiveAndBelow(index, folderEntries.size()) ? folderEntries[index] : juce::File();
    }

    /** Loads entry `index` of the scanned folder. Returns false (and leaves the cursor
        alone) when the index is out of range or the file cannot be read. */
    bool selectWavetableAt(int index);

    /** Steps the folder cursor by `delta` entries, wrapping at both ends. Skips over files
        that fail to load so one bad wav cannot wedge the browser. */
    bool stepWavetable(int delta);

    bool nextWavetable() { return stepWavetable(1); }
    bool previousWavetable() { return stepWavetable(-1); }

    /** File backing the loaded table, or an invalid File when none is loaded. */
    juce::File getWavetableFile() const {
        return messageLoadedTable != nullptr ? juce::File(messageLoadedTable->sourcePath) : juce::File();
    }

    /** Frame count of the table the module would currently play. */
    int getNumFrames() const;

    /** Display name of the table the module would currently play. */
    juce::String getWavetableName() const;

    /** True when a file-backed table is available for the "Loaded File" choice. */
    bool hasLoadedWavetable() const { return messageLoadedTable != nullptr; }

    /** Current scan position (the Position parameter), 0..1. */
    float getScanPosition() const { return positionParam->get(); }

    /** Fills `out` with numPoints samples of the frame at scan position `position`, for UI
        display. Message thread only. */
    void getDisplayWaveformAt(std::vector<float>& out, int numPoints, float position) const;

    /** Changes to this value mean the drawn waveform changed shape. The display's repaint
        gate folds it in so a Warp tweak redraws without an unconditional per-tick repaint. */
    int getWarpSignature() const;

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
    static const std::array<float, kDecimTaps>& decimationKernel();

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
    //
    // TableBuilder (an alias for wavetable::TableBuilder above) does the actual FFT work; the
    // functions below use it to synthesise the built-in tables and to import an audio file.
    // Bodies are in WavetableOscillatorModule.cpp (FRO73, to keep this header under the
    // file-size cap).
    // -------------------------------------------------------------------------
    static float classicShapeHarmonic(int shape, int h);
    static void builtInSpectrum(int tableIndex, int frame, int numFrames, float* cosAmp, float* sinAmp);
    static const std::array<TablePtr, kNumBuiltIns>& builtInTables();
    static std::array<TablePtr, kNumBuiltIns> buildBuiltInTables();

    /** Splits mono sample data into single-cycle frames and builds their mip pyramids.

        `mode` decides how the file is cut:
          - Auto          whole kFrameSize blocks when the file is long enough, else one cycle
          - Fixed256..2048 whole blocks of that size, analysed at their own size
          - SingleCycle   the entire file resampled to one cycle
          - PitchDetect   autocorrelation finds the period, then blocks of that period
          - Spectral      like Auto, but every frame is resynthesised zero-phase */
    static TablePtr buildTableFromSamples(const float* samples, int numSamples, const juce::String& name,
                                          const juce::String& sourcePath, ImportMode mode);
    static bool isPowerOfTwo(int v);
    /** Linear-resamples srcLen samples into exactly dstLen samples. */
    static void resample(const float* samples, int srcLen, float* out, int dstLen);
    /** Estimates the waveform period in samples by normalised autocorrelation.

        Only the first few thousand samples are searched — a wavetable source is periodic from
        the start, and bounding the search keeps the O(n·lag) scan cheap. Falls back to
        kFrameSize when nothing correlates well enough, so a non-periodic file still imports. */
    static int detectPeriod(const float* samples, int numSamples);

    // -------------------------------------------------------------------------
    // Table handoff, message thread <-> audio thread
    // -------------------------------------------------------------------------
    /** Publishes a freshly built table. Reclaims the slot the audio thread retired first,
        so the audio thread never has to free anything. */
    void publishLoadedTable(TablePtr table);

    /** Audio thread: take a published table if one is waiting. Pointer moves only — no
        allocation, no deallocation. Skipped (and retried next block) when the message
        thread is mid-publish or has not reclaimed the previous table yet. */
    void adoptPendingTable();

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
    static float stackSemitones(Stack mode, int u, int unisonCount);

    /** Equal-power stereo placement for unison voice `u`. width 0 collapses to centre. */
    static void unisonPanGains(int u, int unisonCount, float width, float& gainL, float& gainR);

    // The balance pan law itself lives on ModuleBase::panGains — Oscillator and Filter grew
    // Audio L/R blocks of their own in #219 and all three modules must place a signal identically.

    static bool isChannelActive(const juce::AudioBuffer<float>& buffer, int ch, int numSamples);

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
    void fillParameterRamps(int numSamples);

    /** Gathers the voice-independent settings for this block, including the unison gain
        table. Width and Blend land at block rate: they are voicing settings whose CV does
        not need sample accuracy, and keeping them out of the inner loop keeps the
        oversampled path affordable. */
    BlockSettings gatherBlockSettings();

    /** Marks the samples where the Sync input crosses zero going up. Computed once per block
        because every voice needs the same crossings, and the voices render one after another. */
    void buildSyncResets(int cacheLen, SyncMode mode);

    /** Restarts a voice's oscillators at the configured retrigger phase. Phase / Rand /
        Spread are sampled at the note-on instant — they shape the attack, not the sustain. */
    void retriggerVoice(VoiceState& voice, int unisonCount);

    /** Fills freqRamp with a voice's smoothed per-sample frequency in Hz. */
    void fillFrequencyRamp(VoiceState& voice, float targetHz, int numSamples);

    // -------------------------------------------------------------------------
    // Mono mode (voice 0, MIDI driven)
    // -------------------------------------------------------------------------
    void processMonoMode(juce::AudioBuffer<float>& buffer);

    // -------------------------------------------------------------------------
    // Poly mode (voices 0-7, pitch CV in Hz on channels 0-7)
    // -------------------------------------------------------------------------
    void processPolyMode(juce::AudioBuffer<float>& buffer);

    /** Snapshots every shared mod-CV channel for this block. Doing it in one pass keeps the
        two voice modes from drifting apart as jacks are added. */
    void cacheModCV(const juce::AudioBuffer<float>& buffer, bool poly, int cacheLen, int numSamples);

    bool hasCV(int jack) const { return cvActive[(size_t)(jack - 1)]; }
    float cvAt(int jack, int idx) const {
        const size_t slot = (size_t)(jack - 1);
        return cvActive[slot] ? cvCache[slot][(size_t)idx] : 0.0f;
    }

    void pushToVisualBuffer(const juce::AudioBuffer<float>& buffer, int numSamples);

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
