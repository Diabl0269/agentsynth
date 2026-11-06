// Concern: sections - the "sections" part of the file format.
//
// Written only when there are sections: {"id","name","colourArgb","lengthBeats","placements":[{"id","startBeat"}]}
// plus "nextSectionId". The reader is strict like the marker reader: any malformed section, a duplicate id (one id
// space for sections and placements), a section with no placement, an overlap or an unknown key rejects the load.
#include "TimelineDoc.h"
#include "TimelineDocInternal.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace synth {

using namespace detail;

namespace {

bool readNumber(const juce::var& v, double& out) {
    if (!(v.isDouble() || v.isInt() || v.isInt64()))
        return false;
    out = static_cast<double>(v);
    return std::isfinite(out);
}

bool readIdValue(const juce::var& v, std::int64_t& out) {
    if (!(v.isInt() || v.isInt64()))
        return false;
    out = static_cast<std::int64_t>(static_cast<juce::int64>(v));
    return out > 0 && out <= TimelineDoc::kMaxIdValue;
}

bool onlyKeys(const juce::DynamicObject& obj, std::initializer_list<const char*> allowed) {
    for (int i = 0; i < obj.getProperties().size(); ++i) {
        const auto key = obj.getProperties().getName(i).toString();
        if (std::none_of(allowed.begin(), allowed.end(), [&](const char* k) { return key == k; }))
            return false;
    }
    return true;
}

bool readPlacement(const juce::var& v, double length, std::set<std::int64_t>& seenIds, SectionId section,
                   std::vector<SectionPlacement>& out) {
    auto* obj = v.getDynamicObject();
    std::int64_t id = 0;
    SectionPlacement placement;
    if (obj == nullptr || !onlyKeys(*obj, {"id", "startBeat"}) || !readIdValue(obj->getProperty("id"), id) ||
        !seenIds.insert(id).second || !readNumber(obj->getProperty("startBeat"), placement.startBeat) ||
        !isFiniteAtOrAfterZero(placement.startBeat) || !std::isfinite(placement.startBeat + length))
        return false;
    placement.id = PlacementId{id};
    placement.section = section;
    out.push_back(placement);
    return true;
}

bool readSection(const juce::var& v, std::set<std::int64_t>& seenIds, std::vector<Section>& sectionsOut,
                 std::vector<SectionPlacement>& placementsOut) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr || !onlyKeys(*obj, {"id", "name", "colourArgb", "lengthBeats", "placements"}))
        return false;

    Section section;
    std::int64_t id = 0;
    const auto nameVar = obj->getProperty("name");
    const auto colourVar = obj->getProperty("colourArgb");
    if (!readIdValue(obj->getProperty("id"), id) || !seenIds.insert(id).second || !nameVar.isString() ||
        !readNumber(obj->getProperty("lengthBeats"), section.lengthBeats) || !isFinitePositive(section.lengthBeats))
        return false;
    section.id = SectionId{id};
    section.name = nameVar.toString();
    if (section.name != section.name.trim() || section.name.isEmpty() ||
        section.name.length() > TimelineDoc::kMaxMarkerTextLength)
        return false;
    if (!colourVar.isVoid()) {
        if (!(colourVar.isInt() || colourVar.isInt64()))
            return false;
        const auto colour = static_cast<std::int64_t>(static_cast<juce::int64>(colourVar));
        if (colour < 0 || colour > 0xffffffffLL)
            return false;
        section.colourArgb = static_cast<juce::uint32>(colour);
    }

    const auto& list = obj->getProperty("placements");
    if (!list.isArray() || list.getArray()->isEmpty())
        return false;
    for (const auto& item : *list.getArray())
        if (!readPlacement(item, section.lengthBeats, seenIds, section.id, placementsOut))
            return false;
    sectionsOut.push_back(std::move(section));
    return true;
}

} // namespace

void TimelineDoc::writeSections(juce::DynamicObject& root) const {
    if (sections.empty())
        return;
    juce::Array<juce::var> sectionVars;
    for (const auto& section : sections) {
        juce::DynamicObject::Ptr s = new juce::DynamicObject();
        s->setProperty("id", static_cast<juce::int64>(section.id.value));
        s->setProperty("name", section.name);
        s->setProperty("colourArgb", static_cast<juce::int64>(section.colourArgb));
        s->setProperty("lengthBeats", section.lengthBeats);
        juce::Array<juce::var> placementVars;
        for (const auto& placement : placements) {
            if (placement.section != section.id)
                continue;
            juce::DynamicObject::Ptr p = new juce::DynamicObject();
            p->setProperty("id", static_cast<juce::int64>(placement.id.value));
            p->setProperty("startBeat", placement.startBeat);
            placementVars.add(juce::var(p.get()));
        }
        s->setProperty("placements", placementVars);
        sectionVars.add(juce::var(s.get()));
    }
    root.setProperty("sections", sectionVars);
    root.setProperty("nextSectionId", static_cast<juce::int64>(nextSectionId));
}

bool TimelineDoc::readSections(const juce::DynamicObject& root, std::vector<Section>& sectionsOut,
                               std::vector<SectionPlacement>& placementsOut, std::int64_t& nextIdOut) {
    nextIdOut = 1;
    const auto counterVar = root.getProperty("nextSectionId");
    if (!counterVar.isVoid()) {
        if (!readIdValue(counterVar, nextIdOut))
            return false;
    }
    const auto listVar = root.getProperty("sections");
    if (listVar.isVoid())
        return true;
    if (!listVar.isArray())
        return false;
    const auto* list = listVar.getArray();
    if (list->size() > kMaxSections)
        return false;

    std::set<std::int64_t> seenIds;
    for (const auto& item : *list)
        if (!readSection(item, seenIds, sectionsOut, placementsOut))
            return false;
    if (static_cast<int>(placementsOut.size()) > kMaxPlacements)
        return false;

    std::stable_sort(placementsOut.begin(), placementsOut.end(),
                     [](const SectionPlacement& a, const SectionPlacement& b) {
                         return a.startBeat != b.startBeat ? a.startBeat < b.startBeat : a.id.value < b.id.value;
                     });
    const auto lengthOf = [&](SectionId id) {
        for (const auto& s : sectionsOut)
            if (s.id == id)
                return s.lengthBeats;
        return 0.0;
    };
    for (size_t i = 1; i < placementsOut.size(); ++i)
        if (placementsOut[i].startBeat < placementsOut[i - 1].startBeat + lengthOf(placementsOut[i - 1].section))
            return false;

    for (const auto& id : seenIds)
        nextIdOut = std::max(nextIdOut, id + 1);
    return true;
}

} // namespace synth
