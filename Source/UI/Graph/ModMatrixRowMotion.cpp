// Concern: ModMatrixComponent's motion when a routing row is added or removed (Add Modulation, a row's delete button,
// undo and redo of either, a source created from a row's picker): the row grows in, or shrinks away, and the rows
// below make room, through the shared ExitEnterListMotion the mixer's rows and the timeline's tracks use. Rows that
// change in place, or that both come and go in one refresh, land at once.
#include "ModMatrixRowMotion.h"
#include "ModMatrixComponent.h"
#include "UI/Layout/ExitEnterList/ExitEnterListMotion.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>

namespace {
using synth::ui::ExitEnterListRow;

bool hasKey(const std::vector<ExitEnterListRow>& rows, const juce::String& key) {
    return std::any_of(rows.begin(), rows.end(), [&key](const ExitEnterListRow& r) { return r.key == key; });
}
} // namespace

bool ModMatrixComponent::canAnimateRows() const {
    // A list scrolled away from its top is left out: the pictures would not line up with the rows.
    return (synth::ui::FadeVisibility::canAnimateIn(this) || rowMotionForced_) && !synth::ui::animationsOff() &&
           !viewport.getBounds().isEmpty() && viewport.getViewPositionY() == 0;
}

std::unique_ptr<synth::ui::ModMatrixRowSnapshot> ModMatrixComponent::snapshotRows() const {
    if (rows.empty() || !canAnimateRows())
        return nullptr;
    auto snapshot = std::make_unique<synth::ui::ModMatrixRowSnapshot>();
    snapshot->rows = rowMotionRows();
    snapshot->picture = synth::ui::ExitEnterListMotion::pictureOf(const_cast<ModMatrixComponent&>(*this),
                                                                  viewport.getBounds(), snapshot->pictureScale);
    return snapshot;
}

void ModMatrixComponent::playRowMotion(std::unique_ptr<synth::ui::ModMatrixRowSnapshot> before,
                                       const std::vector<juce::String>& startKeys) {
    if (rowMotion_ == nullptr)
        rowMotion_ = std::make_unique<synth::ui::ExitEnterListMotion>(*this);
    rowMotion_->finishNow();
    const auto after = rowMotionRows();
    std::vector<juce::String> added, removed;
    for (const auto& row : after)
        if (std::find(startKeys.begin(), startKeys.end(), row.key) == startKeys.end())
            added.push_back(row.key);
    for (const auto& key : startKeys)
        if (!hasKey(after, key))
            removed.push_back(key);
    if (added.empty() == removed.empty() || !canAnimateRows())
        return; // nothing came or went, or both did (lands at once)

    const auto area = viewport.getBounds();
    const auto& colours = synth::theme::themeOf(*this).colors;
    synth::ui::ExitEnterListMotion::Job job;
    job.axis = synth::ui::ListAxis::Vertical;
    job.area = area;
    job.background = colours.bg1;
    job.accent = colours.accent;
    const float extent = static_cast<float>(area.getHeight());
    if (!removed.empty()) {
        if (before == nullptr || before->picture.isNull())
            return;
        job.picture = before->picture;
        job.pictureScale = before->pictureScale;
        job.items = synth::ui::ExitEnterListPlan::forRemoval(before->rows, after, extent);
    } else {
        job.picture = synth::ui::ExitEnterListMotion::pictureOf(*this, area, job.pictureScale);
        job.items = synth::ui::ExitEnterListPlan::forInsertion(after, added, extent);
    }
    rowMotion_->start(std::move(job));
}

synth::ui::ExitEnterListMotion* ModMatrixComponent::getRowMotionForTest() { return rowMotion_.get(); }
