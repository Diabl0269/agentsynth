#include "StepGridModel.h"
#include "Music/NoteNames.h"
#include <algorithm>
#include <cmath>

namespace synth::ui::stepgrid {

namespace {

constexpr float kMinGate = 0.05f;

} // namespace

int laneHeight(SequencerLane lane) noexcept {
    switch (lane) {
    case SequencerLane::Pitch:
        return kPitchLaneHeight;
    case SequencerLane::Slide:
    case SequencerLane::Condition:
        return kMarkLaneHeight;
    default:
        return kBarLaneHeight;
    }
}

juce::String laneName(SequencerLane lane) {
    switch (lane) {
    case SequencerLane::Pitch:
        return "Pitch";
    case SequencerLane::Gate:
        return "Gate";
    case SequencerLane::Velocity:
        return "Velocity";
    case SequencerLane::Chance:
        return "Chance";
    case SequencerLane::Repeat:
        return "Repeat";
    case SequencerLane::Slide:
        return "Slide";
    case SequencerLane::Nudge:
        return "Nudge";
    case SequencerLane::Condition:
        return "Condition";
    case SequencerLane::Filter:
        return "Filter";
    }
    return {};
}

std::vector<SequencerLane> availableLanes(bool poly) {
    std::vector<SequencerLane> lanes;
    for (int i = 0; i < kSequencerLaneCount; ++i)
        if (sequencerLaneAvailable((SequencerLane)i, poly))
            lanes.push_back((SequencerLane)i);
    return lanes;
}

std::uint16_t sanitizeLanes(std::uint16_t mask, bool poly) noexcept {
    std::uint16_t out = 0;
    for (int i = 0; i < kSequencerLaneCount; ++i)
        if ((mask & sequencerLaneBit((SequencerLane)i)) != 0 && sequencerLaneAvailable((SequencerLane)i, poly))
            out = (std::uint16_t)(out | sequencerLaneBit((SequencerLane)i));
    return out;
}

int fixedHeight(bool) { return kToolsRowHeight + kPageStripHeight + kGap + kPadRowHeight + kGap + kChipRowHeight; }

int viewHeightFor(std::uint16_t lanes, bool poly) {
    int height = fixedHeight(poly);
    lanes = sanitizeLanes(lanes, poly);
    for (int i = 0; i < kSequencerLaneCount; ++i)
        if ((lanes & sequencerLaneBit((SequencerLane)i)) != 0)
            height += laneHeight((SequencerLane)i) + kGap;
    return height;
}

int pageCount(int steps) noexcept { return std::max(1, (steps + kColumns - 1) / kColumns); }

juce::String pageLabel(int page, int steps) {
    const int first = page * kColumns + 1;
    const int last = std::min(steps, (page + 1) * kColumns);
    return juce::String(first) + "-" + juce::String(last);
}

juce::String pageDescription(int page, int steps) {
    const int first = page * kColumns + 1;
    const int last = std::min(steps, (page + 1) * kColumns);
    return "Page " + juce::String(page + 1) + ", steps " + juce::String(first) + " to " + juce::String(last);
}

juce::String noteName(int pitch) { return noteNameWithOctave(pitch, 3); }

PitchRange fitPitchRange(const std::vector<int>& pitches) {
    PitchRange range;
    if (pitches.empty()) {
        range.lo = 53;
        range.hi = 65;
        return range;
    }
    const auto [minIt, maxIt] = std::minmax_element(pitches.begin(), pitches.end());
    int lo = *minIt - 1;
    int hi = *maxIt + 1;
    while (hi - lo + 1 < kMinPitchRows) {
        if (hi - lo + 1 < kMinPitchRows)
            --lo;
        if (hi - lo + 1 < kMinPitchRows)
            ++hi;
    }
    if (hi - lo + 1 > kMaxPitchRows)
        lo = hi - kMaxPitchRows + 1;
    if (lo < 0) {
        hi -= lo;
        lo = 0;
    }
    if (hi > 127) {
        lo -= hi - 127;
        hi = 127;
    }
    range.lo = std::max(0, lo);
    range.hi = std::min(127, hi);
    return range;
}

bool isBarLane(SequencerLane lane) noexcept {
    return lane != SequencerLane::Pitch && lane != SequencerLane::Slide && lane != SequencerLane::Condition;
}

float laneNormal(SequencerLane lane, const SequencerStep& step) noexcept {
    switch (lane) {
    case SequencerLane::Gate:
        return std::clamp((step.gate - kMinGate) / (1.0f - kMinGate), 0.0f, 1.0f);
    case SequencerLane::Velocity:
        return std::clamp(((float)step.velocity - 1.0f) / 126.0f, 0.0f, 1.0f);
    case SequencerLane::Chance:
        return std::clamp((float)step.chance / 100.0f, 0.0f, 1.0f);
    case SequencerLane::Repeat:
        return std::clamp(((float)step.repeat - 1.0f) / 7.0f, 0.0f, 1.0f);
    case SequencerLane::Nudge:
        return std::clamp(step.nudge + 0.5f, 0.0f, 1.0f);
    case SequencerLane::Filter:
        return std::clamp(step.filter, 0.0f, 1.0f);
    default:
        return 0.0f;
    }
}

void setLaneNormal(SequencerLane lane, SequencerStep& step, float normal) noexcept {
    normal = std::clamp(normal, 0.0f, 1.0f);
    switch (lane) {
    case SequencerLane::Gate:
        step.gate = std::round((kMinGate + normal * (1.0f - kMinGate)) * 100.0f) / 100.0f;
        break;
    case SequencerLane::Velocity:
        step.velocity = (std::uint8_t)std::clamp((int)std::lround(1.0f + normal * 126.0f), 1, 127);
        break;
    case SequencerLane::Chance:
        step.chance = (std::uint8_t)std::clamp((int)std::lround(normal * 100.0f), 0, 100);
        break;
    case SequencerLane::Repeat:
        step.repeat = (std::uint8_t)std::clamp((int)std::lround(1.0f + normal * 7.0f), 1, 8);
        break;
    case SequencerLane::Nudge:
        step.nudge = std::round((normal - 0.5f) * 100.0f) / 100.0f;
        break;
    case SequencerLane::Filter:
        step.filter = std::round(normal * 100.0f) / 100.0f;
        break;
    default:
        break;
    }
}

juce::String laneValueText(SequencerLane lane, const SequencerStep& step) {
    switch (lane) {
    case SequencerLane::Gate:
        return "gate " + juce::String((int)std::lround(step.gate * 100.0f)) + " percent";
    case SequencerLane::Velocity:
        return "velocity " + juce::String((int)step.velocity);
    case SequencerLane::Chance:
        return "chance " + juce::String((int)step.chance) + " percent";
    case SequencerLane::Repeat:
        return "repeat " + juce::String((int)step.repeat);
    case SequencerLane::Nudge: {
        const int percent = (int)std::lround(step.nudge * 100.0f);
        if (percent == 0)
            return "no nudge";
        return "nudge " + juce::String(std::abs(percent)) + " percent " + (percent < 0 ? "early" : "late");
    }
    case SequencerLane::Filter:
        return "filter " + juce::String((int)std::lround(step.filter * 100.0f)) + " percent";
    case SequencerLane::Slide:
        return step.slide ? "slides into the next step" : "no slide";
    case SequencerLane::Condition:
        return "plays " + conditionText(step);
    default:
        return {};
    }
}

const std::vector<std::pair<int, int>>& conditionChoices() {
    static const std::vector<std::pair<int, int>> choices = [] {
        std::vector<std::pair<int, int>> out{{1, 1}, {1, 2}, {2, 2}};
        for (const int of : {3, 4, 8})
            for (int hit = 1; hit <= of; ++hit)
                out.emplace_back(hit, of);
        return out;
    }();
    return choices;
}

juce::String conditionText(const SequencerStep& step) {
    return juce::String((int)step.condHit) + "/" + juce::String((int)step.condOf);
}

} // namespace synth::ui::stepgrid
