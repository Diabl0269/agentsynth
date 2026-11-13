// ParametricEQModule: parameters, ports and the band model. Rendering is in ParametricEQModuleProcess.cpp, the
// band maths in ParametricEQFilterDesign.cpp. docs/modules/fx-modules.md#parametric-eq-module.
#include "Modules/FX/ParametricEQModule.h"
#include "Modules/FX/ParametricEQ/ParametricEQFilterDesign.h"

#include <algorithm>
#include <cmath>

namespace {

juce::NormalisableRange<float> freqRange() {
    return synth::logFrequencyRange(ParametricEQModule::kMinFreq, ParametricEQModule::kMaxFreq);
}

juce::NormalisableRange<float> gainRange() { return {-ParametricEQModule::kMaxGainDb, ParametricEQModule::kMaxGainDb}; }

juce::NormalisableRange<float> qRange() {
    juce::NormalisableRange<float> range(ParametricEQModule::kMinQ, ParametricEQModule::kMaxQ);
    range.setSkewForCentre(1.0f);
    return range;
}

// Readout formatting. Without these the skewed ranges surface values like "2999.9" and "0.7071". Kept compact: these
// share the standard 50px knob text box with every other module, so "3.0k" fits where "3.00 kHz" would be truncated.
juce::AudioParameterFloatAttributes hzAttributes() {
    return juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
        if (v >= 10000.0f)
            return juce::String(juce::roundToInt(v / 1000.0f)) + "k";
        if (v >= 1000.0f)
            return juce::String(v / 1000.0f, 1) + "k";
        return juce::String(juce::roundToInt(v));
    });
}

juce::AudioParameterFloatAttributes plainAttributes(int decimals) {
    return juce::AudioParameterFloatAttributes().withStringFromValueFunction(
        [decimals](float v, int) { return juce::String(v, decimals); });
}

// The Type choice lists the types in BandType order, so a choice index is the enum value.
juce::StringArray typeChoices() {
    return {"Low Shelf", "Bell", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt"};
}

juce::StringArray slopeChoices() {
    juce::StringArray out;
    for (int slope : synth::eq::kSlopesDbPerOct)
        out.add(juce::String(slope) + " dB/oct");
    return out;
}

void setFloat(juce::AudioParameterFloat* param, float value) {
    if (param != nullptr)
        param->setValueNotifyingHost(param->convertTo0to1(value));
}

void setChoice(juce::AudioParameterChoice* param, int index) {
    if (param != nullptr)
        param->setValueNotifyingHost(param->convertTo0to1((float)juce::jlimit(0, param->choices.size() - 1, index)));
}

} // namespace

// Parameter order is the saved model's contract for positional lookups (ModuleBase::addOutputLevelParameter): the
// four shown bands' On/Freq/Gain/Q, Output, Muted, Mix and Pan keep the indices the four-band module gave them, and
// everything this module grew is appended after Pan: bands 5-24's On/Freq/Gain/Q (band by band), then every band's
// Type and Slope (band1Type, band1Slope, band2Type, ...). An old patch has none of the appended keys, so each keeps
// its default: slots 1-4 default to the types they always had, so the old patch loads and sounds the same.
ParametricEQModule::ParametricEQModule()
    : ModuleBase("Parametric EQ", kTotalInputChannels,
                 2) { // 0-1 audio, 2-14 CV (2-5 the bells' bespoke curves), 15 Mix, 16 Pan
    for (int b = 0; b < kOriginalBands; ++b)
        addBandParameters(b);
    addParameter(outputGainParam =
                     new juce::AudioParameterFloat("outputGain", "Output", gainRange(), 0.0f, plainAttributes(1)));
    addMuteParameter();
    addMixParameter(kMixCvChannel);
    addPanParameter(kPanCvChannel);
    for (int b = kOriginalBands; b < kMaxBands; ++b)
        addBandParameters(b);
    for (int b = 0; b < kMaxBands; ++b)
        addTypeAndSlopeParameters(b);
    enableVisualBuffer(true);
}

// The on/off parameter's display name doubles as the card's row label ("1 Low Shelf", not "Band 1 On").
void ParametricEQModule::addBandParameters(int b) {
    const juce::String id = "band" + juce::String(b + 1);
    const juce::String prefix = "B" + juce::String(b + 1) + " ";
    auto& band = bands[(size_t)b];
    addParameter(band.on = new juce::AudioParameterBool(id + "On", rowLabelFor(b), false));
    addParameter(band.freq = new juce::AudioParameterFloat(id + "Freq", prefix + "Freq", freqRange(), defaultFreqFor(b),
                                                           hzAttributes()));
    addParameter(
        band.gain = new juce::AudioParameterFloat(id + "Gain", prefix + "Gain", gainRange(), 0.0f, plainAttributes(1)));
    addParameter(band.q =
                     new juce::AudioParameterFloat(id + "Q", prefix + "Q", qRange(), kDefaultQ, plainAttributes(2)));
}

void ParametricEQModule::addTypeAndSlopeParameters(int b) {
    const juce::String id = "band" + juce::String(b + 1);
    const juce::String prefix = "B" + juce::String(b + 1) + " ";
    auto& band = bands[(size_t)b];
    addParameter(band.type = new juce::AudioParameterChoice(id + "Type", prefix + "Type", typeChoices(),
                                                            static_cast<int>(bandTypeFor(b))));
    addParameter(band.slope = new juce::AudioParameterChoice(id + "Slope", prefix + "Slope", slopeChoices(),
                                                             synth::eq::kDefaultSlopeIndex));
}

float ParametricEQModule::bandMagnitudeDb(BandType type, float centreHz, float gainDb, float q, float freq,
                                          int slopeDbPerOct) noexcept {
    return synth::eq::bandMagnitudeDb(type, centreHz, gainDb, q, freq, slopeDbPerOct);
}

void ParametricEQModule::writeBiquad(BandType type, float centreHz, float gainDb, float q, double sampleRate,
                                     float* dest) noexcept {
    synth::eq::writeBiquad(type, centreHz, gainDb, q, sampleRate, dest);
}

// The original four bands' channels were added over time (see getInputPortLabel), so they are listed, not computed.
int ParametricEQModule::bandCvChannel(int band, int field) noexcept {
    static constexpr int kFreq[kOriginalBands] = {6, 2, 4, 8};
    static constexpr int kGain[kOriginalBands] = {7, 3, 5, 9};
    if (band < 0 || band >= kMaxBands || field < 0 || field >= kBandCvFields)
        return -1;
    if (band >= kOriginalBands)
        return kFirstBandCvChannel + (band - kOriginalBands) * kBandCvFields + field;
    return field == 0 ? kFreq[band] : field == 1 ? kGain[band] : 10 + band;
}

juce::String ParametricEQModule::getInputPortLabel(int i) const {
    // Appended after the original four (ch2-5) so saved patches that modulate B2/B3 keep working; see
    // docs/modules/modulation.md#every-continuous-parameter-is-a-target. Bands 5-24's channels follow Pan and have no
    // jack of their own.
    const juce::String cv[] = {"B2 Freq", "B2 Gain", "B3 Freq", "B3 Gain", "B1 Freq", "B1 Gain", "B4 Freq", "B4 Gain",
                               "B1 Q",    "B2 Q",    "B3 Q",    "B4 Q",    "Output",  "Mix",     "Pan"};
    if (i >= kFirstBandCvChannel && i < kTotalInputChannels) {
        const int slot = i - kFirstBandCvChannel;
        static constexpr const char* kField[kBandCvFields] = {"Freq", "Gain", "Q"};
        return "B" + juce::String(kOriginalBands + 1 + slot / kBandCvFields) + " " + kField[slot % kBandCvFields];
    }
    return stereoInputLabel(i, kNumCvInputs, cv);
}

// A band's Freq, Gain and Q are reached through the knobs of the card's selected-band row (and the pop-out window's),
// never through a gutter jack of their own, so none of them takes a gutter row whichever band the card shows. Output
// is the one knob that always stays on the card.
std::vector<ModulationTarget> ParametricEQModule::getModulationTargets() const {
    std::vector<ModulationTarget> targets;
    static constexpr const char* kField[kBandCvFields] = {"Freq", "Gain", "Q"};
    for (int b = 0; b < kMaxBands; ++b)
        for (int f = 0; f < kBandCvFields; ++f)
            targets.push_back({"B" + juce::String(b + 1) + " " + kField[f], bandCvChannel(b, f),
                               "band" + juce::String(b + 1) + kField[f], true});
    targets.push_back({"Output", 14, "outputGain"});
    targets.push_back({"Mix", kMixCvChannel, "mix", true});
    targets.push_back({"Pan", kPanCvChannel, "pan", true});
    // Channel order, as the four-band module listed them, so the lists that follow it keep their order.
    std::sort(targets.begin(), targets.end(),
              [](const ModulationTarget& a, const ModulationTarget& b) { return a.channelIndex < b.channelIndex; });
    return targets;
}

// Raw channels past Pan are reached only through a band's knob: they never take a jack, so each maps to a jack index
// of its own beyond the visible ones.
LogicalPort ParametricEQModule::mapInputChannel(int raw) const {
    if (raw < kFirstBandCvChannel)
        return mapStereoPairInput(raw, kNumCvInputs);
    LogicalPort port;
    port.visibleJackIndex = raw;
    port.role = PortRole::ModCV;
    port.isPolyGroupHead = true;
    port.polyVoiceSpan = 1;
    return port;
}

// Values come from the parameters, so the curve tracks an edit even with no audio flowing; a band's live CV value is
// overlaid per parameter, only while that parameter's own jack is driving it.
ParametricEQModule::BandSnapshots ParametricEQModule::getBandSnapshots() const {
    BandSnapshots out{};
    for (int b = 0; b < kMaxBands; ++b) {
        auto& snap = out[(size_t)b];
        const auto& band = bands[(size_t)b];
        snap.type = typeOf(b);
        snap.slopeDbPerOct = slopeOf(b);
        snap.enabled = band.on->get();
        snap.freqHz = freqCVActive[(size_t)b].load(std::memory_order_relaxed)
                          ? resolvedFreq[(size_t)b].load(std::memory_order_relaxed)
                          : band.freq->get();
        snap.gainDb = gainCVActive[(size_t)b].load(std::memory_order_relaxed)
                          ? resolvedGain[(size_t)b].load(std::memory_order_relaxed)
                          : band.gain->get();
        snap.q = qCVActive[(size_t)b].load(std::memory_order_relaxed)
                     ? resolvedQ[(size_t)b].load(std::memory_order_relaxed)
                     : band.q->get();
    }
    return out;
}

int ParametricEQModule::getEnabledBandCount() const {
    int count = 0;
    for (const auto& band : bands)
        if (band.on->get())
            ++count;
    return count;
}

ParametricEQModule::BandType ParametricEQModule::typeOf(int band) const noexcept {
    const int index = bands[(size_t)band].type->getIndex();
    return static_cast<BandType>(juce::jlimit(0, synth::eq::kNumBandTypes - 1, index));
}

int ParametricEQModule::slopeOf(int band) const noexcept {
    return synth::eq::slopeDbFor(bands[(size_t)band].slope->getIndex());
}

ParametricEQModule::BandType ParametricEQModule::getBandType(int band) const {
    return isValidBand(band) ? typeOf(band) : bandTypeFor(band);
}

int ParametricEQModule::getBandSlope(int band) const {
    return isValidBand(band) ? slopeOf(band) : synth::eq::slopeDbFor(synth::eq::kDefaultSlopeIndex);
}

void ParametricEQModule::setBandEnabled(int band, bool enabled) {
    if (isValidBand(band))
        bands[(size_t)band].on->setValueNotifyingHost(enabled ? 1.0f : 0.0f);
}

void ParametricEQModule::setBandFreq(int band, float freqHz) {
    if (isValidBand(band))
        setFloat(bands[(size_t)band].freq, juce::jlimit(kMinFreq, kMaxFreq, freqHz));
}

void ParametricEQModule::setBandGain(int band, float gainDb) {
    if (isValidBand(band))
        setFloat(bands[(size_t)band].gain, juce::jlimit(-kMaxGainDb, kMaxGainDb, gainDb));
}

void ParametricEQModule::setBandQ(int band, float q) {
    if (isValidBand(band))
        setFloat(bands[(size_t)band].q, juce::jlimit(kMinQ, kMaxQ, q));
}

void ParametricEQModule::setBandType(int band, BandType type) {
    if (isValidBand(band))
        setChoice(bands[(size_t)band].type, static_cast<int>(type));
}

void ParametricEQModule::setBandSlope(int band, int slopeDbPerOct) {
    if (!isValidBand(band))
        return;
    int nearest = 0;
    for (int i = 1; i < (int)synth::eq::kSlopesDbPerOct.size(); ++i)
        if (std::abs(synth::eq::kSlopesDbPerOct[(size_t)i] - slopeDbPerOct) <
            std::abs(synth::eq::kSlopesDbPerOct[(size_t)nearest] - slopeDbPerOct))
            nearest = i;
    setChoice(bands[(size_t)band].slope, nearest);
}

// The disabled shown slot whose home frequency is closest on a log axis, so a click down low lands on the low shelf
// and one up top on the high shelf. Only the four home slots: slots 5-24 are picked by findFreeBand.
int ParametricEQModule::findBandForNewPoint(float freqHz) const {
    int best = -1;
    float bestDistance = 0.0f;
    const float target = std::log(juce::jlimit(kMinFreq, kMaxFreq, freqHz));
    for (int b = 0; b < kOriginalBands; ++b) {
        if (bands[(size_t)b].on->get())
            continue;
        const float distance = std::abs(target - std::log(defaultFreqFor(b)));
        if (best < 0 || distance < bestDistance) {
            best = b;
            bestDistance = distance;
        }
    }
    return best;
}

int ParametricEQModule::findFreeBand() const {
    for (int b = 0; b < kMaxBands; ++b)
        if (!bands[(size_t)b].on->get())
            return b;
    return -1;
}

// The slot's type and settings are written before it switches on, so the band never plays its old shape.
int ParametricEQModule::addBand(float freqHz, BandType type) {
    const int band = findFreeBand();
    if (band < 0)
        return -1;
    setBandType(band, type);
    setBandFreq(band, freqHz);
    setBandGain(band, 0.0f);
    setBandQ(band, kDefaultQ);
    setBandEnabled(band, true);
    return band;
}

ParametricEQModule::BandType ParametricEQModule::bandTypeFor(int index) noexcept {
    if (index == 0)
        return BandType::LowShelf;
    if (index == kOriginalBands - 1)
        return BandType::HighShelf;
    return BandType::Peak;
}

juce::String ParametricEQModule::rowLabelFor(int index) {
    switch (bandTypeFor(index)) {
    case BandType::LowShelf:
        return juce::String(index + 1) + " Low Shelf";
    case BandType::HighShelf:
        return juce::String(index + 1) + " High Shelf";
    default:
        break;
    }
    return juce::String(index + 1) + " Peak";
}

// Slots 1-4 keep their four-band homes. Slots 5-24 spread evenly on a log axis from 40 Hz to 16 kHz, so a band added
// there without a frequency still lands somewhere distinct.
float ParametricEQModule::defaultFreqFor(int index) noexcept {
    constexpr float shown[kOriginalBands] = {100.0f, 500.0f, 3000.0f, 8000.0f};
    const int slot = juce::jlimit(0, kMaxBands - 1, index);
    if (slot < kOriginalBands)
        return shown[slot];
    const float t = static_cast<float>(slot - kOriginalBands) / static_cast<float>(kMaxBands - kOriginalBands - 1);
    return 40.0f * std::pow(16000.0f / 40.0f, t);
}

float ParametricEQModule::responseDb(const BandSnapshots& bands, float outputGainDb, float freq) noexcept {
    float total = outputGainDb;
    for (const auto& band : bands)
        if (band.enabled)
            total += synth::eq::bandMagnitudeDb(band.type, band.freqHz, band.gainDb, band.q, freq, band.slopeDbPerOct);
    return total;
}

// Matches FilterModule's cutoff-CV feel, so a single LFO drives both the same way.
float ParametricEQModule::applyFreqCV(float base, float cv) noexcept {
    base = juce::jlimit(kMinFreq, kMaxFreq, base);
    cv = juce::jlimit(-1.0f, 1.0f, cv);
    if (cv == 0.0f)
        return base;
    if (cv > 0.0f)
        return juce::jlimit(kMinFreq, kMaxFreq, base * std::pow(kMaxFreq / base, cv));
    return juce::jlimit(kMinFreq, kMaxFreq, base * std::pow(kMinFreq / base, -cv));
}

float ParametricEQModule::applyGainCV(float baseDb, float cv) noexcept {
    cv = juce::jlimit(-1.0f, 1.0f, cv);
    return juce::jlimit(-kMaxGainDb, kMaxGainDb, baseDb + cv * kMaxGainDb);
}
