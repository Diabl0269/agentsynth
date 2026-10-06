// CardLayoutOnCardEditorAddDrop.cpp -- dragging a row of the Add control panel onto the card: a ghost of the
// control's cell follows the pointer, and where it is dropped the control lands at that spot in the group
// there, the neighbours it crowds pushed aside as a drop of a control already on the card does.
// docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutOnCardEditor.h"
#include "OnCardAddControlModel.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <utility>

namespace synth::ui {

namespace {

constexpr float kDraggedPanelAlpha = 0.15f;
constexpr double kPushMs = 160.0;

// A start no neighbour is flush against: a control new to the card was never anywhere.
const juce::Rectangle<int> kNowhere(-100000, -100000, 0, 0);

} // namespace

// The group a drop at `y` lands in: the last one whose top (header included) is at or above it, else the first.
int CardLayoutOnCardEditor::editableSectionAt(int y) const {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (body == nullptr)
        return -1;
    int best = -1;
    for (int i = 0; i < (int)body->getPlan().sections.size(); ++i) {
        const auto& section = body->getPlan().sections[(size_t)i];
        if (!section.visible || section.footer || section.tabGroup >= 0)
            continue;
        const int top = section.cellTop - (section.hasHeader() ? cardbody::kSectionHeaderHeight : 0);
        if (best < 0 || top <= y)
            best = i;
    }
    return best;
}

// The panel stays under the pointer's hand but fades back so the card shows through; releasing over it
// cancels the drag.
void CardLayoutOnCardEditor::dragAdded(const juce::String& paramId, CardLayoutAddRow::DragPhase phase,
                                       juce::Point<int> screenPosition) {
    const auto at = getLocalPoint(nullptr, screenPosition);
    auto* box = addPanel_ != nullptr ? addPanel_->findParentComponentOfClass<juce::CallOutBox>() : nullptr;
    const int section = card_ != nullptr && card_->getCardBody() != nullptr ? editableSectionAt(at.y) : -1;
    if (phase == CardLayoutAddRow::DragPhase::Begin && box != nullptr)
        box->setAlpha(kDraggedPanelAlpha);
    addDrag_.paramId = paramId;
    addDrag_.ghost = {};
    if (phase != CardLayoutAddRow::DragPhase::End && section >= 0 && cardArea().contains(at)) {
        const auto size = addedCellSize(section, paramId);
        addDrag_.ghost = oncard::clampToLimits({at - size / 2, at - size / 2 + size},
                                               limitsFor(*card_->getCardBody(), section, card_->getWidth()));
    }
    repaint();
    if (phase != CardLayoutAddRow::DragPhase::End)
        return;
    const bool onPanel = box != nullptr && box->getScreenBounds().contains(screenPosition);
    if (box != nullptr)
        box->setAlpha(1.0f);
    addDrag_ = {};
    if (!onPanel && cardArea().contains(at))
        dropAdded(paramId, at);
}

// Writes every cell of the group at its place after the push (the group becomes positioned, as a drop of
// a control already on the card makes it), then the new control at the drop.
void CardLayoutOnCardEditor::dropAdded(const juce::String& paramId, juce::Point<int> at) {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    const int section = body != nullptr ? editableSectionAt(at.y) : -1;
    if (closed_ || source_ == nullptr || section < 0)
        return;
    flushNudge();
    const auto layout = body->explicitLayout();
    const int layoutSection = layoutSectionIndexOfPlan(layout, section);
    const auto limits = limitsFor(*body, section, card_->getWidth());
    const auto size = addedCellSize(section, paramId);
    const auto dropped = oncard::clampToLimits({at - size / 2, at - size / 2 + size}, limits);

    const auto indices = cellsOfSection(section);
    const auto pushed = pushedNeighbours(section, -1, dropped, kNowhere);
    std::vector<OnCardCell> placed;
    std::vector<Move> moves;
    for (size_t k = 0; k < indices.size(); ++k) {
        placed.push_back(cells_[(size_t)indices[k]]);
        placed.back().rect = pushed[k];
        moves.push_back({placed.back().key, cells_[(size_t)indices[k]].rect, pushed[k], kPushMs, false});
    }
    const auto g = cardbody::BodyGeometry::forCardWidth(card_->getWidth());
    const int top = body->getPlan().sections[(size_t)section].cellTop;
    const auto positioned = withCellPositions(layout, placed, collectViews(*card_, section), g.contentX, top);
    const int item = body->getPlan().findParam(paramId);
    const auto name = item >= 0 ? body->getPlan().items[(size_t)item].captionText() : paramId;
    writeLayout(withControlAdded(positioned, paramId,
                                 juce::Point<int>(dropped.getX() - g.contentX, dropped.getY() - top), layoutSection));
    finishAdded(paramId, name);
    startGlide(std::move(moves));
}

} // namespace synth::ui
