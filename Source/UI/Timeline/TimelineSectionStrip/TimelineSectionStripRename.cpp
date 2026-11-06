// TimelineSectionStripRename.cpp
//
// The Sections strip's in-place name editor: the same shape as the ruler's marker rename (detach before delete so the
// focus-lost callback cannot re-enter, Return and click-away commit, Esc cancels), with one difference of meaning: a
// section's name is shared by every copy, so one rename changes them all.

#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"

#include <algorithm>

namespace synth::ui {

namespace {
constexpr int kMinEditorWidth = 96;
} // namespace

void TimelineSectionStrip::beginRenameSection(synth::PlacementId id) {
    if (doc_ == nullptr)
        return;
    // FIRST: any previous rename commits before a new one opens, and it may mutate the doc.
    if (renamingPlacement_ != id)
        finishRename(true);
    const auto* placement = doc_->getPlacement(id);
    if (placement == nullptr)
        return;
    const auto* section = doc_->getSection(placement->section);
    if (section == nullptr)
        return;

    const auto blocks = buildBlocks();
    const auto found = std::find_if(blocks.begin(), blocks.end(), [id](const Block& b) { return b.id == id; });
    if (found == blocks.end())
        return; // scrolled off screen: nowhere to put the editor

    const auto rect = found->bounds.toNearestInt();
    const int width = std::max(kMinEditorWidth, rect.getWidth());
    const int height = std::max(12, getHeight() - 2);
    const int x = juce::jlimit(0, std::max(0, getWidth() - width), rect.getX());
    const int y = std::max(0, (getHeight() - height) / 2);

    const bool fresh = renameEditor_ == nullptr;
    if (fresh) {
        renameEditor_ = std::make_unique<juce::TextEditor>("sectionRenameEditor");
        renameEditor_->setComponentID("timelineSectionRenameEditor");
        renameEditor_->setTitle("Section name");
        renameEditor_->setMultiLine(false);
        renameEditor_->setReturnKeyStartsNewLine(false);
        renameEditor_->setText(section->name, juce::dontSendNotification);
        renameEditor_->onReturnKey = [this] { finishRename(true); };
        renameEditor_->onEscapeKey = [this] { finishRename(false); };
        // Clicking away commits, like every in-place rename in the app; Escape is the cancel.
        renameEditor_->onFocusLost = [this] { finishRename(true); };
        addAndMakeVisible(*renameEditor_);
    }
    renamingPlacement_ = id;
    renameEditor_->setBounds(x, y, width, height);
    if (fresh) {
        renameEditor_->selectAll();
        renameEditor_->grabKeyboardFocus();
    }
}

void TimelineSectionStrip::finishRename(bool commit) {
    if (renameEditor_ == nullptr)
        return;
    // Detach FIRST: deleting the editor takes focus from it, which fires onFocusLost, which re-enters here and finds a
    // null editor, so it stops.
    auto editor = std::move(renameEditor_);
    const auto id = renamingPlacement_;
    renamingPlacement_ = {};
    const juce::String text = editor->getText();
    editor.reset();

    if (!commit || doc_ == nullptr)
        return;
    if (const auto* placement = doc_->getPlacement(id))
        renameSection(placement->section, text);
}

// An empty name cancels (the section keeps its own); an unchanged one records nothing.
void TimelineSectionStrip::renameSection(synth::SectionId id, const juce::String& newName) {
    const auto name = newName.trim();
    const auto* section = doc_ != nullptr ? doc_->getSection(id) : nullptr;
    if (section == nullptr || name.isEmpty() || name == section->name)
        return;
    performEdit([this, id, name] { doc_->renameSection(id, name); });
    refresh();
}

} // namespace synth::ui
