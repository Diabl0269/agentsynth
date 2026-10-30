// DrumLanes: the drum machine's row table. Names and pitches follow DrumKitModule::drumForNote, which is what the
// drum kit module plays for each note; colours group the rows by instrument family.

#include "DrumLanes.h"

#include "Modules/DrumKit/DrumKitModule.h"

#include <algorithm>
#include <set>

namespace synth::ui {

namespace {

const juce::Colour kKick{0xffe08a3c};
const juce::Colour kSnare{0xffd9c44a};
const juce::Colour kClap{0xffe86a8a};
const juce::Colour kHat{0xff58c4a8};
const juce::Colour kTom{0xff7f8cf0};
const juce::Colour kCymbal{0xffc27adf};
const juce::Colour kCowbell{0xff9ac35a};
const juce::Colour kUnknown{0xff8a93a0};

std::vector<DrumLane> makeDefaults() {
    return {{36, "Kick", kKick},    {38, "Snare", kSnare}, {39, "Clap", kClap},     {42, "Closed hat", kHat},
            {46, "Open hat", kHat}, {45, "Tom", kTom},     {49, "Cymbal", kCymbal}, {56, "Cowbell", kCowbell}};
}

std::vector<DrumLane> makeExtras() {
    return {{37, "Rim", kSnare},          {44, "Pedal hat", kHat},   {41, "Low floor tom", kTom},
            {43, "High floor tom", kTom}, {47, "Low-mid tom", kTom}, {48, "Hi-mid tom", kTom},
            {50, "High tom", kTom},       {51, "Ride", kCymbal},     {35, "Kick 2", kKick},
            {40, "Snare 2", kSnare},      {57, "Crash 2", kCymbal},  {59, "Ride 2", kCymbal}};
}

} // namespace

const std::vector<DrumLane>& defaultDrumLanes() {
    static const std::vector<DrumLane> lanes = makeDefaults();
    return lanes;
}

const std::vector<DrumLane>& extraDrumLanes() {
    static const std::vector<DrumLane> lanes = makeExtras();
    return lanes;
}

DrumLane drumLaneForPitch(int pitch) {
    for (const auto* table : {&defaultDrumLanes(), &extraDrumLanes()})
        for (const auto& lane : *table)
            if (lane.pitch == pitch)
                return lane;
    return {pitch, "Note " + juce::String(pitch), kUnknown};
}

bool KitRows::operator==(const KitRows& other) const {
    if (total != other.total || hiddenCount != other.hiddenCount || hidden != other.hidden ||
        lanes.size() != other.lanes.size())
        return false;
    for (size_t i = 0; i < lanes.size(); ++i)
        if (lanes[i].pitch != other.lanes[i].pitch || lanes[i].name != other.lanes[i].name)
            return false;
    return true;
}

KitRows kitRowsFor(const DrumKitModule& kit) {
    KitRows rows;
    rows.total = kit.padCount();
    std::set<int> shownNotes;
    std::set<int> laneNotes;
    for (int i = 0; i < synth::drums::kMaxPads; ++i) {
        const auto& pad = kit.pad(i);
        if (pad.shown)
            shownNotes.insert(pad.note);
        else
            rows.hidden.insert(pad.note);
        if (i >= rows.total)
            continue;
        rows.hiddenCount += pad.shown ? 0 : 1;
        if (pad.shown && laneNotes.insert(pad.note).second) {
            DrumLane lane = drumLaneForPitch(pad.sound >= 0 ? pad.sound : pad.note);
            lane.pitch = pad.note;
            lane.name = pad.name;
            rows.lanes.push_back(lane);
        }
    }
    for (int note : shownNotes)
        rows.hidden.erase(note);
    return rows;
}

std::vector<DrumLane> visibleDrumLanes(const synth::Clip* clip, bool showAll, const KitRows* kit) {
    std::set<int> held;
    if (clip != nullptr)
        for (const auto& note : clip->notes)
            held.insert(note.pitch);

    if (kit != nullptr) {
        std::vector<DrumLane> lanes = kit->lanes;
        std::set<int> listed;
        for (const auto& lane : lanes)
            listed.insert(lane.pitch);
        for (int pitch : held)
            if (listed.count(pitch) == 0 && kit->hidden.count(pitch) == 0)
                lanes.push_back(drumLaneForPitch(pitch));
        return lanes;
    }

    // Canonical order, a subset of it shown: defaults, then extras, then pitches outside the table by number.
    std::vector<DrumLane> lanes = defaultDrumLanes();
    std::set<int> listed;
    for (const auto& lane : lanes)
        listed.insert(lane.pitch);
    for (const auto& lane : extraDrumLanes()) {
        listed.insert(lane.pitch);
        if (showAll || held.count(lane.pitch) > 0)
            lanes.push_back(lane);
    }
    for (int pitch : held)
        if (listed.count(pitch) == 0)
            lanes.push_back(drumLaneForPitch(pitch));
    return lanes;
}

} // namespace synth::ui
