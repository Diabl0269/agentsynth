// Out-of-line bodies for WavetableOscillatorModule (FRO73). These ran inline in the header;
// moved here verbatim (only the class qualifier was added and `inline` dropped) as part of
// getting WavetableOscillatorModule.h under the file-size cap. No behavior change.

#include "WavetableOscillatorModule.h"

// ---- Built-in harmonic specs -------------------------------------------
float WavetableOscillatorModule::classicShapeHarmonic(int shape, int h) {
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

void WavetableOscillatorModule::builtInSpectrum(int tableIndex, int frame, int numFrames, float* cosAmp,
                                                float* sinAmp) {
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

const std::array<WavetableOscillatorModule::TablePtr, WavetableOscillatorModule::kNumBuiltIns>&
WavetableOscillatorModule::builtInTables() {
    // Function-local static: built once, on whichever thread constructs the first
    // WavetableOscillatorModule (always the message thread), then read-only forever.
    static const std::array<TablePtr, kNumBuiltIns> tables = buildBuiltInTables();
    return tables;
}

std::array<WavetableOscillatorModule::TablePtr, WavetableOscillatorModule::kNumBuiltIns>
WavetableOscillatorModule::buildBuiltInTables() {
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

WavetableOscillatorModule::TablePtr
WavetableOscillatorModule::buildTableFromSamples(const float* samples, int numSamples, const juce::String& name,
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

bool WavetableOscillatorModule::isPowerOfTwo(int v) { return v > 0 && (v & (v - 1)) == 0; }

void WavetableOscillatorModule::resample(const float* samples, int srcLen, float* out, int dstLen) {
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

int WavetableOscillatorModule::detectPeriod(const float* samples, int numSamples) {
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

// kNumInputs in: 8 per-voice pitch CV (0-7) + 15 shared mod CV. More outputs than inputs are
// declared for the same reason as OscillatorModule: JUCE's AudioProcessorGraph only makes a
// private copy of an input buffer when inputChan < getTotalNumOutputChannels(). Declaring
// kNumOutputs stops our post-render clear of the CV channels from corrupting a CV source that
// also feeds another node. The channels above the audio blocks are silent pass-throughs.
//
// StereoAudio::Declared: with more than two outputs the Auto shape test cannot see the stereo
// pair — Audio R is the kRightBase block, not ch1. Ships SPLIT.
WavetableOscillatorModule::WavetableOscillatorModule()
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
                                                            {"Off", "Sync", "Bend +", "Bend -", "PWM", "Asym", "Flip",
                                                             "Mirror", "Quantize", "Remap", "Formant"},
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
    addParameter(syncModeParam =
                     new juce::AudioParameterChoice("syncMode", "Sync In", {"Off", "Hard Sync", "Ring Mod", "AM"}, 0));

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

void WavetableOscillatorModule::prepareToPlay(double sampleRate, int samplesPerBlock) {
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

void WavetableOscillatorModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
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

std::vector<ModulationTarget> WavetableOscillatorModule::getModulationTargets() const {
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

LogicalPort WavetableOscillatorModule::mapInputChannel(int raw) const {
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

LogicalPort WavetableOscillatorModule::mapOutputChannel(int raw) const {
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

bool WavetableOscillatorModule::isAutoPromotableModTarget(int dstChannel) const {
    if (polyParam->get())
        return false;
    return ModuleBase::isAutoPromotableModTarget(dstChannel);
}

void WavetableOscillatorModule::getStateInformation(juce::MemoryBlock& destData) {
    juce::ValueTree state("ModuleState");
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            state.setProperty(p->paramID, p->getValue(), nullptr);

    state.setProperty("wavetableFile", getWavetableFile().getFullPathName(), nullptr);
    state.setProperty("wavetableFolder", wavetableFolder.getFullPathName(), nullptr);
    copyXmlToBinary(*state.createXml(), destData);
}

juce::var WavetableOscillatorModule::getExtraState() const {
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

void WavetableOscillatorModule::setExtraState(const juce::var& state) {
    if (auto* obj = state.getDynamicObject()) {
        const juce::String folder = obj->getProperty("wavetableFolder").toString();
        if (folder.isNotEmpty())
            setWavetableFolder(juce::File(folder));

        const juce::String path = obj->getProperty("wavetableFile").toString();
        if (path.isNotEmpty())
            loadWavetableFile(juce::File(path));
    }
}

void WavetableOscillatorModule::setStateInformation(const void* data, int sizeInBytes) {
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

bool WavetableOscillatorModule::loadWavetableFile(const juce::File& file) {
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

void WavetableOscillatorModule::setWavetableFolder(const juce::File& folder) {
    wavetableFolder = folder;
    folderEntries.clear();
    folderIndex = -1;

    if (!folder.isDirectory())
        return;

    for (const auto& entry : juce::RangedDirectoryIterator(folder, /*isRecursive*/ false, "*", juce::File::findFiles)) {
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

bool WavetableOscillatorModule::selectWavetableAt(int index) {
    if (!juce::isPositiveAndBelow(index, folderEntries.size()))
        return false;
    if (!loadWavetableFile(folderEntries[index]))
        return false;
    folderIndex = index;
    return true;
}

bool WavetableOscillatorModule::stepWavetable(int delta) {
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

void WavetableOscillatorModule::getDisplayWaveformAt(std::vector<float>& out, int numPoints, float position) const {
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

const std::array<float, WavetableOscillatorModule::kDecimTaps>& WavetableOscillatorModule::decimationKernel() {
    static const std::array<float, kDecimTaps> kernel = [] {
        std::array<float, kDecimTaps> k{};
        const double cutoff = 0.5 / (double)kOversample; // cycles/sample at the oversampled rate
        const double centre = (double)(kDecimTaps - 1) * 0.5;
        double sum = 0.0;
        for (int i = 0; i < kDecimTaps; ++i) {
            const double x = (double)i - centre;
            const double sinc = (std::abs(x) < 1.0e-9) ? 2.0 * cutoff
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

void WavetableOscillatorModule::publishLoadedTable(TablePtr table) {
    TablePtr unconsumed, reclaimed;
    {
        const juce::SpinLock::ScopedLockType lock(tableLock);
        unconsumed = std::move(pendingTable);
        reclaimed = std::move(retiredTable);
        pendingTable = std::move(table);
    }
    // unconsumed / reclaimed are released here, on this (message) thread.
}

void WavetableOscillatorModule::adoptPendingTable() {
    const juce::SpinLock::ScopedTryLockType lock(tableLock);
    if (!lock.isLocked() || pendingTable == nullptr || retiredTable != nullptr)
        return;

    retiredTable = std::move(audioLoadedTable);
    audioLoadedTable = std::move(pendingTable);
}

float WavetableOscillatorModule::stackSemitones(Stack mode, int u, int unisonCount) {
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

void WavetableOscillatorModule::unisonPanGains(int u, int unisonCount, float width, float& gainL, float& gainR) {
    float pan = 0.0f;
    if (unisonCount > 1)
        pan = width * (2.0f * (float)u / (float)(unisonCount - 1) - 1.0f);
    panGains(pan, gainL, gainR);
}

bool WavetableOscillatorModule::isChannelActive(const juce::AudioBuffer<float>& buffer, int ch, int numSamples) {
    if (ch >= buffer.getNumChannels())
        return false;
    const float* data = buffer.getReadPointer(ch);
    const int checkLen = std::min(numSamples, 64);
    float energy = 0.0f;
    for (int i = 0; i < checkLen; ++i)
        energy += data[i] * data[i];
    return (energy / (float)checkLen) > 1.0e-6f;
}

void WavetableOscillatorModule::fillParameterRamps(int numSamples) {
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

WavetableOscillatorModule::BlockSettings WavetableOscillatorModule::gatherBlockSettings() {
    BlockSettings bs;
    bs.unisonCount = juce::jlimit(1, MAX_UNISON, unisonParam->get());
    bs.warp = (Warp)juce::jlimit(0, (int)Warp::Count - 1, warpParam->getIndex());
    bs.stack = (Stack)juce::jlimit(0, (int)Stack::Count - 1, stackParam->getIndex());
    bs.syncMode = (SyncMode)juce::jlimit(0, (int)SyncMode::Count - 1, syncModeParam->getIndex());
    bs.interpolation = (Interpolation)juce::jlimit(0, (int)Interpolation::Count - 1, interpolationParam->getIndex());
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

void WavetableOscillatorModule::buildSyncResets(int cacheLen, SyncMode mode) {
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

void WavetableOscillatorModule::retriggerVoice(VoiceState& voice, int unisonCount) {
    const float startPhase = juce::jlimit(0.0f, 1.0f, phaseParam->get() / 360.0f + cvAt(kJackPhase, 0));
    const float spread = juce::jlimit(0.0f, 1.0f, spreadParam->get() + cvAt(kJackSpread, 0));
    const float randomAmount = juce::jlimit(0.0f, 1.0f, randomPhaseParam->get() + cvAt(kJackRand, 0));
    voice.resetPhases(startPhase, spread, randomAmount, unisonCount, phaseRandom);
}

void WavetableOscillatorModule::fillFrequencyRamp(VoiceState& voice, float targetHz, int numSamples) {
    voice.smoothedFreq.setTargetValue(targetHz);
    for (int s = 0; s < numSamples; ++s)
        freqRamp[(size_t)s] = voice.smoothedFreq.getNextValue();
}

void WavetableOscillatorModule::processMonoMode(juce::AudioBuffer<float>& buffer) {
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

void WavetableOscillatorModule::processPolyMode(juce::AudioBuffer<float>& buffer) {
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

void WavetableOscillatorModule::cacheModCV(const juce::AudioBuffer<float>& buffer, bool poly, int cacheLen,
                                           int numSamples) {
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

void WavetableOscillatorModule::pushToVisualBuffer(const juce::AudioBuffer<float>& buffer, int numSamples) {
    if (auto* vb = getVisualBuffer()) {
        const float* ch0 = buffer.getReadPointer(0);
        for (int i = 0; i < numSamples; ++i)
            vb->pushSample(ch0[i]);
    }
}

const juce::String* WavetableOscillatorModule::jackLabels() {
    static const juce::String labels[kNumJacks] = {"Pitch", "Position", "Octave", "Coarse", "Fine",   "Level",
                                                   "Warp",  "Phase",    "Rand",   "Detune", "Spread", "Width",
                                                   "Blend", "Sub",      "Pan",    "Sync"};
    return labels;
}

int WavetableOscillatorModule::getNumFrames() const {
    const auto* wt = selectedTableForMessageThread();
    return wt != nullptr ? wt->numFrames : 0;
}

juce::String WavetableOscillatorModule::getWavetableName() const {
    const auto* wt = selectedTableForMessageThread();
    return wt != nullptr ? wt->name : juce::String();
}

int WavetableOscillatorModule::getWarpSignature() const {
    return warpParam->getIndex() * 1024 + (int)std::round(warpAmountParam->get() * 200.0f) * 2 +
           interpolationParam->getIndex();
}
