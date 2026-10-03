// OnCardCells.cpp -- reading the on-card editor's cells off a card's plan and writing their free
// positions into a layout.
#include "OnCardCells.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <map>

namespace synth::ui {

namespace {

bool isEditableSection(const CardBodyPlan::Section& section) {
    return section.visible && !section.footer && section.tabGroup < 0;
}

// The members of the cell that starts at `section.items[from]`: one item, or a run of one swap group.
std::vector<int> membersAt(const CardBodyPlan& plan, const CardBodyPlan::Section& section, size_t from) {
    const int group = plan.items[(size_t)section.items[from]].swapGroup;
    std::vector<int> members{section.items[from]};
    for (size_t i = from + 1; group >= 0 && i < section.items.size(); ++i) {
        if (plan.items[(size_t)section.items[i]].swapGroup != group)
            break;
        members.push_back(section.items[i]);
    }
    return members;
}

// A cell for `members`, or nothing when no member is drawn on the card now (a view, an empty swap cell).
bool makeCell(const CardBodyPlan& plan, int sectionIndex, const std::vector<int>& members, OnCardCell& cell) {
    const CardBodyItem* shown = nullptr;
    for (int member : members) {
        const auto& item = plan.items[(size_t)member];
        if (item.param != nullptr)
            cell.paramIds.push_back(item.param->paramID);
        if (shown == nullptr && item.shown && item.param != nullptr && item.widget != nullptr &&
            item.widget->isVisible())
            shown = &item;
    }
    if (shown == nullptr)
        return false;
    auto rect = shown->widget->getBounds();
    if (shown->label != nullptr && shown->label->isVisible())
        rect = rect.getUnion(shown->label->getBounds());
    cell.key = shown->param->paramID;
    cell.caption = shown->captionText();
    cell.section = sectionIndex;
    cell.rect = rect;
    cell.widget = shown->widget;
    cell.widgetRel = shown->widget->getBounds().translated(-rect.getX(), -rect.getY());
    if (shown->label != nullptr && shown->label->isVisible()) {
        cell.label = shown->label;
        cell.labelRel = shown->label->getBounds().translated(-rect.getX(), -rect.getY());
    }
    return true;
}

} // namespace

std::vector<OnCardCell> collectCells(const ModuleComponent& card) {
    std::vector<OnCardCell> cells;
    const auto* body = card.getCardBody();
    if (body == nullptr)
        return cells;
    const auto& plan = body->getPlan();
    for (int s = 0; s < (int)plan.sections.size(); ++s) {
        const auto& section = plan.sections[(size_t)s];
        if (!isEditableSection(section))
            continue;
        for (size_t i = 0; i < section.items.size();) {
            const auto members = membersAt(plan, section, i);
            i += members.size();
            OnCardCell cell;
            if (plan.items[(size_t)members.front()].kind != CardBodyItem::Kind::View &&
                makeCell(plan, s, members, cell))
                cells.push_back(std::move(cell));
        }
    }
    return cells;
}

std::vector<OnCardView> collectViews(const ModuleComponent& card, int section) {
    std::vector<OnCardView> views;
    const auto* body = card.getCardBody();
    if (body == nullptr)
        return views;
    const auto& plan = body->getPlan();
    for (int index : plan.sections[(size_t)section].items) {
        const auto& item = plan.items[(size_t)index];
        if (item.kind == CardBodyItem::Kind::View && item.widget != nullptr && item.widget->isVisible())
            views.push_back({item.view, item.widget->getBounds()});
    }
    return views;
}

oncard::Limits limitsFor(const synth::CardBody& body, int section, int cardWidth) {
    const auto g = cardbody::BodyGeometry::forCardWidth(cardWidth);
    return {g.contentX, g.contentX + g.contentW, body.getPlan().sections[(size_t)section].cellTop};
}

void placeCell(const OnCardCell& cell, juce::Point<int> topLeft) {
    if (cell.widget != nullptr)
        cell.widget->setBounds(cell.widgetRel.translated(topLeft.x, topLeft.y));
    if (cell.label != nullptr)
        cell.label->setBounds(cell.labelRel.translated(topLeft.x, topLeft.y));
}

CardLayout withCellPositions(CardLayout layout, const std::vector<OnCardCell>& cells,
                             const std::vector<OnCardView>& views, int contentX, int top) {
    std::map<juce::String, juce::Point<int>> positions;
    for (const auto& cell : cells)
        for (const auto& id : cell.paramIds)
            positions[id] = {cell.rect.getX() - contentX, cell.rect.getY() - top};
    for (auto& section : layout.sections)
        for (auto& item : section.items)
            if (auto* param = std::get_if<CardParamItem>(&item)) {
                if (const auto found = positions.find(param->paramId); found != positions.end())
                    param->at = found->second;
            } else if (auto* view = std::get_if<CardViewItem>(&item)) {
                for (const auto& shown : views)
                    if (shown.view == view->view)
                        view->at = juce::Point<int>(shown.rect.getX() - contentX, shown.rect.getY() - top);
            }
    return layout;
}

} // namespace synth::ui
