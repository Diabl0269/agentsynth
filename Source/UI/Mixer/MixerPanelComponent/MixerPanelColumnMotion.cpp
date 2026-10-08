// Concern: MixerPanelComponent's delete and undo motion of the strip columns -- the picture taken before a removal,
// which channels a rebuild added or removed, and the overlay (ExitEnterListMotion, horizontal) that shrinks a removed
// column away, closes the gap, and on undo opens the gap, grows the column back and fades an outline around it. Only
// the scrolling group moves; a pinned zone lands at once. docs/layout/animation.md "Delete and undo animation".
#include "AppUndoManager.h"
#include "MixerPanelComponent.h"
#include "MixerPanelInternal.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr int kColumnPitch = kMixerColumnWidth + kMixerColumnGap;

void addUnique(std::vector<juce::String>& list, const juce::String& key) {
    if (std::find(list.begin(), list.end(), key) == list.end())
        list.push_back(key);
}
} // namespace

// The scrolling group's columns across the picture of the viewport, scrolled as the view is. A row is a column and the
// gap after it, so the columns after one that comes or goes move by a whole pitch.
std::vector<ExitEnterListRow> MixerPanelComponent::scrollingColumnRows() const {
    std::vector<ExitEnterListRow> rows;
    const int scroll = viewport_.getViewPositionX();
    for (const auto& entry : columnEntries_)
        if (entry.zone == synth::MixerZone::Scrolling && entry.component != nullptr && entry.component->isVisible())
            rows.push_back({entry.channelId, static_cast<float>(entry.zoneIndex * kColumnPitch - scroll),
                            static_cast<float>(kColumnPitch)});
    return rows;
}

void MixerPanelComponent::noteColumnsLeaving() {
    columnMotion_.finishNow();
    pendingColumnPicture_.reset();
    columnsAdded_.clear();
    columnsRemoved_.clear();
    if (!canAnimateColumnMotion() || viewport_.getBounds().isEmpty())
        return;
    ColumnPicture picture;
    picture.rows = scrollingColumnRows();
    picture.image = ExitEnterListMotion::pictureOf(*this, viewport_.getBounds(), picture.scale);
    pendingColumnPicture_ = std::move(picture);
}

// Called by rebuild(): which strip and bus columns it had to build new and which it let go. Only a delete (a picture is
// waiting) or an undo/redo is animated; a column added any other way lands at once.
void MixerPanelComponent::noteColumnSetChange(const std::vector<juce::String>& added,
                                              const std::vector<juce::String>& removed) {
    if (added.empty() && removed.empty())
        return;
    columnMotion_.finishNow();
    if (!pendingColumnPicture_ && (undoManager_ == nullptr || !undoManager_->isRestoring()))
        return;
    for (const auto& key : added)
        addUnique(columnsAdded_, key);
    for (const auto& key : removed)
        addUnique(columnsRemoved_, key);
}

void MixerPanelComponent::finishColumnChange() {
    auto pending = std::exchange(pendingColumnPicture_, std::nullopt);
    auto added = std::exchange(columnsAdded_, {});
    auto removed = std::exchange(columnsRemoved_, {});
    if (!canAnimateColumnMotion() || viewport_.getBounds().isEmpty())
        return;

    const auto after = scrollingColumnRows();
    auto inRows = [](const std::vector<ExitEnterListRow>& rows, const juce::String& key) {
        return std::any_of(rows.begin(), rows.end(), [&key](const ExitEnterListRow& r) { return r.key == key; });
    };
    // A pinned column, or one that was never shown, is not part of the motion.
    if (pending)
        removed.erase(std::remove_if(removed.begin(), removed.end(),
                                     [&](const juce::String& k) { return !inRows(pending->rows, k); }),
                      removed.end());
    added.erase(std::remove_if(added.begin(), added.end(), [&](const juce::String& k) { return !inRows(after, k); }),
                added.end());
    if (added.empty() == removed.empty())
        return; // nothing came or went, or both did (lands at once)

    const auto area = viewport_.getBounds();
    const auto& colours = synth::theme::themeOf(*this).colors;
    ExitEnterListMotion::Job job;
    job.axis = ListAxis::Horizontal;
    job.area = area;
    job.background = colours.bg0;
    job.accent = colours.accent;
    const float extent = static_cast<float>(area.getWidth());
    if (!removed.empty()) {
        if (!pending)
            return;
        job.picture = pending->image;
        job.pictureScale = pending->scale;
        job.items = ExitEnterListPlan::forRemoval(pending->rows, after, extent);
    } else {
        job.picture = ExitEnterListMotion::pictureOf(*this, area, job.pictureScale);
        job.items = ExitEnterListPlan::forInsertion(after, added, extent);
    }
    columnMotion_.start(std::move(job));
}

} // namespace synth::ui
