// DrumLaneSpec: the five lanes' words, ranges and value encodings, and how a lane value reads from and writes to the
// notes. DrumHitLane (one class) draws and edits any of them.

#include "DrumLaneSpec.h"

#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {

constexpr int kRepeatOff = 2;  // repeat 1, no flam
constexpr int kRepeatFlam = 3; // repeat 1 + flam

int repeatValue(int repeat, bool flam) { return repeat * 2 + (flam ? 1 : 0); }
int conditionValue(int hit, int of) { return of * 16 + hit; }

juce::String repeatText(int value) {
    const int repeat = value / 2;
    const bool flam = (value & 1) != 0;
    if (repeat <= 1)
        return flam ? "Flam" : "Off";
    return juce::String(repeat) + (flam ? " + flam" : "");
}

juce::String conditionText(int value) {
    const int of = value / 16;
    const int hit = value % 16;
    return of <= 1 ? "Always" : juce::String(hit) + "/" + juce::String(of);
}

juce::String percentText(int value) { return juce::String(value) + "%"; }

juce::String nudgeText(int value) { return (value > 0 ? "+" : "") + juce::String(value) + "%"; }

std::vector<DrumLaneSpec::Option> repeatOptions() {
    std::vector<DrumLaneSpec::Option> options{{kRepeatOff, "Off"}, {kRepeatFlam, "Flam"}};
    for (int n = 2; n <= 8; ++n)
        options.push_back({repeatValue(n, false), juce::String(n)});
    return options;
}

// Always, 1/2 and 2/2, 1/3 to 3/3, 1/4 to 4/4, then 1/8 to 8/8 (the sequencer's usual cycle lengths).
std::vector<DrumLaneSpec::Option> conditionOptions() {
    std::vector<DrumLaneSpec::Option> options{{conditionValue(1, 1), "Always"}};
    for (const int of : {2, 3, 4, 8})
        for (int hit = 1; hit <= of; ++hit)
            options.push_back({conditionValue(hit, of), juce::String(hit) + "/" + juce::String(of)});
    return options;
}

std::vector<DrumLaneSpec> makeSpecs() {
    std::vector<DrumLaneSpec> specs(kDrumLaneKindCount);
    auto& loud = specs[(size_t)DrumLaneKind::Loudness];
    loud.kind = DrumLaneKind::Loudness;
    loud.name = "Loudness";
    loud.word = "loudness";
    loud.componentId = "drumLoudnessLane";
    loud.tooltip = "Loudness: drag across the bars to set how loud each hit is. Left and Right pick a hit, Up and Down "
                   "change its loudness";
    loud.minValue = 1;
    loud.maxValue = 127;
    loud.scaleMin = 0;
    loud.keyStep = 5;
    loud.shiftStep = 25;

    auto& chance = specs[(size_t)DrumLaneKind::Chance];
    chance.kind = DrumLaneKind::Chance;
    chance.name = "Chance";
    chance.word = "chance";
    chance.componentId = "drumChanceLane";
    chance.tooltip = "Chance: drag across the bars to set how likely each hit is to play. Left and Right pick a hit, "
                     "Up and Down change its chance by 5 (Shift 25)";
    chance.minValue = 0;
    chance.maxValue = 100;
    chance.scaleMin = 0;
    chance.keyStep = 5;
    chance.shiftStep = 25;

    auto& repeat = specs[(size_t)DrumLaneKind::Repeat];
    repeat.kind = DrumLaneKind::Repeat;
    repeat.name = "Repeat";
    repeat.word = "repeat";
    repeat.componentId = "drumRepeatLane";
    repeat.tooltip = "Repeat: drag across the bars to roll a hit (2 to 8 hits in its step) or add a flam. Left and "
                     "Right pick a hit, Up and Down change its repeat";
    repeat.options = repeatOptions();
    repeat.minValue = repeat.options.front().value;
    repeat.maxValue = repeatValue(8, true);

    auto& nudge = specs[(size_t)DrumLaneKind::Nudge];
    nudge.kind = DrumLaneKind::Nudge;
    nudge.name = "Nudge";
    nudge.word = "nudge";
    nudge.componentId = "drumNudgeLane";
    nudge.tooltip = "Nudge: drag across the bars to play each hit earlier (down) or later (up), up to half a step. "
                    "Left and Right pick a hit, Up and Down change its nudge by 5 (Shift 25)";
    nudge.minValue = -50;
    nudge.maxValue = 50;
    nudge.scaleMin = -50;
    nudge.keyStep = 5;
    nudge.shiftStep = 25;
    nudge.bipolar = true;

    auto& cond = specs[(size_t)DrumLaneKind::Condition];
    cond.kind = DrumLaneKind::Condition;
    cond.name = "Condition";
    cond.word = "condition";
    cond.componentId = "drumConditionLane";
    cond.tooltip = "Condition: drag across the bars to play a hit only on some passes of the loop (for example 2/4 "
                   "is the second of every four). Left and Right pick a hit, Up and Down change its condition";
    cond.options = conditionOptions();
    cond.minValue = cond.options.front().value;
    cond.maxValue = conditionValue(8, 8);
    cond.shiftStep = 4;
    return specs;
}

int nearestOption(const DrumLaneSpec& spec, int value) {
    int best = 0;
    for (size_t i = 0; i < spec.options.size(); ++i)
        if (std::abs(spec.options[i].value - value) < std::abs(spec.options[(size_t)best].value - value))
            best = static_cast<int>(i);
    return best;
}

} // namespace

juce::String DrumLaneSpec::textOf(int value) const {
    for (const auto& option : options)
        if (option.value == value)
            return option.label;
    switch (kind) {
    case DrumLaneKind::Chance:
        return percentText(value);
    case DrumLaneKind::Repeat:
        return repeatText(value);
    case DrumLaneKind::Nudge:
        return nudgeText(value);
    case DrumLaneKind::Condition:
        return conditionText(value);
    case DrumLaneKind::Loudness:
        break;
    }
    return juce::String(value);
}

double DrumLaneSpec::fractionOf(int value) const {
    if (isDiscrete())
        return options.size() < 2
                   ? 0.0
                   : static_cast<double>(nearestOption(*this, value)) / static_cast<double>(options.size() - 1);
    const double span = std::max(1, maxValue - scaleMin);
    return juce::jlimit(0.0, 1.0, static_cast<double>(value - scaleMin) / span);
}

int DrumLaneSpec::valueAtFraction(double fraction) const {
    const double f = juce::jlimit(0.0, 1.0, fraction);
    if (isDiscrete())
        return options[(size_t)std::lround(f * static_cast<double>(options.size() - 1))].value;
    return juce::jlimit(minValue, maxValue, static_cast<int>(std::lround(scaleMin + f * (maxValue - scaleMin))));
}

int DrumLaneSpec::stepped(int value, int steps) const {
    if (!isDiscrete())
        return juce::jlimit(minValue, maxValue, value + steps);
    // `below` is the last option at or under the value; an off-list value counts as sitting just above it, so the
    // first step up lands on the next option and the first step down on `below`.
    int below = -1;
    for (size_t i = 0; i < options.size(); ++i)
        if (options[i].value <= value)
            below = static_cast<int>(i);
    const bool exact = below >= 0 && options[(size_t)below].value == value;
    const int from = steps < 0 && !exact ? below + 1 : below;
    return options[(size_t)juce::jlimit(0, static_cast<int>(options.size()) - 1, from + steps)].value;
}

const DrumLaneSpec& drumLaneSpec(DrumLaneKind kind) {
    static const std::vector<DrumLaneSpec> specs = makeSpecs();
    return specs[(size_t)kind];
}

int drumLaneValueOf(DrumLaneKind kind, const drumgrid::CellHits& hits) {
    switch (kind) {
    case DrumLaneKind::Loudness:
        return hits.velocity;
    case DrumLaneKind::Chance:
        return hits.chance;
    case DrumLaneKind::Repeat:
        return repeatValue(hits.repeat, hits.flam);
    case DrumLaneKind::Nudge:
        return static_cast<int>(std::lround(hits.nudge * 100.0));
    case DrumLaneKind::Condition:
        return conditionValue(hits.condHit, hits.condOf);
    }
    return 0;
}

void applyDrumLaneValue(synth::TimelineDoc& doc, DrumLaneKind kind, const std::vector<synth::NoteId>& ids, int value) {
    for (const auto id : ids) {
        switch (kind) {
        case DrumLaneKind::Loudness:
            doc.setNoteVelocity(id, juce::jlimit(1, 127, value));
            break;
        case DrumLaneKind::Chance:
            doc.setNoteChance(id, juce::jlimit(0, 100, value));
            break;
        case DrumLaneKind::Repeat:
            doc.setNoteRepeat(id, juce::jlimit(1, 8, value / 2), (value & 1) != 0);
            break;
        case DrumLaneKind::Nudge:
            doc.setNoteNudge(id, juce::jlimit(-50, 50, value) / 100.0);
            break;
        case DrumLaneKind::Condition:
            doc.setNoteCondition(id, value % 16, value / 16);
            break;
        }
    }
}

} // namespace synth::ui
