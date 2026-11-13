#pragma once

// Reads, describes and applies the timelineOps `makeSection` and `placeSection` ops. Split out of TimelineOps.cpp
// by concern (that file is at its size cap). Neither op needs the host: a section is part of the document, so both
// run inside the batch's one undo step. Errors carry no "timelineOps[i] (op): " prefix; the caller adds it.

#include "TimelineDoc/TimelineTypes.h"

#include <juce_core/juce_core.h>
#include <map>
#include <vector>

namespace synth {

class TimelineDoc;

/** The checked fields of one `makeSection` op. */
struct MakeSectionOpFields {
    juce::String name;
    double startBeat = 0.0;
    double endBeat = 0.0;
};

/** The checked fields of one `placeSection` op. */
struct PlaceSectionOpFields {
    juce::String name;
    double beat = 0.0;
};

/** What a batch has done with sections so far, for the preview and for the "made earlier in this batch" check. */
struct SectionOpsProgress {
    std::vector<MakeSectionOpFields> made;
    std::vector<PlaceSectionOpFields> placed;
    std::map<juce::String, SectionId> madeIds; // the name an op used -> the section it made
};

/** Reads `op`'s "name" (1..kOpMaxMarkerNameChars characters once trimmed) and its "startBeat" and "endBeat"
 *  (0..kMaxPpqUntrusted, end after start). Empty on success, else the failure text. */
juce::String readMakeSectionOp(juce::DynamicObject& op, MakeSectionOpFields& out);

/** Reads `op`'s "name" and "beat" (0..kMaxPpqUntrusted). Empty on success, else the failure text. */
juce::String readPlaceSectionOp(juce::DynamicObject& op, PlaceSectionOpFields& out);

/** Reads a `makeSection` op and makes the section on `doc`, in the next palette colour. A name already used by a
 *  section of the document, or by an earlier op of the batch, is refused. Empty on success. */
juce::String runMakeSectionOp(juce::DynamicObject& op, TimelineDoc& doc, SectionOpsProgress& progress);

/** Reads a `placeSection` op and places the section of that name (the one made earlier in the batch, else the
 *  document's only section of that name) at `beat`, covering what is there. Refused when the spot overlaps another
 * placement. Empty on success. */
juce::String runPlaceSectionOp(juce::DynamicObject& op, TimelineDoc& doc, SectionOpsProgress& progress);

/** Preview phrases for a batch's section ops (bars counted in 4/4), e.g. `makes section "Chorus" (bars 5-8)` and
 *  `places "Chorus" at bar 9`. Several fold into one phrase each. Empty for none. */
juce::String describeMadeSections(const std::vector<MakeSectionOpFields>& made);
juce::String describePlacedSections(const std::vector<PlaceSectionOpFields>& placed);

} // namespace synth
