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
        : ModuleBase("Wavetable", 13,
                     13) // 13 in: 8 per-voice pitch CV (0-7) + 5 shared mod CV (8-12).
                         // 13 out declared (not 8) for the same reason as OscillatorModule: JUCE's
                         // AudioProcessorGraph only makes a private copy of an input buffer when
                         // inputChan < getTotalNumOutputChannels(). Declaring 13 stops our
                         // post-render clear of channels 8-12 from corrupting a CV source that
                         // also feeds another node. Channels 8-12 are silent pass-through outputs.
    {
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
            if (msg.isNoteOn())
                voices[0].lastMidiNote = (float)msg.getNoteNumber();
        }

        if (polyParam->get())
            processPolyMode(buffer);
        else
            processMonoMode(buffer);
    }

    // -------------------------------------------------------------------------
    // Ports / modulation
    // -------------------------------------------------------------------------
    std::vector<ModulationTarget> getModulationTargets() const override {
        if (polyParam->get())
            return {{"Position", 8}, {"Octave", 9}, {"Coarse", 10}, {"Fine", 11}, {"Level", 12}};
        return {{"Pitch", 0}, {"Position", 1}, {"Octave", 2}, {"Coarse", 3}, {"Fine", 4}, {"Level", 5}};
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String labels[] = {"Pitch", "Position", "Octave", "Coarse", "Fine", "Level"};
        return (i >= 0 && i < 6) ? labels[i] : ModuleBase::getInputPortLabel(i);
    }
    juce::String getOutputPortLabel(int) const override { return "Audio"; }
    int getVisibleInputPortCount() const override { return 6; }
    int getVisibleOutputPortCount() const override { return 1; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Oscillator; }
    ModuleType getModuleType() const override { return ModuleType::Wavetable; }

    LogicalPort mapInputChannel(int raw) const override {
        LogicalPort p;
        if (polyParam->get()) {
            // Poly: raw 0-7 = per-voice Pitch fan; raw 8-12 = shared mod CV jacks 1-5.
            if (raw >= 0 && raw <= 7) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Pitch;
                p.isPolyGroupHead = (raw == 0);
                p.polyVoiceSpan = (raw == 0) ? 8 : 1;
                return p;
            }
            if (raw >= 8 && raw <= 12) {
                p.visibleJackIndex = raw - 7; // ch8 -> jack 1 (Position) ... ch12 -> jack 5 (Level)
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
        } else if (raw >= 0 && raw <= 5) {
            // Mono: raw 0-5 map straight onto the six visible jacks.
            p.visibleJackIndex = raw;
            p.role = PortRole::ModCV;
            p.isPolyGroupHead = true;
            p.polyVoiceSpan = 1;
            return p;
        }
        return ModuleBase::mapInputChannel(raw);
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
        copyXmlToBinary(*state.createXml(), destData);
    }

    // Non-parameter state that must survive a graph rebuild (undo/redo, preset save/load).
    // This — not getStateInformation — is what AIStateMapper::graphToJSON persists, so the
    // loaded wavetable path has to be published here or it is silently lost on preset load.
    juce::var getExtraState() const override {
        const juce::File file = getWavetableFile();
        if (file == juce::File())
            return {};
        juce::DynamicObject::Ptr state = new juce::DynamicObject();
        state->setProperty("wavetableFile", file.getFullPathName());
        return juce::var(state.get());
    }

    void setExtraState(const juce::var& state) override {
        if (auto* obj = state.getDynamicObject()) {
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

        // Restore the file first: loadWavetableFile() never touches parameters, so the
        // "table" choice restored below stays authoritative.
        const juce::String path = state.getProperty("wavetableFile", juce::String()).toString();
        if (path.isNotEmpty()) {
            const juce::File file(path);
            if (file.existsAsFile())
                loadWavetableFile(file);
        }

        for (auto* param : getParameters())
            if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
                if (state.hasProperty(p->paramID))
                    param->setValue((float)state.getProperty(p->paramID));
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
                                           file.getFullPathName());
        if (table == nullptr)
            return false;

        messageLoadedTable = table;
        publishLoadedTable(std::move(table));
        return true;
    }

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
        for (int i = 0; i < n; ++i)
            out[(size_t)i] = sampleTable(*wt, 0, posFrames, (float)i / (float)n);
    }

    /** Fills `out` with the frame currently under the scan position. Message thread only. */
    void getDisplayWaveform(std::vector<float>& out, int numPoints) const {
        getDisplayWaveformAt(out, numPoints, positionParam->get());
    }

private:
    static constexpr int MAX_VOICES = 8;
    static constexpr int MAX_UNISON = 8;
    static constexpr int kMaxBlock = 4096;

    struct VoiceState {
        float phase[MAX_UNISON]{};
        juce::SmoothedValue<float> smoothedFreq;
        float lastMidiNote = 69.0f;
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
            std::fill(work.begin(), work.end(), 0.0f);
            std::copy_n(cycle, kFrameSize, work.begin());
            fftFor(0).performRealOnlyForwardTransform(work.data(), true);

            cosAmp[0] = sinAmp[0] = 0.0f;
            for (int h = 1; h <= kMaxHarmonic; ++h) {
                cosAmp[h] = work[(size_t)(2 * h)];
                sinAmp[h] = -work[(size_t)(2 * h + 1)];
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

        juce::dsp::FFT& fftFor(int mip) { return *ffts[(size_t)(mipOrder(mip) - kMinOrder)]; }

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

    /** Splits mono sample data into single-cycle frames and builds their mip pyramids. */
    static TablePtr buildTableFromSamples(const float* samples, int numSamples, const juce::String& name,
                                          const juce::String& sourcePath) {
        if (samples == nullptr || numSamples <= 0)
            return nullptr;

        const int available = numSamples / kFrameSize;
        const int sourceFrames = std::max(1, available);
        const int numFrames = std::min(kMaxFrames, sourceFrames);

        TableBuilder builder;
        std::vector<float> cycle((size_t)kFrameSize, 0.0f);
        std::vector<float> cosAmp((size_t)kMaxHarmonic + 1, 0.0f);
        std::vector<float> sinAmp((size_t)kMaxHarmonic + 1, 0.0f);
        auto wt = TableBuilder::allocate(numFrames, name, sourcePath);

        for (int f = 0; f < numFrames; ++f) {
            if (available >= 1) {
                // Evenly spaced source frames, so a long file still spans its morph range.
                const int src = (numFrames > 1) ? (int)((juce::int64)f * (sourceFrames - 1) / (numFrames - 1)) : 0;
                std::copy_n(samples + (size_t)src * (size_t)kFrameSize, kFrameSize, cycle.begin());
            } else {
                // Shorter than one frame: treat the whole file as a single cycle.
                resampleToCycle(samples, numSamples, cycle.data());
            }

            builder.analyseFrame(cycle.data(), cosAmp.data(), sinAmp.data());
            builder.renderFrame(*wt, f, cosAmp.data(), sinAmp.data(), kMaxHarmonic);
        }

        TableBuilder::normalise(*wt);
        return TablePtr(std::move(wt));
    }

    /** Linear-resamples numSamples into exactly kFrameSize samples. */
    static void resampleToCycle(const float* samples, int numSamples, float* out) {
        for (int i = 0; i < kFrameSize; ++i) {
            const float pos = (float)i * (float)numSamples / (float)kFrameSize;
            const int i0 = std::min(numSamples - 1, (int)pos);
            const int i1 = std::min(numSamples - 1, i0 + 1);
            const float frac = pos - (float)i0;
            out[i] = samples[i0] + (samples[i1] - samples[i0]) * frac;
        }
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

    /** Bilinear read: interpolates within the frame (phase) and between frames (scan). */
    static float sampleTable(const Wavetable& wt, int mip, float posFrames, float phase) {
        const int last = wt.numFrames - 1;
        int f0 = (int)posFrames;
        if (f0 < 0)
            f0 = 0;
        if (f0 >= last)
            return readFrame(wt, mip, last, phase);

        const float frac = posFrames - (float)f0;
        const float a = readFrame(wt, mip, f0, phase);
        const float b = readFrame(wt, mip, f0 + 1, phase);
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

    /** Renders one voice into `out`. freqRamp holds the (already smoothed) per-sample base
        frequency in Hz; positionRamp / levelRamp hold the smoothed parameter values that
        the corresponding CV is added to. Scratch arrays only hold `cacheLen` entries, so
        blocks longer than kMaxBlock hold the last cached CV/ramp value for the remainder
        (same behaviour as OscillatorModule). */
    void renderVoice(const Wavetable& wt, VoiceState& voice, float* out, int numSamples, int cacheLen,
                     bool pitchModulated, int unisonCount, float detuneCents) {
        float uniRatio[MAX_UNISON];
        int uniMip[MAX_UNISON];
        const float invSampleRate = 1.0f / (float)currentSampleRate;

        // The ramp is monotone, so its highest-frequency end bounds the harmonic content
        // for the whole block — pick the mip from that end to stay alias-free.
        const float mipFreq = std::max(freqRamp[0], freqRamp[(size_t)(cacheLen - 1)]);

        for (int u = 0; u < unisonCount; ++u) {
            const float cents =
                (unisonCount > 1) ? detuneCents * (2.0f * (float)u / (float)(unisonCount - 1) - 1.0f) : 0.0f;
            uniRatio[u] = std::pow(2.0f, cents / 1200.0f);
            uniMip[u] = selectMip(juce::jlimit(20.0f, 20000.0f, mipFreq) * uniRatio[u] * invSampleRate);
        }

        const float lastFrame = (float)(wt.numFrames - 1);
        const float invUnison = 1.0f / (float)unisonCount;

        for (int s = 0; s < numSamples; ++s) {
            const int idx = std::min(s, cacheLen - 1);

            float freq = freqRamp[(size_t)idx];
            if (pitchModulated) {
                float semis = 0.0f;
                if (hasOctaveCV)
                    semis += std::round(octaveCVCache[(size_t)idx] * 4.0f) * 12.0f;
                if (hasCoarseCV)
                    semis += std::round(coarseCVCache[(size_t)idx] * 12.0f);
                if (hasFineCV)
                    semis += fineCVCache[(size_t)idx];
                if (semis != 0.0f)
                    freq *= std::pow(2.0f, semis / 12.0f);
            }
            const float dt = juce::jlimit(20.0f, 20000.0f, freq) * invSampleRate;

            float position = positionRamp[(size_t)idx];
            if (hasPositionCV)
                position = juce::jlimit(0.0f, 1.0f, position + positionCVCache[(size_t)idx]);
            const float posFrames = position * lastFrame;

            float level = levelRamp[(size_t)idx];
            if (hasLevelCV)
                level = juce::jlimit(0.0f, 1.0f, level + levelCVCache[(size_t)idx]);

            float sample = 0.0f;
            for (int u = 0; u < unisonCount; ++u) {
                const float uniDt = dt * uniRatio[u];
                const int mip = pitchModulated ? selectMip(uniDt) : uniMip[u];
                sample += sampleTable(wt, mip, posFrames, voice.phase[u]);
                voice.phase[u] += uniDt;
                // `while`, not `if`: at very low sample rates a single note can advance more
                // than a full cycle per sample, and one subtraction would leave phase >= 1.
                while (voice.phase[u] >= 1.0f)
                    voice.phase[u] -= 1.0f;
            }

            out[s] = sample * invUnison * level;
        }
    }

    /** Fills positionRamp / levelRamp with this block's smoothed parameter values. */
    void fillParameterRamps(int numSamples) {
        smoothedPosition.setTargetValue(positionParam->get());
        smoothedLevel.setTargetValue(levelParam->get());
        for (int s = 0; s < numSamples; ++s) {
            positionRamp[(size_t)s] = smoothedPosition.getNextValue();
            levelRamp[(size_t)s] = smoothedLevel.getNextValue();
        }
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

        // Mono jack 0 (Pitch) shares channel 0 with the audio output, so — exactly as in
        // OscillatorModule — pitch CV is ignored in mono mode; MIDI drives the pitch.
        hasPositionCV = isChannelActive(buffer, 1, numSamples);
        hasOctaveCV = isChannelActive(buffer, 2, numSamples);
        hasCoarseCV = isChannelActive(buffer, 3, numSamples);
        hasFineCV = isChannelActive(buffer, 4, numSamples);
        hasLevelCV = isChannelActive(buffer, 5, numSamples);

        cacheChannel(buffer, 1, hasPositionCV, positionCVCache, cacheLen);
        cacheChannel(buffer, 2, hasOctaveCV, octaveCVCache, cacheLen);
        cacheChannel(buffer, 3, hasCoarseCV, coarseCVCache, cacheLen);
        cacheChannel(buffer, 4, hasFineCV, fineCVCache, cacheLen);
        cacheChannel(buffer, 5, hasLevelCV, levelCVCache, cacheLen);

        // Clearing channels 1-5 here is safe because their CV was cached above and the
        // module declares 13 outputs, so JUCE hands us a private copy of any CV buffer that
        // is also consumed downstream. Do NOT reduce the output count.
        for (int ch = 0; ch < getTotalNumOutputChannels() && ch < numCh; ++ch)
            buffer.clear(ch, 0, numSamples);

        const Wavetable* wt = audioTable();
        if (wt == nullptr)
            return;

        fillParameterRamps(cacheLen);
        fillFrequencyRamp(voices[0], tunedFrequency(frequencyForMidiNote(voices[0].lastMidiNote)), cacheLen);

        renderVoice(*wt, voices[0], buffer.getWritePointer(0), numSamples, cacheLen,
                    hasOctaveCV || hasCoarseCV || hasFineCV, unisonParam->get(), detuneParam->get());

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

        hasPositionCV = isChannelActive(buffer, 8, numSamples);
        hasOctaveCV = isChannelActive(buffer, 9, numSamples);
        hasCoarseCV = isChannelActive(buffer, 10, numSamples);
        hasFineCV = isChannelActive(buffer, 11, numSamples);
        hasLevelCV = isChannelActive(buffer, 12, numSamples);

        cacheChannel(buffer, 8, hasPositionCV, positionCVCache, cacheLen);
        cacheChannel(buffer, 9, hasOctaveCV, octaveCVCache, cacheLen);
        cacheChannel(buffer, 10, hasCoarseCV, coarseCVCache, cacheLen);
        cacheChannel(buffer, 11, hasFineCV, fineCVCache, cacheLen);
        cacheChannel(buffer, 12, hasLevelCV, levelCVCache, cacheLen);

        // See the note in processMonoMode: safe because the CVs are cached and the module
        // declares 13 outputs.
        for (int ch = 0; ch < getTotalNumOutputChannels() && ch < numCh; ++ch)
            buffer.clear(ch, 0, numSamples);

        const Wavetable* wt = audioTable();
        if (wt == nullptr)
            return;

        fillParameterRamps(cacheLen);

        const bool pitchModulated = hasOctaveCV || hasCoarseCV || hasFineCV;
        const int unisonCount = unisonParam->get();
        const float detuneCents = detuneParam->get();

        for (int v = 0; v < MAX_VOICES && v < numCh; ++v) {
            float freq = pitchCV[(size_t)v];
            if (freq < 20.0f && v == 0)
                freq = frequencyForMidiNote(voices[0].lastMidiNote); // MIDI fallback for voice 0
            if (freq < 20.0f)
                continue;

            fillFrequencyRamp(voices[v], tunedFrequency(freq), cacheLen);
            renderVoice(*wt, voices[v], buffer.getWritePointer(v), numSamples, cacheLen, pitchModulated, unisonCount,
                        detuneCents);
        }

        // Shared CV channels must not leak downstream as audio.
        for (int ch = MAX_VOICES; ch < numCh; ++ch)
            buffer.clear(ch, 0, numSamples);

        pushToVisualBuffer(buffer, numSamples);
    }

    static void cacheChannel(const juce::AudioBuffer<float>& buffer, int ch, bool active,
                             std::array<float, kMaxBlock>& cache, int numSamples) {
        if (active && ch < buffer.getNumChannels())
            std::copy_n(buffer.getReadPointer(ch), numSamples, cache.data());
        else
            std::fill_n(cache.data(), numSamples, 0.0f);
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

    // Table handoff
    juce::SpinLock tableLock;    // guards pendingTable / retiredTable only
    TablePtr pendingTable;       // message thread -> audio thread
    TablePtr retiredTable;       // audio thread -> message thread
    TablePtr audioLoadedTable;   // audio thread only
    TablePtr messageLoadedTable; // message thread only (UI queries, state save)

    // Pre-allocated scratch — no heap traffic on the audio thread
    std::array<float, kMaxBlock> positionCVCache{};
    std::array<float, kMaxBlock> octaveCVCache{};
    std::array<float, kMaxBlock> coarseCVCache{};
    std::array<float, kMaxBlock> fineCVCache{};
    std::array<float, kMaxBlock> levelCVCache{};
    std::array<float, kMaxBlock> positionRamp{};
    std::array<float, kMaxBlock> levelRamp{};
    std::array<float, kMaxBlock> freqRamp{};
    std::array<float, MAX_VOICES> pitchCV{};

    bool hasPositionCV = false;
    bool hasOctaveCV = false;
    bool hasCoarseCV = false;
    bool hasFineCV = false;
    bool hasLevelCV = false;

    juce::AudioParameterChoice* tableParam = nullptr;
    juce::AudioParameterFloat* positionParam = nullptr;
    juce::AudioParameterInt* octaveParam = nullptr;
    juce::AudioParameterInt* coarseParam = nullptr;
    juce::AudioParameterFloat* fineParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;
    juce::AudioParameterBool* polyParam = nullptr;
    juce::AudioParameterInt* unisonParam = nullptr;
    juce::AudioParameterFloat* detuneParam = nullptr;

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
