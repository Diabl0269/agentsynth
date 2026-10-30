// CardLayoutOnCardEditorGroupRename.cpp -- editing a group's title in place: a double-click, Return or the menu's
// Rename group opens the app's inline label editor over the title. Return (or leaving the field) keeps the text,
// Esc cancels, an empty or unchanged text writes nothing; a rename is ONE write.
// docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutOnCardEditor.h"
#include "OnCardAddControlModel.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <utility>

namespace synth::ui {

// The inline editor is a NonModalLabel (Source/UI/CLAUDE.md): VoiceOver keeps the whole window while it is open.
void CardLayoutOnCardEditor::startRename(const juce::String& sectionId) {
    flushNudge();
    if (finishGlide_)
        std::exchange(finishGlide_, nullptr)();
    const int section = planSectionOfGroup(sectionId);
    if (closed_ || card_ == nullptr || section < 0)
        return;
    cancelRename();
    const auto& s = card_->getCardBody()->getPlan().sections[(size_t)section];
    renamingId_ = sectionId;
    renameLabel_.setText(s.title->trim(), juce::dontSendNotification);
    renameLabel_.setBounds(s.header->getBounds());
    renameLabel_.setVisible(true);
    renameLabel_.toFront(false);
    renameLabel_.showEditor();
}

void CardLayoutOnCardEditor::commitRename(const juce::String& text) {
    const auto id = std::exchange(renamingId_, juce::String());
    const auto title = text.trim();
    const int section = planSectionOfGroup(id);
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (closed_ || source_ == nullptr || body == nullptr || section < 0 || title.isEmpty() ||
        title == body->getPlan().sections[(size_t)section].title->trim())
        return;
    const auto layout = body->explicitLayout();
    const auto written = oncard::withGroupTitle(layout, layoutSectionIndexOfPlan(layout, section), title);
    if (written == layout)
        return;
    writeLayout(written);
    announce("Group renamed to " + title);
}

// A rename in progress ends without writing (the card changed under it, a drag started, the session ends).
void CardLayoutOnCardEditor::cancelRename() {
    if (renameLabel_.getCurrentTextEditor() != nullptr)
        renameLabel_.hideEditor(true);
    renameLabel_.setVisible(false);
    renamingId_ = {};
}

} // namespace synth::ui
