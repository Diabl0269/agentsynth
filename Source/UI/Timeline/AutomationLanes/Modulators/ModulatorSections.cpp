// Concern: the sections algebra -- blocks to and from a lane's Hold breakpoints, the span edits a
// gesture makes, and the one doc mutation that writes the result.
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorSections.h"

#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr double kEpsilon = 1.0e-9;
constexpr double kOnThreshold = 0.5;
constexpr int kHold = static_cast<int>(synth::BreakpointCurve::Hold);

synth::AutomationLane::Breakpoint holdPoint(double beat, double value) { return {beat, value, 0.0f, kHold}; }

// 9, 9.5, 9.25: whole numbers without a decimal point, anything else with up to two places.
juce::String barNumber(double bar) {
    if (std::abs(bar - std::round(bar)) < 1.0e-6)
        return juce::String((int)std::llround(bar));
    return juce::String(bar, 2).trimCharactersAtEnd("0");
}

juce::String describeBlock(const SectionBlock& block, double beatsPerBar) {
    const auto first = barNumber(block.start / beatsPerBar + 1.0);
    if (!std::isfinite(block.end))
        return first + " onward";
    const auto last = barNumber(block.end / beatsPerBar);
    return first == last ? first : first + juce::String::fromUTF8("\xE2\x80\x93") + last;
}
} // namespace

SectionBlocks normalisedSections(SectionBlocks blocks) {
    blocks.erase(std::remove_if(blocks.begin(), blocks.end(),
                                [](const SectionBlock& b) { return !(b.end > b.start + kEpsilon); }),
                 blocks.end());
    std::sort(blocks.begin(), blocks.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    SectionBlocks merged;
    for (const auto& block : blocks) {
        if (!merged.empty() && block.start <= merged.back().end + kEpsilon)
            merged.back().end = std::max(merged.back().end, block.end);
        else
            merged.push_back(block);
    }
    return merged;
}

// The kernel holds a lane's first value back to the start of the song, so a lane whose first point is
// already on is on from beat 0, whatever beat that point sits at.
SectionBlocks sectionsFromPoints(const std::vector<synth::AutomationLane::Breakpoint>& points) {
    SectionBlocks blocks;
    bool on = false;
    double start = 0.0;
    for (size_t i = 0; i < points.size(); ++i) {
        const bool pointOn = points[i].value >= kOnThreshold;
        if (pointOn && !on) {
            start = i == 0 ? 0.0 : points[i].beat;
            on = true;
        } else if (!pointOn && on) {
            blocks.push_back({start, points[i].beat});
            on = false;
        }
    }
    if (on)
        blocks.push_back({start, kOpenEnd});
    return normalisedSections(std::move(blocks));
}

std::vector<synth::AutomationLane::Breakpoint> pointsFromSections(const SectionBlocks& blocks) {
    std::vector<synth::AutomationLane::Breakpoint> points;
    const auto normal = normalisedSections(blocks);
    if (normal.empty())
        return points;
    if (normal.front().start > 0.0)
        points.push_back(holdPoint(0.0, 0.0));
    for (const auto& block : normal) {
        points.push_back(holdPoint(std::max(0.0, block.start), 1.0));
        if (std::isfinite(block.end))
            points.push_back(holdPoint(block.end, 0.0));
    }
    return points;
}

SectionBlocks paintedSpan(const SectionBlocks& blocks, double lo, double hi) {
    auto result = blocks;
    result.push_back({lo, hi});
    return normalisedSections(std::move(result));
}

SectionBlocks erasedSpan(const SectionBlocks& blocks, double lo, double hi) {
    SectionBlocks result;
    for (const auto& block : blocks) {
        if (block.end <= lo || block.start >= hi) {
            result.push_back(block);
            continue;
        }
        if (block.start < lo)
            result.push_back({block.start, lo});
        if (block.end > hi)
            result.push_back({hi, block.end});
    }
    return normalisedSections(std::move(result));
}

SectionBlocks resizedBlock(const SectionBlocks& blocks, int index, bool startEdge, double beat, double minLength) {
    if (index < 0 || index >= (int)blocks.size())
        return blocks;
    auto result = blocks;
    auto& block = result[(size_t)index];
    if (startEdge)
        block.start = juce::jlimit(0.0, std::max(0.0, block.end - minLength), beat);
    else
        block.end = std::max(block.start + minLength, beat);
    return normalisedSections(std::move(result));
}

SectionBlocks movedBlock(const SectionBlocks& blocks, int index, double newStart) {
    if (index < 0 || index >= (int)blocks.size())
        return blocks;
    auto result = blocks;
    auto& block = result[(size_t)index];
    const double length = block.end - block.start;
    block.start = std::max(0.0, newStart);
    block.end = block.start + length;
    return normalisedSections(std::move(result));
}

SectionBlocks withoutBlock(const SectionBlocks& blocks, int index) {
    auto result = blocks;
    if (index >= 0 && index < (int)result.size())
        result.erase(result.begin() + index);
    return result;
}

int blockIndexAt(const SectionBlocks& blocks, double beat) {
    for (int i = 0; i < (int)blocks.size(); ++i)
        if (beat >= blocks[(size_t)i].start - kEpsilon && beat < blocks[(size_t)i].end)
            return i;
    return -1;
}

int blockIndexStartingAt(const SectionBlocks& blocks, double start) {
    for (int i = 0; i < (int)blocks.size(); ++i)
        if (std::abs(blocks[(size_t)i].start - start) < 1.0e-6)
            return i;
    return -1;
}

juce::String describeSections(const SectionBlocks& blocks, bool hasLane, double beatsPerBar) {
    if (!hasLane)
        return "on everywhere";
    if (blocks.empty())
        return "off everywhere";
    const double bar = beatsPerBar > 0.0 ? beatsPerBar : 4.0;
    juce::StringArray parts;
    for (const auto& block : blocks)
        parts.add(describeBlock(block, bar));
    const bool single = parts.size() == 1 && !parts[0].containsAnyOf(juce::String::fromUTF8("\xE2\x80\x93 "));
    return (single ? "bar " : "bars ") + parts.joinIntoString(", ");
}

const synth::AutomationLane* sectionsLaneFor(const synth::TimelineDoc& doc, const juce::String& lfoUuid) {
    return lfoUuid.isEmpty() ? nullptr : doc.getLaneForParam(lfoUuid, kSectionsParamId);
}

bool applySections(synth::TimelineDoc& doc, synth::TrackId track, const juce::String& lfoUuid,
                   const SectionBlocks& blocks) {
    const auto* existing = sectionsLaneFor(doc, lfoUuid);
    if (blocks.empty())
        return existing != nullptr && doc.removeLane(existing->id);

    auto id = existing != nullptr ? existing->id : doc.addLane(track, lfoUuid, kSectionsParamId, {0.0f, 1.0f, 1.0f});
    const auto* lane = id.isValid() ? doc.getLane(id) : nullptr;
    if (lane == nullptr)
        return false;
    // The applier only plays a Read lane (a stray Touch/Write would hand the level back to the knob).
    doc.setLaneRecordMode(id, static_cast<int>(synth::LaneRecordMode::Read));
    std::vector<double> removeBeats;
    for (const auto& point : doc.getLane(id)->points)
        removeBeats.push_back(point.beat);
    return doc.editBreakpoints(id, removeBeats, pointsFromSections(blocks));
}

} // namespace synth::ui
