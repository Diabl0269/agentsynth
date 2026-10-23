// MixerRowsMotion.cpp: see MixerRowsMotion.h.
#include "MixerRowsMotion.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>

namespace synth::ui {

namespace {
bool hasKey(const std::vector<ExitEnterListRow>& rows, const juce::String& key) {
    return std::any_of(rows.begin(), rows.end(), [&key](const ExitEnterListRow& r) { return r.key == key; });
}
} // namespace

MixerRowsMotion::MixerRowsMotion(juce::Component& host)
    : host_(host)
    , inserts_(host)
    , sends_(host) {}

juce::String MixerRowsMotion::insertKey(const synth::MixerInsertEntry& entry) {
    using Kind = synth::MixerInsertEntry::Kind;
    if (entry.kind == Kind::DryPath)
        return "dry:" + entry.uuid + ":" + juce::String(entry.path);
    return entry.kind == Kind::Split ? "split:" + entry.uuid : entry.uuid;
}

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
    const bool drops = std::any_of(part.rows.begin(), part.rows.end(), [&nextKeys](const ExitEnterListRow& r) {
        return std::find(nextKeys.begin(), nextKeys.end(), r.key) == nextKeys.end();
    });
    const bool adds = std::any_of(nextKeys.begin(), nextKeys.end(),
                                  [&part](const juce::String& key) { return !hasKey(part.rows, key); });
    if (drops && !adds)
        part.picture = ExitEnterListMotion::pictureOf(host, part.area, part.pictureScale);
    return part;
}

bool MixerRowsMotion::play(List list, const Part& before, const Part& after, juce::Colour background) {
    auto& motion = motionFor(list);
    motion.finishNow();
    if (before.area.isEmpty() || before.area != after.area)
        return false;

    std::vector<juce::String> added, removed;
    for (const auto& row : after.rows)
        if (!hasKey(before.rows, row.key))
            added.push_back(row.key);
    for (const auto& row : before.rows)
        if (!hasKey(after.rows, row.key))
            removed.push_back(row.key);
    if (added.empty() == removed.empty())
        return false; // nothing came or went, or both did (lands at once)

    const auto& colours = synth::theme::themeOf(host_).colors;
    ExitEnterListMotion::Job job;
    job.axis = ListAxis::Vertical;
    job.area = after.area;
    job.background = background;
    job.accent = colours.accent;
    const float extent = static_cast<float>(after.area.getHeight());
    if (!removed.empty()) {
        if (before.picture.isNull())
            return false;
        job.picture = before.picture;
        job.pictureScale = before.pictureScale;
        job.items = ExitEnterListPlan::forRemoval(before.rows, after.rows, extent);
    } else {
        job.picture = ExitEnterListMotion::pictureOf(host_, after.area, job.pictureScale);
        job.items = ExitEnterListPlan::forInsertion(after.rows, added, extent);
    }
    return motion.start(std::move(job));
}

void MixerRowsMotion::finishNow() {
    inserts_.finishNow();
    sends_.finishNow();
}

} // namespace synth::ui
