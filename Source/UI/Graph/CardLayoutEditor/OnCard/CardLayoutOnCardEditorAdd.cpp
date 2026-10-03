// CardLayoutOnCardEditorAdd.cpp -- "+ Add control": the Add control panel opening under the card, a click
// on a row putting that control back in the last group at a free spot, and the fade the added control
// arrives with. Dragging a row onto the card is CardLayoutOnCardEditorAddDrop.cpp.
// docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutOnCardEditor.h"
#include "OnCardAddControlModel.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardBody/CardBodyLayoutWalk.h"
#include "UI/Graph/CardLayoutEditor/BuiltInCardLayoutSource.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include <utility>

namespace synth::ui {

namespace {

constexpr double kFadeInMs = 160.0;
constexpr double kReducedFadeMs = 80.0;

} // namespace

// The panel opens from the button, in a call-out like the per-control panel's. It lists what the card does
// not show, and the button is off when that is nothing.
void CardLayoutOnCardEditor::openAddPanel() {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (closed_ || source_ == nullptr || body == nullptr || addPanel_ != nullptr)
        return;
    flushNudge();
    endDrag();
    closePanel();
    auto controls = addableControls(*body);
    if (controls.empty())
        return;
    auto panel = std::make_unique<CardLayoutAddPanel>();
    juce::Component::SafePointer<CardLayoutOnCardEditor> self(this);
    panel->onPick = [self](const juce::String& paramId) {
        if (auto* e = self.getComponent())
            e->addControl(paramId);
    };
    panel->onDrag = [self](const juce::String& paramId, CardLayoutAddRow::DragPhase phase, juce::Point<int> at) {
        if (auto* e = self.getComponent())
            e->dragAdded(paramId, phase, at);
    };
    panel->onRequestClose = [self] {
        if (auto* e = self.getComponent())
            e->closePanel();
    };
    panel->setControls(std::move(controls));
    addPanel_ = panel.get();
    launchPanel(std::move(panel), addControl_.getScreenBounds());
}

// After a rebuild: the button is on while something is off the card, and the open panel lists what is
// left (it closes when nothing is).
void CardLayoutOnCardEditor::refreshAddPanel() {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    auto controls = body != nullptr ? addableControls(*body) : std::vector<AddableControl>();
    addControl_.setEnabled(!controls.empty());
    auto* panel = addPanel_.getComponent();
    if (panel == nullptr)
        return;
    if (controls.empty())
        return closePanel();
    panel->setControls(std::move(controls));
}

std::vector<juce::Rectangle<int>> CardLayoutOnCardEditor::occupiedIn(int planSection) const {
    std::vector<juce::Rectangle<int>> rects;
    for (const auto& cell : cells_)
        if (cell.section == planSection)
            rects.push_back(cell.rect);
    if (card_ != nullptr)
        for (const auto& view : collectViews(*card_, planSection))
            rects.push_back(view.rect);
    return rects;
}

// The size its widget and caption take in that group, by the same rule the group's layout sizes a cell.
juce::Point<int> CardLayoutOnCardEditor::addedCellSize(int planSection, const juce::String& paramId) const {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    const int item = body != nullptr ? body->getPlan().findParam(paramId) : -1;
    if (item < 0 || planSection < 0 || planSection >= (int)body->getPlan().sections.size())
        return {};
    const auto g = cardbody::BodyGeometry::forCardWidth(card_->getWidth());
    return cardBodyCellSize(body->getPlan().items[(size_t)item].kind,
                            body->getPlan().sections[(size_t)planSection].columns, g);
}

// A place that overlaps nothing and keeps the 8 px gap, as a free position (from the group's content origin).
juce::Point<int> CardLayoutOnCardEditor::freeSpotIn(int planSection, const juce::String& paramId) const {
    const auto* body = card_->getCardBody();
    const auto g = cardbody::BodyGeometry::forCardWidth(card_->getWidth());
    const auto spot = oncard::findFreeSpot(addedCellSize(planSection, paramId), occupiedIn(planSection),
                                           limitsFor(*body, planSection, card_->getWidth()));
    return {spot.x - g.contentX, spot.y - body->getPlan().sections[(size_t)planSection].cellTop};
}

// A click: the control goes to the end of the last grid group. A positioned group gets it at the next free
// spot; a flowing one just takes it in its flow.
void CardLayoutOnCardEditor::addControl(const juce::String& paramId) {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (closed_ || source_ == nullptr || body == nullptr)
        return;
    flushNudge();
    const auto layout = body->explicitLayout();
    const auto& plan = body->getPlan();
    const int item = plan.findParam(paramId);
    const int target = lastGridSectionIndex(layout);
    std::optional<juce::Point<int>> at;
    if (target >= 0 && item >= 0) {
        const int planSection = planSectionIndexOf(layout, target);
        if (planSection < (int)plan.sections.size() && plan.sections[(size_t)planSection].freeform)
            at = freeSpotIn(planSection, paramId);
    }
    const auto name = item >= 0 ? plan.items[(size_t)item].captionText() : paramId;
    writeLayout(withControlAdded(layout, paramId, at));
    finishAdded(paramId, name);
}

void CardLayoutOnCardEditor::finishAdded(const juce::String& paramId, const juce::String& name) {
    fadeInControl(paramId);
    announce(name + " added");
}

// The added control fades in at its spot: 160 ms, 80 ms under Reduce Motion. Nothing on screen, nothing to fade.
void CardLayoutOnCardEditor::fadeInControl(const juce::String& paramId) {
    const int cell = indexOfCell(paramId);
    if (cell < 0 || !isShowing())
        return;
    const std::vector<juce::Component::SafePointer<juce::Component>> parts{cells_[(size_t)cell].widget,
                                                                           cells_[(size_t)cell].label};
    const auto setAlpha = [parts](float alpha) {
        for (const auto& part : parts)
            if (part != nullptr)
                part->setAlpha(alpha);
    };
    setAlpha(0.0f);
    const double ms = prefersReducedMotion() ? kReducedFadeMs : kFadeInMs;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    finishAddFade_ = [this, setAlpha] {
        addFadePump_.stop();
        setAlpha(1.0f);
    };
    addFadePump_.run(
        ms,
        [setAlpha, start, ms] {
            const auto t = (float)juce::jlimit(0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - start) / ms);
            setAlpha(easeOutCubic(t));
        },
        [this] { finishAddFade_ = nullptr; });
}

} // namespace synth::ui
