// Concern: MixerPanelComponent's delete, add and undo motion of the strip columns -- the picture taken before a
// removal, which channels a rebuild added or removed, and the overlay (ExitEnterListMotion, horizontal) that shrinks a
// removed column away, closes the gap, and on undo opens the gap, grows the column back and fades an outline around it.
// Only the scrolling group moves; a pinned zone lands at once. docs/layout/animation.md "Delete and undo animation".
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

// Called at the top of rebuildFrom(), while the old columns still stand: works out what the new snapshot adds and
// removes among the shown strip and bus columns and prepares the motion for a change that is a single kind of
// move (a removal needs the picture of the old columns, so it is taken now). A load (the doc was replaced), the
// catch-up after the panel was hidden, a first fill with several columns, and a clear leave it alone and land at once.
void MixerPanelComponent::noteColumnSetComing(const synth::MixerSnapshot& snapshot) {
    autoColumnFinish_ = false;
    const bool restoring = undoManager_ != nullptr && undoManager_->isRestoring();
    const bool loaded = doc_ != nullptr && doc_->getReplaceCount() != replaceCountSeen_ && !restoring;
    if (doc_ != nullptr)
        replaceCountSeen_ = doc_->getReplaceCount();
    if (loaded || catchingUp_ || !canAnimateColumnMotion() || viewport_.getBounds().isEmpty() || viewDoc_ == nullptr)
        return;

    std::vector<juce::String> existing, incoming;
    for (const auto& entry : columnEntries_)
        if (entry.kind == ColumnEntry::Kind::Strip)
            existing.push_back(entry.uuid);
    for (const auto& column : snapshot.columns)
        if ((column.kind == synth::MixerColumn::Kind::Strip || column.kind == synth::MixerColumn::Kind::Bus) &&
            !viewDoc_->isHidden(channelIdFor(column)))
            incoming.push_back(column.uuid);
    auto contains = [](const std::vector<juce::String>& list, const juce::String& key) {
        return std::find(list.begin(), list.end(), key) != list.end();
    };
    const auto added = static_cast<size_t>(std::count_if(
        incoming.begin(), incoming.end(), [&](const juce::String& key) { return !contains(existing, key); }));
    const auto removed = static_cast<size_t>(std::count_if(
        existing.begin(), existing.end(), [&](const juce::String& key) { return !contains(incoming, key); }));
    if ((added == 0) == (removed == 0))
        return; // nothing came or went, or both did
    if ((added > 1 && existing.empty()) || (removed > 1 && incoming.empty()))
        return; // the first fill or a clear of the whole panel

    const bool scopeOpen = pendingColumnPicture_.has_value() || columnInsertArmed_;
    if (removed > 0 && !pendingColumnPicture_)
        noteColumnsLeaving();
    else if (added > 0)
        columnInsertArmed_ = true;
    autoColumnFinish_ = !scopeOpen;
}

// Called by rebuild(): which strip and bus columns it had to build new and which it let go. Only a delete (a picture is
// waiting), an undo/redo, or an add or duplicate (armColumnInsertMotion) is animated; a column added any other way
// lands at once.
void MixerPanelComponent::noteColumnSetChange(const std::vector<juce::String>& added,
                                              const std::vector<juce::String>& removed) {
    if (added.empty() && removed.empty())
        return;
    columnMotion_.finishNow();
    if (!pendingColumnPicture_ && !columnInsertArmed_ && (undoManager_ == nullptr || !undoManager_->isRestoring()))
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
    columnInsertArmed_ = false;
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
