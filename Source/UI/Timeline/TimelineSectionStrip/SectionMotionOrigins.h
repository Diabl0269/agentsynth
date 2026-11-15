// SectionMotionOrigins -- which side each section block that arrives grows from and each block that leaves shrinks
// toward. The Sections strip and the clip lane each own one and feed it the same doc changes, so a block and the
// clips inside it move as one picture. docs/timeline/sections.md ("Motion") states the rule.
#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/MotionOrigin.h"
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace synth::ui {

/** One placement as a span of beats. */
struct PlacementSpan {
    std::int64_t id = 0;
    std::int64_t section = 0;
    double start = 0.0;
    double end = 0.0;
};
std::vector<PlacementSpan> placementSpansOf(const synth::TimelineDoc& doc);

/** Where one block grows from or shrinks to: the origin, the beat it is anchored at (the edge of the run of blocks
 *  that move together, or the block's own middle for Centre) and the block's own span. */
struct SectionMotionAnchor {
    MotionOrigin origin = MotionOrigin::Centre;
    double beat = 0.0;
    double start = 0.0;
    double end = 0.0;
};

class SectionMotionOrigins {
public:
    struct Moves {
        std::map<std::int64_t, SectionMotionAnchor> grows; // placements in `after` only
        std::map<std::int64_t, SectionMotionAnchor> exits; // placements in `before` only
        /** The anchor of the arriving (or leaving) block whose span holds `beat`, or none. */
        const SectionMotionAnchor* growAt(double beat) const;
        const SectionMotionAnchor* exitAt(double beat) const;
    };

    /** The origin of every block between two snapshots. `restored` is an undo or redo; `madeFrom` is the side a
     *  make-drag was anchored on, for the blocks it adds. */
    Moves diff(const std::vector<PlacementSpan>& before, const std::vector<PlacementSpan>& after, bool restored,
               std::optional<MotionOrigin> madeFrom = {});
    /** Forgets every remembered origin (a load: nothing it brings is the reverse of an edit). */
    void clear() { remembered_.clear(); }

private:
    std::map<std::int64_t, MotionOrigin> remembered_;
};

} // namespace synth::ui
