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

// The start no neighbour is flush against: the control was never in the group it lands in.
const juce::Rectangle<int> kNowhere(-100000, -100000, 0, 0);

} // namespace

// The control (every member of its swap group) leaves its group's items for the target's, free-positioned at the
// drop: sized as the target group sizes it (its columns can differ), clamped inside it. Both groups are written
// positioned so nothing else moves: the group it left keeps every other control where it stands, the target gets
// its neighbours pushed from where they stand (and back home when free). One write, so one undo step.
void CardLayoutOnCardEditor::commitMoveToSection(int cell, juce::Rectangle<int> dropped, int target) {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (closed_ || source_ == nullptr || body == nullptr || cell < 0 || cell >= (int)cells_.size())
        return;
    const auto moving = cells_[(size_t)cell];
    const int from = moving.section;
    const auto layout = body->explicitLayout();
    const int layoutTarget = layoutSectionIndexOfPlan(layout, target);
    if (layoutTarget < 0 || from == target)
        return;
    const auto size = addedCellSize(target, moving.key);
    const auto landed = oncard::clampToLimits(size.isOrigin() ? dropped : dropped.withSize(size.x, size.y),
                                              limitsFor(*body, target, card_->getWidth()));

    const auto indices = cellsOfSection(target);
    const auto pushed = pushedNeighbours(target, -1, landed, kNowhere);
    std::vector<OnCardCell> placedTarget;
    std::vector<Move> moves{{moving.key, moving.rect, landed, kSettleMs, false}};
    for (size_t k = 0; k < indices.size(); ++k) {
        placedTarget.push_back(cells_[(size_t)indices[k]]);
        placedTarget.back().rect = pushed[k];
        moves.push_back({placedTarget.back().key, cells_[(size_t)indices[k]].rect, pushed[k], kPushMs, false});
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
