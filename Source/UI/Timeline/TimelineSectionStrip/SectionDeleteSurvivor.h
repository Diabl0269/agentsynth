#pragma once

// SectionDeleteSurvivor.h: which section block takes the selection and the focus ring when others are deleted (the
// bug kind delete-drops-focus: a delete must leave the keys somewhere, never on the whole strip). The first block
// after the last deleted one by start beat, else the last remaining block before it, else none (the strip
// is empty, and the whole-strip ring is right then). A multi-delete picks one survivor.

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <algorithm>
#include <vector>

namespace synth::ui {

struct SectionDeleteSurvivor {
    static synth::PlacementId pick(const synth::TimelineDoc& doc, const std::vector<synth::PlacementId>& deleted) {
        const auto isDeleted = [&deleted](synth::PlacementId id) {
            return std::find(deleted.begin(), deleted.end(), id) != deleted.end();
        };
        bool any = false;
        double lastStart = 0.0;
        for (const auto& placement : doc.getPlacements()) {
            if (!isDeleted(placement.id))
                continue;
            lastStart = any ? std::max(lastStart, placement.startBeat) : placement.startBeat;
            any = true;
        }
        if (!any)
            return {};
        const synth::SectionPlacement* after = nullptr;
        const synth::SectionPlacement* before = nullptr;
        for (const auto& placement : doc.getPlacements()) {
            if (isDeleted(placement.id))
                continue;
            if (placement.startBeat > lastStart && (after == nullptr || placement.startBeat < after->startBeat))
                after = &placement;
            if (placement.startBeat < lastStart && (before == nullptr || placement.startBeat > before->startBeat))
                before = &placement;
        }
        if (after != nullptr)
            return after->id;
        return before != nullptr ? before->id : synth::PlacementId{};
    }
};

} // namespace synth::ui
