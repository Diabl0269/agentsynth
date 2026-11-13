#include "SequencerPattern.h"
#include "SequencerPatternParse.h"

namespace synth {

namespace {

constexpr const char* kChordIds[kSequencerChordCount] = {
    "Unison", "Major",       "Minor",         "Maj7",       "Min7",       "5ths",       "Octs",
    "Random", "Scale Triad", "Scale Seventh", "Scale Sus2", "Scale Sus4", "Scale Add9", "Scale Sixth",
    "Dom7",   "Dim",         "Aug",           "Sus2",       "Sus4",       "Sixth",      "Min6",
    "Add9",   "Dim7",        "HalfDim7",      "MinMaj7"};

constexpr float kMinGate = 0.05f;
constexpr int kMaxNameLength = 64;

// Decimals kept when saving, so the JSON the model and the project file see is "0.35", not "0.3499999940395355".
double roundForJson(float value) { return std::round((double)value * 1000.0) / 1000.0; }

juce::String conditionText(const SequencerStep& step) {
    return juce::String((int)step.condHit) + "/" + juce::String((int)step.condOf);
}

void parseCondition(const juce::var& v, SequencerStep& step) {
    if (!v.isString())
        return;
    const auto text = v.toString();
    const int slash = text.indexOfChar('/');
    if (slash <= 0)
        return;
    int hit = 0;
    int of = 0;
    if (!sequencer_detail::readInt(text.substring(0, slash).trim(), 1, 8, hit) ||
        !sequencer_detail::readInt(text.substring(slash + 1).trim(), 1, 8, of))
        return;
    step.condOf = (std::uint8_t)of;
    step.condHit = (std::uint8_t)std::min(hit, of);
}

// Overlays the recognised fields of `obj` onto `step`; everything else is ignored.
void readStep(const juce::DynamicObject& obj, SequencerStep& step) {
    using namespace sequencer_detail;
    const auto& props = obj.getProperties();
    int i = 0;
    float f = 0.0f;
    if (const auto* p = props.getVarPointer("pitch"); p != nullptr && readInt(*p, 0, 127, i))
        step.pitch = (std::uint8_t)i;
    if (const auto* p = props.getVarPointer("on"); p != nullptr && (p->isBool() || p->isInt() || p->isDouble()))
        step.on = (bool)*p;
    if (const auto* p = props.getVarPointer("gate"); p != nullptr && readFloat(*p, kMinGate, 1.0f, f))
        step.gate = f;
    if (const auto* p = props.getVarPointer("velocity"); p != nullptr && readInt(*p, 1, 127, i))
        step.velocity = (std::uint8_t)i;
    if (const auto* p = props.getVarPointer("chance"); p != nullptr && readInt(*p, 0, 100, i))
        step.chance = (std::uint8_t)i;
    if (const auto* p = props.getVarPointer("repeat"); p != nullptr && readInt(*p, 1, 8, i))
        step.repeat = (std::uint8_t)i;
    if (const auto* p = props.getVarPointer("slide"); p != nullptr && (p->isBool() || p->isInt() || p->isDouble()))
        step.slide = (bool)*p;
    if (const auto* p = props.getVarPointer("nudge"); p != nullptr && readFloat(*p, -0.5f, 0.5f, f))
        step.nudge = f;
    if (const auto* p = props.getVarPointer("condition"))
        parseCondition(*p, step);
    if (const auto* p = props.getVarPointer("filter"); p != nullptr && readFloat(*p, 0.0f, 1.0f, f))
        step.filter = f;
    if (const auto* p = props.getVarPointer("chord"); p != nullptr && p->isString())
        if (const auto chord = chordFromId(p->toString()))
            step.chord = *chord;
}

juce::var stepToVar(const SequencerStep& step, bool poly) {
    const SequencerStep base;
    auto* obj = new juce::DynamicObject();
    if (step.pitch != base.pitch)
        obj->setProperty("pitch", (int)step.pitch);
    if (step.on != base.on)
        obj->setProperty("on", step.on);
    if (step.gate != base.gate)
        obj->setProperty("gate", roundForJson(step.gate));
    if (step.velocity != base.velocity)
        obj->setProperty("velocity", (int)step.velocity);
    if (step.chance != base.chance)
        obj->setProperty("chance", (int)step.chance);
    if (step.repeat != base.repeat)
        obj->setProperty("repeat", (int)step.repeat);
    if (step.slide != base.slide && !poly)
        obj->setProperty("slide", step.slide);
    if (step.nudge != base.nudge)
        obj->setProperty("nudge", roundForJson(step.nudge));
    if (step.condHit != base.condHit || step.condOf != base.condOf)
        obj->setProperty("condition", conditionText(step));
    if (step.filter != base.filter && !poly)
        obj->setProperty("filter", roundForJson(step.filter));
    if (step.chord != base.chord && poly)
        obj->setProperty("chord", chordId(step.chord));
    return juce::var(obj);
}

void readCustomScale(const juce::var& v, SequencerPattern& pattern) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return;
    int mask = 0;
    if (!sequencer_detail::readInt(obj->getProperty("mask"), 0, 0xFFF, mask))
        return;
    pattern.customMask = (std::uint16_t)mask;
    const auto name = obj->getProperty("name");
    pattern.customName = name.isString() ? name.toString().substring(0, kMaxNameLength) : juce::String();
}

// Written only when set; a missing or malformed range loads as none.
void readNoteRange(const juce::var& v, SequencerPattern& pattern) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return;
    int low = 0;
    int high = 0;
    if (!sequencer_detail::readInt(obj->getProperty("low"), 0, 127, low) ||
        !sequencer_detail::readInt(obj->getProperty("high"), 0, 127, high))
        return;
    pattern.noteRange = std::make_pair(std::min(low, high), std::max(low, high));
}

} // namespace

juce::String chordId(SequencerChord chord) {
    const int index = (int)chord;
    return index >= 0 && index < kSequencerChordCount ? juce::String(kChordIds[index]) : juce::String("Unison");
}

std::optional<SequencerChord> chordFromId(const juce::String& id) {
    for (int i = 0; i < kSequencerChordCount; ++i)
        if (id == kChordIds[i])
            return (SequencerChord)i;
    return std::nullopt;
}

SequencerPattern SequencerPattern::makeDefault(bool poly) {
    static constexpr std::uint8_t kMonoPitches[8] = {53, 65, 54, 61, 53, 57, 54, 60};
    static constexpr std::uint8_t kPolyRoots[8] = {48, 52, 55, 60, 48, 55, 52, 60};
    SequencerPattern pattern;
    for (int i = 0; i < kMaxSequencerSteps; ++i)
        pattern.steps[(size_t)i].pitch = poly ? kPolyRoots[i % 8] : kMonoPitches[i % 8];
    return pattern;
}

juce::var SequencerPattern::toVar(bool poly) const {
    const auto defaults = makeDefault(poly);
    int last = kMaxSequencerSteps;
    while (last > 0 && steps[(size_t)last - 1] == defaults.steps[(size_t)last - 1])
        --last;

    juce::Array<juce::var> stepList;
    for (int i = 0; i < last; ++i)
        stepList.add(stepToVar(steps[(size_t)i], poly));

    auto* root = new juce::DynamicObject();
    root->setProperty("version", 1);
    root->setProperty("seed", (juce::int64)seed);
    if (customMask != 0xFFF || customName.isNotEmpty()) {
        auto* custom = new juce::DynamicObject();
        custom->setProperty("name", customName);
        custom->setProperty("mask", (int)customMask);
        root->setProperty("customScale", juce::var(custom));
    }
    if (noteRange.has_value()) {
        auto* range = new juce::DynamicObject();
        range->setProperty("low", noteRange->first);
        range->setProperty("high", noteRange->second);
        root->setProperty("noteRange", juce::var(range));
    }
    root->setProperty("steps", juce::var(stepList));
    return juce::var(root);
}

SequencerPattern SequencerPattern::fromVar(const juce::var& v, bool poly, std::uint32_t fallbackSeed) {
    SequencerPattern pattern = makeDefault(poly);
    pattern.seed = fallbackSeed;
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return pattern;

    double seed = 0.0;
    if (sequencer_detail::readNumber(obj->getProperty("seed"), seed))
        pattern.seed = (std::uint32_t)std::clamp(seed, 0.0, 4294967295.0);
    readCustomScale(obj->getProperty("customScale"), pattern);
    readNoteRange(obj->getProperty("noteRange"), pattern);

    if (const auto* list = obj->getProperty("steps").getArray()) {
        const int count = std::min(list->size(), kMaxSequencerSteps);
        for (int i = 0; i < count; ++i)
            if (auto* stepObj = (*list)[i].getDynamicObject()) {
                pattern.steps[(size_t)i] = SequencerStep{};
                readStep(*stepObj, pattern.steps[(size_t)i]);
            }
    }
    return pattern;
}

} // namespace synth
