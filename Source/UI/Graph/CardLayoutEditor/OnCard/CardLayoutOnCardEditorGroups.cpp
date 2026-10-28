// CardLayoutOnCardEditorGroups.cpp -- dropping a control into another group of the card: the drop writes the
// control into that group's items at the drop position, the neighbours it crowds there pushed aside, as one
// write. docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutOnCardEditor.h"
#include "OnCardAddControlModel.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <utility>

namespace synth::ui {

namespace {

constexpr double kPushMs = 160.0;
constexpr double kSettleMs = 140.0;

} // namespace

// The control (every member of its swap group) leaves its group's items for the target's, free-positioned where
// the drop's row plan landed it (sized as the target group sizes it: its columns can differ). Both groups are
// written positioned so nothing else moves: the group it left keeps every other control where it stands, the
// target gets its cells as the plan placed them. One write, so one undo step.
void CardLayoutOnCardEditor::commitMoveToSection(int cell, juce::Rectangle<int> landed, int target,
                                                 const CellRects& placed) {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (closed_ || source_ == nullptr || body == nullptr || cell < 0 || cell >= (int)cells_.size())
        return;
    const auto moving = cells_[(size_t)cell];
    const int from = moving.section;
    const auto layout = body->explicitLayout();
    const int layoutTarget = layoutSectionIndexOfPlan(layout, target);
    if (layoutTarget < 0 || from == target)
        return;
    const auto indices = cellsOfSection(target);
    std::vector<OnCardCell> placedTarget;
    std::vector<Move> moves{{moving.key, moving.rect, landed, kSettleMs, false}};
    for (int index : indices) {
        placedTarget.push_back(cells_[(size_t)index]);
        placedTarget.back().rect = placedRect(placed, index, cells_[(size_t)index].rect);
        moves.push_back(
            {placedTarget.back().key, cells_[(size_t)index].rect, placedTarget.back().rect, kPushMs, false});
    }
    std::vector<OnCardCell> staying;
    for (int i : cellsOfSection(from))
        if (i != cell)
            staying.push_back(cells_[(size_t)i]);

    const auto g = cardbody::BodyGeometry::forCardWidth(card_->getWidth());
    const auto& sections = body->getPlan().sections;
    const int fromTop = sections[(size_t)from].cellTop;
    const int targetTop = sections[(size_t)target].cellTop;
    auto written = withCellPositions(layout, staying, collectViews(*card_, from), g.contentX, fromTop);
    written = withCellPositions(std::move(written), placedTarget, collectViews(*card_, target), g.contentX, targetTop);
    const juce::Point<int> at(landed.getX() - g.contentX, landed.getY() - targetTop);
    const auto codeDefault = body->codeDefaultLayout();
    for (const auto& id : moving.paramIds)
        if (!layout.hidden.contains(id))
            written = withControlAdded(std::move(written), id, at, layoutTarget, codeDefault ? &*codeDefault : nullptr);

    homes_[moving.key] = landed;
    const auto name = cardSectionDisplayName(written.sections[(size_t)layoutTarget]);
    writeLayout(written);
    announce(moving.caption + " moved to " + name);
    startGlide(std::move(moves));
}

} // namespace synth::ui
