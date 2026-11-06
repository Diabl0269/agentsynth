// Concern: reading, describing and applying the timelineOps `makeSection` and `placeSection` ops.
#include "TimelineOpsSections.h"

#include "TimelineDoc/TimelineDoc.h"
#include "TimelineOpsTempoMarkers.h"
#include "TimelineValidator.h"
#include "UI/Timeline/SectionColours.h"

#include <cmath>
#include <initializer_list>

namespace synth {

namespace {

// Seconds per beat the section edits split any audio clip at: the app's fallback tempo. The ops here only ever cut
// clips the same batch placed, and a MIDI clip has no source offset to get wrong.
constexpr double kSplitSecondsPerBeat = 0.5;
constexpr double kBeatsPerBar = 4.0;
constexpr int kMaxPreviewedSections = 4;

bool readNumber(const juce::var& v, double& out) {
    if (!(v.isDouble() || v.isInt() || v.isInt64()))
        return false;
    out = static_cast<double>(v);
    return std::isfinite(out);
}

juce::String firstUnknownKey(juce::DynamicObject& o, std::initializer_list<const char*> allowed) {
    for (int i = 0; i < o.getProperties().size(); ++i) {
        const juce::String key = o.getProperties().getName(i).toString();
        bool known = false;
        for (const char* candidate : allowed)
            known = known || key == candidate;
        if (!known)
            return key;
    }
    return {};
}

juce::String numberText(double v) {
    if (v == std::floor(v))
        return juce::String(static_cast<juce::int64>(v));
    return juce::String(v, 2).trimCharactersAtEnd("0");
}

juce::String readName(juce::DynamicObject& op, juce::String& out) {
    const juce::var nameVar = op.getProperty("name");
    if (!nameVar.isString())
        return "needs a string \"name\".";
    out = nameVar.toString().trim();
    if (out.isEmpty())
        return "has an empty \"name\". A section needs a name.";
    if (out.length() > kOpMaxMarkerNameChars)
        return "has a \"name\" of " + juce::String(out.length()) + " characters, exceeding the limit of " +
               juce::String(kOpMaxMarkerNameChars) + ".";
    return {};
}

juce::String readBeat(juce::DynamicObject& op, const char* key, double& out) {
    if (!readNumber(op.getProperty(key), out))
        return juce::String("needs a numeric \"") + key + "\".";
    if (out < 0.0 || out > kMaxPpqUntrusted)
        return juce::String("has a \"") + key + "\" of " + numberText(out) + ". It must be between 0 and " +
               juce::String(kMaxPpqUntrusted) + ".";
    return {};
}

bool madeInBatch(const SectionOpsProgress& progress, const juce::String& name) {
    for (const auto& made : progress.made)
        if (made.name == name)
            return true;
    return false;
}

// The one section of the document with this name; invalid when there is none or several.
SectionId uniqueSectionNamed(const TimelineDoc& doc, const juce::String& name, int& matches) {
    SectionId found;
    matches = 0;
    for (const auto& section : doc.getSections())
        if (section.name == name) {
            found = section.id;
            ++matches;
        }
    return matches == 1 ? found : SectionId{};
}

juce::String barText(double beat) {
    return juce::String(static_cast<juce::int64>(std::floor(beat / kBeatsPerBar)) + 1);
}

juce::String foldPhrase(const juce::String& verb, const juce::String& noun, int count, const juce::StringArray& shown) {
    if (count == 1)
        return verb + " " + shown[0];
    juce::String text = verb + " " + juce::String(count) + " " + noun + " (" + shown.joinIntoString(", ");
    if (count > shown.size())
        text += ", and " + juce::String(count - shown.size()) + " more";
    return text + ")";
}

} // namespace

juce::String readMakeSectionOp(juce::DynamicObject& op, MakeSectionOpFields& out) {
    if (const auto bad = firstUnknownKey(op, {"op", "name", "startBeat", "endBeat"}); bad.isNotEmpty())
        return "has an unknown field \"" + bad +
               "\". A makeSection op accepts only \"name\", \"startBeat\" and \"endBeat\".";
    if (const auto error = readName(op, out.name); error.isNotEmpty())
        return error;
    if (const auto error = readBeat(op, "startBeat", out.startBeat); error.isNotEmpty())
        return error;
    if (const auto error = readBeat(op, "endBeat", out.endBeat); error.isNotEmpty())
        return error;
    if (out.endBeat <= out.startBeat)
        return "has an \"endBeat\" of " + numberText(out.endBeat) + ", not after its \"startBeat\" of " +
               numberText(out.startBeat) + ".";
    return {};
}

juce::String readPlaceSectionOp(juce::DynamicObject& op, PlaceSectionOpFields& out) {
    if (const auto bad = firstUnknownKey(op, {"op", "name", "beat"}); bad.isNotEmpty())
        return "has an unknown field \"" + bad + "\". A placeSection op accepts only \"name\" and \"beat\".";
    if (const auto error = readName(op, out.name); error.isNotEmpty())
        return error;
    return readBeat(op, "beat", out.beat);
}

juce::String runMakeSectionOp(juce::DynamicObject& op, TimelineDoc& doc, SectionOpsProgress& progress) {
    MakeSectionOpFields fields;
    if (const auto error = readMakeSectionOp(op, fields); error.isNotEmpty())
        return error;
    int matches = 0;
    uniqueSectionNamed(doc, fields.name, matches);
    if (matches > 0 || madeInBatch(progress, fields.name))
        return "names \"" + fields.name + "\", which is already a section. Section names must be different.";
    if (static_cast<int>(doc.getSections().size()) >= TimelineDoc::kMaxSections)
        return "would take the timeline past its limit of " + juce::String(TimelineDoc::kMaxSections) + " sections.";
    if (static_cast<int>(doc.getPlacements().size()) >= TimelineDoc::kMaxPlacements)
        return "would take the timeline past its limit of " + juce::String(TimelineDoc::kMaxPlacements) +
               " section placements.";
    if (!doc.makeSection(fields.startBeat, fields.endBeat, fields.name,
                         ui::sectionColourFor(static_cast<int>(doc.getSections().size())), kSplitSecondsPerBeat)
             .isValid())
        return "asks for bars that overlap another section.";
    progress.made.push_back(fields);
    return {};
}

juce::String runPlaceSectionOp(juce::DynamicObject& op, TimelineDoc& doc, SectionOpsProgress& progress) {
    PlaceSectionOpFields fields;
    if (const auto error = readPlaceSectionOp(op, fields); error.isNotEmpty())
        return error;
    int matches = 0;
    const SectionId id = uniqueSectionNamed(doc, fields.name, matches);
    if (matches == 0)
        return "names section \"" + fields.name + "\", which no makeSection op made and the timeline does not have.";
    if (matches > 1)
        return "names section \"" + fields.name + "\", but " + juce::String(matches) + " sections have that name.";
    if (static_cast<int>(doc.getPlacements().size()) >= TimelineDoc::kMaxPlacements)
        return "would take the timeline past its limit of " + juce::String(TimelineDoc::kMaxPlacements) +
               " section placements.";
    double length = 0.0;
    if (const auto* section = doc.getSection(id))
        length = section->lengthBeats;
    if (fields.beat + length > kMaxPpqUntrusted)
        return "places \"" + fields.name + "\" past beat " + juce::String(kMaxPpqUntrusted) + ".";
    for (const auto& placement : doc.getPlacements()) {
        double placedLength = 0.0;
        if (const auto* other = doc.getSection(placement.section))
            placedLength = other->lengthBeats;
        if (fields.beat < placement.startBeat + placedLength && placement.startBeat < fields.beat + length)
            return "places \"" + fields.name + "\" over another section at beat " + numberText(fields.beat) + ".";
    }
    if (!doc.placeSection(id, fields.beat, SectionDropMode::Cover, kSplitSecondsPerBeat).isValid())
        return "could not place the section.";
    progress.placed.push_back(fields);
    return {};
}

juce::String describeMadeSections(const std::vector<MakeSectionOpFields>& made) {
    if (made.empty())
        return {};
    auto one = [](const MakeSectionOpFields& m) {
        const auto lastBar = juce::String(static_cast<juce::int64>(std::ceil(m.endBeat / kBeatsPerBar)));
        const auto firstBar = barText(m.startBeat);
        return m.name + " (bars " + firstBar + (firstBar == lastBar ? "" : "-" + lastBar) + ")";
    };
    juce::StringArray shown;
    for (size_t i = 0; i < made.size() && i < static_cast<size_t>(kMaxPreviewedSections); ++i)
        shown.add(one(made[i]));
    return foldPhrase(made.size() == 1 ? "makes section" : "makes", "sections", static_cast<int>(made.size()), shown);
}

juce::String describePlacedSections(const std::vector<PlaceSectionOpFields>& placed) {
    if (placed.empty())
        return {};
    auto one = [](const PlaceSectionOpFields& p) { return p.name + " at bar " + barText(p.beat); };
    juce::StringArray shown;
    for (size_t i = 0; i < placed.size() && i < static_cast<size_t>(kMaxPreviewedSections); ++i)
        shown.add(one(placed[i]));
    return foldPhrase("places", "placements", static_cast<int>(placed.size()), shown);
}

} // namespace synth
