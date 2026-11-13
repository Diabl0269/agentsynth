#pragma once

// MakeSectionKey.h: what the Make Section key (Shift+Cmd+J) makes a section from, whichever timeline surface has focus:
// the Range tool's span when one is drawn, else the loop while looping is on, else nothing. The panel's key handler
// acts on it and the Sections strip's tooltip names the key only when it is not empty, so the hint and the key always
// agree.

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Transport/TransportService.h"
#include "UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h"
#include <optional>

namespace synth::ui {

struct MakeSectionSpan {
    double startBeat = 0.0;
    double endBeat = 0.0;
    bool fromRange = false; // the Range tool's span (else the loop)
    bool blocked = false;   // a section is already placed over part of it, so the key would make nothing
    juce::String source() const { return fromRange ? "the selected bars" : "the loop"; }
};

/** The span the key would make a section from, or nullopt with no range and looping off (or a loop with no length).
 *  `blocked` says the span overlaps a section already placed, which the doc refuses. */
inline std::optional<MakeSectionSpan> makeSectionKeySpan(const TimelineClipLaneArea& lanes,
                                                         const synth::TransportService* transport,
                                                         const synth::TimelineDoc* doc) {
    if (doc == nullptr)
        return std::nullopt;
    std::optional<MakeSectionSpan> span;
    if (const auto range = lanes.getRangeSpan(); range && range->endBeat > range->startBeat)
        span = MakeSectionSpan{range->startBeat, range->endBeat, true};
    else if (transport != nullptr) {
        const auto snap = transport->getPositionSnapshot();
        if (snap.looping && snap.loopEndPpq > snap.loopStartPpq && snap.loopStartPpq >= 0.0)
            span = MakeSectionSpan{snap.loopStartPpq, snap.loopEndPpq, false};
    }
    if (!span)
        return std::nullopt;
    for (const auto& placement : doc->getPlacements())
        if (const auto* section = doc->getSection(placement.section))
            if (placement.startBeat < span->endBeat && span->startBeat < placement.startBeat + section->lengthBeats)
                span->blocked = true;
    return span;
}

} // namespace synth::ui
