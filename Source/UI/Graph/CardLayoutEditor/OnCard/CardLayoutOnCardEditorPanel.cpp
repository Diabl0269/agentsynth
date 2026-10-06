// CardLayoutOnCardEditorPanel.cpp -- the per-control panel of the on-card editor: opening it in a call-out
// beside the control, keeping it anchored to the control's outline as the card rebuilds, and the writes
// its fields make. docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutOnCardEditor.h"
#include "OnCardControlOptions.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardBody/CardLayoutQuickEdit.h"
#include "UI/Graph/CardLayoutEditor/BuiltInCardLayoutSource.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/ReducedMotion.h"
#include <utility>

namespace synth::ui {

namespace {

std::function<void(std::unique_ptr<juce::Component>)>& launcherForTest() {
    static std::function<void(std::unique_ptr<juce::Component>)> launcher;
    return launcher;
}

} // namespace

void CardLayoutOnCardEditor::setControlPanelLauncherForTest(
    std::function<void(std::unique_ptr<juce::Component>)> launcher) {
    launcherForTest() = std::move(launcher);
}

juce::Rectangle<int> CardLayoutOnCardEditor::screenAreaOf(const CardLayoutOutline& outline) const {
    return outline.localAreaToGlobal(outline.getOutlineArea().toNearestInt());
}

// Every callback holds the editor through a SafePointer: the call-out outlives a session that ends first.
void CardLayoutOnCardEditor::openControlPanel(const juce::String& paramId) {
    auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    auto* outline = getOutlineForTest(paramId);
    if (closed_ || source_ == nullptr || body == nullptr || outline == nullptr)
        return;
    flushNudge();
    endDrag();
    if (panel_ != nullptr && panelParamId_ == paramId)
        return;
    closePanel();
    const auto options = readControlOptions(*body, source_->parameters(), body->explicitLayout(), paramId);
    if (!options.has_value())
        return;

    auto panel = std::make_unique<CardLayoutControlPanel>();
    juce::Component::SafePointer<CardLayoutOnCardEditor> self(this);
    panel->onShowAs = [self, paramId](CardWidget widget) {
        if (auto* e = self.getComponent())
            e->editControl(paramId,
                           [paramId, widget](CardLayout l) { return withShowAs(std::move(l), paramId, widget); });
    };
    panel->onLabel = [self, paramId](const juce::String& text) {
        auto* e = self.getComponent();
        if (e == nullptr || e->panel_ == nullptr)
            return;
        const auto name = e->panel_->getOptions().displayName;
        e->editControl(paramId, [paramId, name, text](CardLayout l) {
            return withControlLabel(std::move(l), paramId, name, text);
        });
    };
    panel->onRange = [self, paramId](std::optional<juce::Range<double>> range) {
        if (auto* e = self.getComponent())
            e->editControl(paramId,
                           [paramId, range](CardLayout l) { return withControlRange(std::move(l), paramId, range); });
    };
    panel->onHide = [self, paramId] {
        if (auto* e = self.getComponent())
            e->hideControl(paramId);
    };
    panel->onRequestClose = [self] {
        if (auto* e = self.getComponent())
            e->closePanel();
    };
    panel->setOptions(*options);
    panel_ = panel.get();
    panelParamId_ = paramId;
    launchPanel(std::move(panel), screenAreaOf(*outline));
}

// A call-out pointing at `screenArea`; a test takes the panel instead.
void CardLayoutOnCardEditor::launchPanel(std::unique_ptr<juce::Component> panel, juce::Rectangle<int> screenArea) {
    if (auto& launcher = launcherForTest())
        return launcher(std::move(panel));
    juce::CallOutBox::launchAsynchronously(std::move(panel), screenArea, nullptr);
}

// The call-out deletes its content once it has gone; the editor lets go of it at once, so nothing it
// does afterwards reaches the panel. A panel with no call-out (a test's) is only hidden. The control panel
// and the Add control panel never stand together, so closing one closes whichever is open.
void CardLayoutOnCardEditor::closePanel() {
    juce::Component* open[] = {panel_.getComponent(), addPanel_.getComponent()};
    panel_ = nullptr;
    addPanel_ = nullptr;
    panelParamId_ = {};
    for (auto* panel : open)
        if (panel != nullptr && !closeHostingWindow(*panel))
            panel->setVisible(false);
}

// After a rebuild: the panel takes the control's new state and its call-out points at the control's new
// outline; a control that is gone (hidden, or off the card) closes it.
void CardLayoutOnCardEditor::refreshPanel() {
    auto* panel = panel_.getComponent();
    if (panel == nullptr)
        return;
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    auto* outline = getOutlineForTest(panelParamId_);
    if (body == nullptr || outline == nullptr || source_ == nullptr)
        return closePanel();
    const auto options = readControlOptions(*body, source_->parameters(), body->explicitLayout(), panelParamId_);
    if (!options.has_value())
        return closePanel();
    panel->setOptions(*options);
    if (auto* box = panel->findParentComponentOfClass<juce::CallOutBox>()) {
        const auto area = screenAreaOf(*outline);
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(area))
            box->updatePosition(area, display->userArea);
    }
}

// A field's change: written at once, from the layout the card draws now, changing only that control.
void CardLayoutOnCardEditor::editControl(const juce::String& paramId,
                                         const std::function<CardLayout(CardLayout)>& edit) {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (closed_ || source_ == nullptr || body == nullptr)
        return;
    flushNudge();
    const auto before = withPlacedItem(body->explicitLayout(), paramId, *body);
    auto after = edit(before);
    if (after != before)
        writeLayout(after);
}

// Hide closes the panel first: the control leaves the card for the More row, and focus goes back to the
// first outline left.
void CardLayoutOnCardEditor::hideControl(const juce::String& paramId) {
    closePanel();
    startShrinkGhost(paramId); // a picture of it, taken before the layout is rewritten
    editControl(paramId,
                [paramId](CardLayout l) { return applyCardQuickEdit(std::move(l), paramId, CardQuickEdit::Hide); });
    if (auto* first = outlines_.getFirst())
        first->grabKeyboardFocus();
}

// The removed control shrinks away where it was: a ghost of the card's picture of it (150 ms, backwards what
// adding does), gone when it has shrunk. Nothing on screen, nothing to animate.
void CardLayoutOnCardEditor::startShrinkGhost(const juce::String& paramId) {
    using namespace control_motion;
    const int cell = indexOfCell(paramId);
    if (cell < 0 || card_ == nullptr || !canAnimate())
        return;
    const auto rect = cells_[(size_t)cell].rect;
    auto image = card_->createComponentSnapshot(rect, true, 2.0f);
    if (image.isNull())
        return;
    auto* ghost =
        ghosts_.add(new ShrinkGhost(std::move(image), {}, axisFor(cells_[(size_t)cell].rect), prefersReducedMotion()));
    ghost->setBounds(rect.withPosition(getLocalPoint(card_, rect.getPosition())));
    addAndMakeVisible(ghost);
    ghost->toBack();
    ghostPump_.run(ghost->durationMs(), [this] { tickGhosts(); }, [this] { ghosts_.clear(); });
}

void CardLayoutOnCardEditor::tickGhosts() {
    const auto now = juce::Time::getMillisecondCounterHiRes();
    for (int i = ghosts_.size(); --i >= 0;) {
        auto* ghost = ghosts_[i];
        ghost->setProgress((float)((now - ghost->startMs()) / ghost->durationMs()));
        if (ghost->progress() >= 1.0f)
            ghosts_.remove(i);
    }
}

} // namespace synth::ui
