// Concern: reading the retired sections format back into blocks, and the amount-lane points that replay
// those blocks, for the project-load migration.
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorSections.h"

#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr double kEpsilon = 1.0e-9;
constexpr double kOnThreshold = 0.5;
constexpr int kHold = static_cast<int>(synth::BreakpointCurve::Hold);

synth::AutomationLane::Breakpoint holdPoint(double beat, double value) { return {beat, value, 0.0f, kHold}; }
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

// The same edges the sections lane had, so the migrated modulator starts and stops on exactly the beats it
// did: the LFO used to be silenced (level 0) outside a block, and now its amount is 0 there instead.
std::vector<synth::AutomationLane::Breakpoint> amountPointsFromSections(const SectionBlocks& blocks, double amount) {
    std::vector<synth::AutomationLane::Breakpoint> points;
    const auto normal = normalisedSections(blocks);
    if (normal.empty() || normal.front().start > 0.0)
        points.push_back(holdPoint(0.0, 0.0));
    for (const auto& block : normal) {
        points.push_back(holdPoint(std::max(0.0, block.start), amount));
        if (std::isfinite(block.end))
            points.push_back(holdPoint(block.end, 0.0));
    }
    return points;
}

const synth::AutomationLane* sectionsLaneFor(const synth::TimelineDoc& doc, const juce::String& lfoUuid) {
    return lfoUuid.isEmpty() ? nullptr : doc.getLaneForParam(lfoUuid, kSectionsParamId);
}

} // namespace synth::ui
