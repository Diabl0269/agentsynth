// ParametricEQModule: parameters, ports and the band model. Rendering is in ParametricEQModuleProcess.cpp, the
// band maths in ParametricEQFilterDesign.cpp. docs/modules/fx-modules.md#parametric-eq-module.
#include "Modules/FX/ParametricEQModule.h"
#include "Modules/FX/ParametricEQ/ParametricEQFilterDesign.h"
#include "Modules/FX/ParametricEQ/ParametricEQParamIds.h"

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
    : ModuleBase("Parametric EQ", 17, 2) { // 0-1 audio, 2-14 CV (2-5 the bells' bespoke curves), 15 Mix, 16 Pan
    for (int b = 0; b < kShownBands; ++b)
        addBandParameters(b);
    addParameter(outputGainParam =
                     new juce::AudioParameterFloat("outputGain", "Output", gainRange(), 0.0f, plainAttributes(1)));
    addMuteParameter();
    addMixParameter(kMixCvChannel);
    addPanParameter(kPanCvChannel);
    for (int b = kShownBands; b < kMaxBands; ++b)
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

// "band<N><Suffix>": hidden when N is 5-24 (any suffix), or for any band's Type and Slope.
bool ParametricEQModule::isHiddenParamId(const juce::String& paramId) { return synth::eq::isHiddenParamId(paramId); }

float ParametricEQModule::bandMagnitudeDb(BandType type, float centreHz, float gainDb, float q, float freq,
                                          int slopeDbPerOct) noexcept {
    return synth::eq::bandMagnitudeDb(type, centreHz, gainDb, q, freq, slopeDbPerOct);
}

void ParametricEQModule::writeBiquad(BandType type, float centreHz, float gainDb, float q, double sampleRate,
                                     float* dest) noexcept {
    synth::eq::writeBiquad(type, centreHz, gainDb, q, sampleRate, dest);
}

// Parsed rather than listed so the AI and diff code can ask without building a module.
bool synth::eq::isHiddenParamId(const juce::String& paramId) {
    constexpr int kMaxBands = ParametricEQModule::kMaxBands;
    constexpr int kShownBands = ParametricEQModule::kShownBands;
    if (!paramId.startsWith("band"))
        return false;
    const auto rest = paramId.substring(4);
    const auto digits = rest.initialSectionContainingOnly("0123456789");
    if (digits.isEmpty())
        return false;
    const int band = digits.getIntValue();
    const auto suffix = rest.substring(digits.length());
    if (band < 1 || band > kMaxBands)
        return false;
    if (suffix == "Type" || suffix == "Slope")
        return true;
    return band > kShownBands && (suffix == "On" || suffix == "Freq" || suffix == "Gain" || suffix == "Q");
}

juce::String ParametricEQModule::getInputPortLabel(int i) const {
    // Appended after the original four (ch2-5) so saved patches that modulate B2/B3 keep working; see
    // docs/modules/modulation.md#every-continuous-parameter-is-a-target. Bands 5-24 have no jacks.
    const juce::String cv[] = {"B2 Freq", "B2 Gain", "B3 Freq", "B3 Gain", "B1 Freq", "B1 Gain", "B4 Freq", "B4 Gain",
                               "B1 Q",    "B2 Q",    "B3 Q",    "B4 Q",    "Output",  "Mix",     "Pan"};
    return stereoInputLabel(i, kNumCvInputs, cv);
}

std::vector<ModulationTarget> ParametricEQModule::getModulationTargets() const {
    return {{"B2 Freq", 2, "band2Freq"},
            {"B2 Gain", 3, "band2Gain"},
            {"B3 Freq", 4, "band3Freq"},
            {"B3 Gain", 5, "band3Gain"},
            {"B1 Freq", 6, "band1Freq"},
            {"B1 Gain", 7, "band1Gain"},
            {"B4 Freq", 8, "band4Freq"},
            {"B4 Gain", 9, "band4Gain"},
            {"B1 Q", 10, "band1Q"},
            {"B2 Q", 11, "band2Q"},
            {"B3 Q", 12, "band3Q"},
            {"B4 Q", 13, "band4Q"},
            {"Output", 14, "outputGain"},
            {"Mix", kMixCvChannel, "mix", true},
            {"Pan", kPanCvChannel, "pan", true}};
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
// and one up top on the high shelf. Only the four shown slots: the curve cannot show a point in slots 5-24 yet.
int ParametricEQModule::findBandForNewPoint(float freqHz) const {
    int best = -1;
    float bestDistance = 0.0f;
    const float target = std::log(juce::jlimit(kMinFreq, kMaxFreq, freqHz));
    for (int b = 0; b < kShownBands; ++b) {
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
    if (index == kShownBands - 1)
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
    constexpr float shown[kShownBands] = {100.0f, 500.0f, 3000.0f, 8000.0f};
    const int slot = juce::jlimit(0, kMaxBands - 1, index);
    if (slot < kShownBands)
        return shown[slot];
    const float t = static_cast<float>(slot - kShownBands) / static_cast<float>(kMaxBands - kShownBands - 1);
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
