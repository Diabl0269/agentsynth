// MixerRowsMotion.cpp: see MixerRowsMotion.h.
#include "MixerRowsMotion.h"

#include "MixerRowLookGlide.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
bool hasKey(const std::vector<ExitEnterListRow>& rows, const juce::String& key) {
    return std::any_of(rows.begin(), rows.end(), [&key](const ExitEnterListRow& r) { return r.key == key; });
}

constexpr const char* kDanglingPrefix = "dangling:";

bool isDangling(const juce::String& key) { return key.startsWith(kDanglingPrefix); }

juce::String moduleOf(const juce::String& key) {
    return key.startsWith(kDanglingPrefix) ? key.fromFirstOccurrenceOf(kDanglingPrefix, false, false) : key;
}

// A module that is a not-connected row on one side and a chain row on the other is one row: both keys become its uuid.
void unifyKinds(std::vector<ExitEnterListRow>& a, std::vector<ExitEnterListRow>& b) {
    const auto flipsAgainst = [](const std::vector<ExitEnterListRow>& rows,
                                 const std::vector<ExitEnterListRow>& other) {
        std::vector<juce::String> keys;
        for (const auto& row : rows)
            if (row.key.startsWith(kDanglingPrefix) && hasKey(other, moduleOf(row.key)))
                keys.push_back(row.key);
        return keys;
    };
    const auto danglingInA = flipsAgainst(a, b);
    const auto danglingInB = flipsAgainst(b, a);
    for (auto& row : a)
        if (std::find(danglingInA.begin(), danglingInA.end(), row.key) != danglingInA.end())
            row.key = moduleOf(row.key);
    for (auto& row : b)
        if (std::find(danglingInB.begin(), danglingInB.end(), row.key) != danglingInB.end())
            row.key = moduleOf(row.key);
}
} // namespace

MixerRowsMotion::MixerRowsMotion(juce::Component& host)
    : host_(host)
    , inserts_(host)
    , sends_(host) {}

namespace {
bool lookGlideForcedFlag = false;
}
void MixerRowsMotion::setLookGlideForcedForTest(bool forced) noexcept { lookGlideForcedFlag = forced; }
bool MixerRowsMotion::lookGlideForced() noexcept { return lookGlideForcedFlag; }

juce::String MixerRowsMotion::insertKey(const synth::MixerInsertEntry& entry) {
    using Kind = synth::MixerInsertEntry::Kind;
    if (entry.kind == Kind::DryPath)
        return "dry:" + entry.uuid + ":" + juce::String(entry.path);
    return entry.kind == Kind::Split ? "split:" + entry.uuid : entry.uuid;
}

juce::String MixerRowsMotion::danglingKey(const synth::MixerDanglingEntry& entry) { return "dangling:" + entry.uuid; }

juce::String MixerRowsMotion::sendKey(const synth::MixerSendEntry& entry) { return juce::String(entry.slot); }

// A section scrolled away from its top is left out: the new list starts at the top, so the rows would not line up.
MixerRowsMotion::Part MixerRowsMotion::rowsIn(const juce::Viewport& frame, std::vector<ExitEnterListRow> listRows) {
    Part part;
    if (!frame.isVisible() || frame.getBounds().isEmpty() || frame.getViewPositionY() != 0)
        return part;
    part.area = frame.getBounds();
    part.rows = std::move(listRows);
    return part;
}

MixerRowsMotion::Part MixerRowsMotion::capture(juce::Component& host, const juce::Viewport& frame,
                                               std::vector<ExitEnterListRow> listRows,
                                               const std::vector<juce::String>& nextKeys) {
    auto part = rowsIn(frame, std::move(listRows));
    if (part.area.isEmpty() || part.rows.empty())
        return part;
    // Compared by module, so a module that only changes kind neither drops nor adds a row.
    std::vector<ExitEnterListRow> next;
    for (const auto& key : nextKeys)
        next.push_back({key, 0.0f, 0.0f});
    auto compared = part.rows;
    unifyKinds(compared, next);
    const bool drops = std::any_of(compared.begin(), compared.end(),
                                   [&next](const ExitEnterListRow& r) { return !hasKey(next, r.key); });
    // A not-connected row that appears while rows go (a module back out from under a folded split) does not stop the
    // picture: the rows that go still shrink away and it shows once they have.
    const bool adds = std::any_of(next.begin(), next.end(), [&compared](const ExitEnterListRow& r) {
        return !hasKey(compared, r.key) && !isDangling(r.key);
    });
    if (drops && !adds)
        part.picture = ExitEnterListMotion::pictureOf(host, part.area, part.pictureScale);
    return part;
}

std::vector<ExitEnterListRow> MixerRowsMotion::kindFlips(const Part& before, const Part& after) {
    std::vector<ExitEnterListRow> flips;
    for (const auto& row : before.rows) {
        const auto uuid = moduleOf(row.key);
        const auto match = std::find_if(after.rows.begin(), after.rows.end(), [&](const ExitEnterListRow& r) {
            return moduleOf(r.key) == uuid && r.key != row.key;
        });
        if (match != after.rows.end())
            flips.push_back({uuid, row.start, row.extent});
    }
    return flips;
}

bool MixerRowsMotion::play(List list, const Part& beforeKinds, const Part& afterKinds, juce::Colour background) {
    auto& motion = motionFor(list);
    motion.finishNow();
    if (beforeKinds.area.isEmpty() || beforeKinds.area != afterKinds.area)
        return false;
    Part before = beforeKinds, after = afterKinds;
    unifyKinds(before.rows, after.rows);

    std::vector<juce::String> added, removed;
    for (const auto& row : after.rows)
        if (!hasKey(before.rows, row.key))
            added.push_back(row.key);
    for (const auto& row : before.rows)
        if (!hasKey(after.rows, row.key))
            removed.push_back(row.key);
    if (added.empty() && removed.empty())
        return false; // nothing came or went
    // A module that leaves its not-connected row for a path of a split (the split row comes in where it was) or comes
    // back out of one (the split's rows go, the module's row shows where they were) is not a row that came and went.
    const auto dangles = [](const juce::String& key) { return isDangling(key); };
    const bool swallowed = !added.empty() && !removed.empty() && std::all_of(removed.begin(), removed.end(), dangles) &&
                           std::none_of(added.begin(), added.end(), dangles);
    const bool surfaced = !added.empty() && !removed.empty() && std::all_of(added.begin(), added.end(), dangles) &&
                          std::none_of(removed.begin(), removed.end(), dangles);
    if (!added.empty() && !removed.empty() && !swallowed && !surfaced)
        return false; // both came and went (lands at once)

    const auto& colours = synth::theme::themeOf(host_).colors;
    ExitEnterListMotion::Job job;
    job.axis = ListAxis::Vertical;
    job.area = after.area;
    job.background = background;
    job.accent = colours.accent;
    const float extent = static_cast<float>(after.area.getHeight());
    if (!removed.empty() && !swallowed) {
        if (before.picture.isNull())
            return false;
        job.picture = before.picture;
        job.pictureScale = before.pictureScale;
        job.items = ExitEnterListPlan::forRemoval(before.rows, after.rows, extent);
    } else {
        job.picture = ExitEnterListMotion::pictureOf(host_, after.area, job.pictureScale);
        job.items = ExitEnterListPlan::forInsertion(after.rows, added, extent);
        if (swallowed)
            for (auto& item : job.items)
                if (const auto it = std::find_if(before.rows.begin(), before.rows.end(),
                                                 [&item](const ExitEnterListRow& r) { return r.key == item.key; });
                    it != before.rows.end() && item.role == ExitEnterListItem::Role::Stay)
                    item.fromStart = it->start; // the rows below were not moved by the module's row going
    }
    return motion.start(std::move(job));
}

void MixerRowsMotion::finishNow() {
    inserts_.finishNow();
    sends_.finishNow();
}

// ---- MixerRowLookGlide (kept in this unit so the small header does not widen what includes the motion) ---------
MixerRowLookGlide::MixerRowLookGlide(juce::Component& owner, std::function<void()> onFrame)
    : tween_(owner) {
    tween_.onDirty = [frame = std::move(onFrame)](juce::Rectangle<int>) {
        if (frame)
            frame();
    };
}

bool MixerRowLookGlide::start(const std::vector<MixerRowLook>& before, const std::vector<MixerRowLook>& resting,
                              bool dimOnly) {
    tween_.clear();
    oldNames_.clear();
    tween_.forceAnimateForTest(MixerRowsMotion::lookGlideForced());
    bool any = false;
    for (int row = 0; row < (int)resting.size(); ++row) {
        const auto& now = resting[(size_t)row];
        const auto old =
            std::find_if(before.begin(), before.end(), [&](const MixerRowLook& r) { return r.key == now.key; });
        if (old == before.end())
            continue;
        if (dimOnly && old->name != now.name)
            continue; // another row now sits in this slot: nothing to ease
        const bool renamed = !dimOnly && old->name != now.name;
        const float oldY = dimOnly ? now.y : old->y;
        if (std::abs(oldY - now.y) < 0.5f && std::abs(old->dim - now.dim) < 0.01f && !renamed)
            continue;
        if (renamed)
            oldNames_[row] = old->name;
        tween_.retarget(row, {old->dim, oldY, renamed ? 1.0f : 0.0f, 0.0f}, {now.dim, now.y, 0.0f, 0.0f}, true);
        any = true;
    }
    if (!tween_.isAnimating())
        oldNames_.clear(); // landed plain (Animations Off, or not on screen)
    return any;
}

void MixerRowLookGlide::finishNow() {
    tween_.clear();
    oldNames_.clear();
}

float MixerRowLookGlide::yOffsetFor(int row) const { return tween_.offsetFor(row).getY(); }

float MixerRowLookGlide::dimFor(int row, float restingDim) const {
    return juce::jlimit(0.0f, 1.0f, restingDim + tween_.offsetFor(row).getX());
}

float MixerRowLookGlide::alphaFor(int row) const { return tween_.alphaFor(row); }

float MixerRowLookGlide::oldNameWeightFor(int row) const {
    return oldNames_.count(row) != 0 ? juce::jlimit(0.0f, 1.0f, tween_.offsetFor(row).getWidth()) : 0.0f;
}

juce::String MixerRowLookGlide::oldNameFor(int row) const {
    const auto it = oldNames_.find(row);
    return it == oldNames_.end() ? juce::String() : it->second;
}

std::vector<MixerRowLook> MixerRowLookGlide::drawn(const std::vector<MixerRowLook>& resting) const {
    auto looks = resting;
    for (int row = 0; row < (int)looks.size(); ++row) {
        auto& look = looks[(size_t)row];
        look.y += yOffsetFor(row);
        look.dim = dimFor(row, look.dim);
        if (oldNameWeightFor(row) > 0.5f)
            look.name = oldNameFor(row);
    }
    return looks;
}

} // namespace synth::ui
