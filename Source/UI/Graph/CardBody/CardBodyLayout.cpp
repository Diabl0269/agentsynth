// CardBodyLayout.cpp -- laying a card body out: each shown section's items in card order, as cells (one
// item, or one swap group showing whichever member's condition holds), grouped into runs of one kind (a
// combo stack, toggle rows, a knob grid, a full-width view) placed by the shared run layouts. One
// function measures (apply = false) and places (apply = true), so the measured height and the real
// positions cannot drift apart; with null widgets (a plan never built) it measures statically. The
// footer row is CardBodyFooter.cpp.
#include "CardBody.h"
#include "CardBodyLayoutWalk.h"
#include "CardBodyViews.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include <limits>

namespace synth {

namespace {

using Kind = CardBodyItem::Kind;

// One place in a run: an item, or a swap group (`group` >= 0) showing `item`, -1 when no member's
// condition holds (the cell stays, empty, so the card keeps its height).
struct Cell {
    int item = -1;
    Kind runKind = Kind::Knob;
    int group = -1;
};

std::vector<Cell> cellsOf(const CardBodyPlan& plan, const std::vector<int>& indices, bool groupSwaps) {
    std::vector<Cell> cells;
    for (size_t i = 0; i < indices.size();) {
        const auto& item = plan.items[(size_t)indices[i]];
        if (!groupSwaps || item.swapGroup < 0) {
            cells.push_back({indices[i], item.kind, -1});
            ++i;
            continue;
        }
        Cell cell{-1, plan.swapGroups[(size_t)item.swapGroup].cellKind, item.swapGroup};
        for (; i < indices.size() && plan.items[(size_t)indices[i]].swapGroup == cell.group; ++i)
            if (cell.item < 0 && plan.items[(size_t)indices[i]].shown)
                cell.item = indices[i];
        cells.push_back(cell);
    }
    return cells;
}

// The end of the run of cells sharing `cells[from]`'s kind (views are a run of one each).
size_t runEnd(const std::vector<Cell>& cells, size_t from) {
    if (cells[from].runKind == Kind::View)
        return from + 1;
    size_t end = from + 1;
    while (end < cells.size() && cells[end].runKind == cells[from].runKind)
        ++end;
    return end;
}

// A widget's own height under its caption, which a swap cell of a taller kind never stretches.
int naturalHeight(Kind kind) {
    switch (kind) {
    case Kind::Knob:
        return cardbody::kKnobHeight;
    case Kind::KnobLarge:
        return cardbody::kKnobLargeHeight;
    case Kind::FaderV:
        return cardbody::kFaderVHeight;
    case Kind::FaderH:
        return cardbody::kFaderHHeight;
    case Kind::Choice:
    case Kind::Segmented:
    case Kind::Stepper:
    case Kind::Toggle:
        return cardbody::kRowHeight;
    case Kind::View:
        break;
    }
    return std::numeric_limits<int>::max();
}

// Every member of a swap cell takes the cell the run gave its shown member, at its own height, so a
// swap moves nothing and a hidden member's knob-bound jack still lands in the cell.
void fitSwapCell(const CardBodyPlan& plan, const Cell& cell) {
    const auto& shown = plan.items[(size_t)cell.item];
    if (shown.widget == nullptr)
        return;
    auto area = shown.widget->getBounds();
    if (shown.label != nullptr)
        area = area.withX(shown.label->getX()).withWidth(shown.label->getWidth());
    for (int member : plan.swapGroups[(size_t)cell.group].members) {
        const auto& item = plan.items[(size_t)member];
        if (item.widget == nullptr)
            continue;
        auto bounds = area.withHeight(std::min(area.getHeight(), naturalHeight(item.kind)));
        if (item.kind == Kind::FaderV)
            bounds = bounds
                         .withSizeKeepingCentre(std::min(bounds.getWidth(), synth::ui::CardFader::kVerticalWidth),
                                                bounds.getHeight())
                         .withY(bounds.getY());
        item.widget->setBounds(bounds);
        if (item.label != nullptr)
            item.label->setBounds(bounds.getX(), bounds.getY() - cardbody::kLabelHeight, bounds.getWidth(),
                                  cardbody::kLabelHeight);
    }
}

int layoutRunKind(Kind kind, const std::vector<cardbody::CaptionedWidget>& captioned,
                  const std::vector<juce::Component*>& plain, int columns, int y, const cardbody::BodyGeometry& g,
                  bool apply, bool tabbed) {
    switch (kind) {
    case Kind::Choice:
        return tabbed ? y : cardbody::layoutChoiceRun(captioned, y, g, apply);
    case Kind::Toggle:
        return cardbody::layoutToggleRun(plain, y, g, apply);
    case Kind::Knob:
        return tabbed ? y : cardbody::layoutKnobRun(captioned, columns, y, g, apply);
    case Kind::KnobLarge:
        return tabbed ? y : cardbody::layoutGridRun(captioned, columns, cardbody::kKnobLargeHeight, 0, y, g, apply);
    case Kind::FaderV:
        return tabbed ? y
                      : cardbody::layoutGridRun(captioned, columns, cardbody::kFaderVHeight,
                                                synth::ui::CardFader::kVerticalWidth, y, g, apply);
    case Kind::FaderH:
        return tabbed ? y : cardbody::layoutCaptionedRows(captioned, cardbody::kFaderHHeight, true, y, g, apply);
    case Kind::Segmented:
        return tabbed ? y : cardbody::layoutCaptionedRows(captioned, cardbody::kRowHeight, true, y, g, apply);
    case Kind::Stepper:
        return tabbed ? y : cardbody::layoutCaptionedRows(captioned, cardbody::kRowHeight, false, y, g, apply);
    case Kind::View:
        break;
    }
    return y;
}

int layoutRun(const CardBodyPlan& plan, juce::AudioProcessor& module, const std::vector<Cell>& run, int columns, int y,
              const cardbody::BodyGeometry& g, bool apply, bool tabbed) {
    const auto kind = run.front().runKind;
    if (kind == Kind::View) {
        const auto& view = plan.items[(size_t)run.front().item];
        const auto* factory = findCardViewFactory(view.view);
        if (factory == nullptr || (apply && view.widget == nullptr))
            return y;
        return cardbody::layoutViewRow(view.widget, factory->preferredHeight(module), y, g, apply);
    }
    std::vector<cardbody::CaptionedWidget> captioned;
    std::vector<juce::Component*> plain;
    for (const auto& cell : run) {
        const auto* item = cell.item >= 0 ? &plan.items[(size_t)cell.item] : nullptr;
        captioned.emplace_back(item != nullptr ? item->widget : nullptr, item != nullptr ? item->label : nullptr);
        plain.push_back(item != nullptr ? item->widget : nullptr);
    }
    y = layoutRunKind(kind, captioned, plain, columns, y, g, apply, tabbed);
    if (apply && !tabbed)
        for (const auto& cell : run)
            if (cell.group >= 0 && cell.item >= 0)
                fitSwapCell(plan, cell);
    return y;
}

int layoutCells(const CardBodyPlan& plan, juce::AudioProcessor& module, const std::vector<Cell>& cells, int columns,
                int y, const cardbody::BodyGeometry& g, bool apply, bool tabbed) {
    for (size_t i = 0; i < cells.size();) {
        const size_t end = runEnd(cells, i);
        const std::vector<Cell> run(cells.begin() + (long)i, cells.begin() + (long)end);
        y = layoutRun(plan, module, run, columns, y, g, apply, tabbed);
        i = end;
    }
    return y;
}

} // namespace

int layoutCardBodyItems(const CardBodyPlan& plan, juce::AudioProcessor& module, const std::vector<int>& indices,
                        int columns, int y, const cardbody::BodyGeometry& g, bool apply, bool tabbed) {
    return layoutCells(plan, module, cellsOf(plan, indices, false), columns, y, g, apply, tabbed);
}

// A titled section starts with its header row across the content width; an untitled one (every
// automatic layout) has none, so those cards keep their exact geometry. A section whose visibleWhen
// does not hold takes no space; the footer is laid out on its own, under the card's chrome.
int layoutCardBodySections(const CardBodyPlan& plan, juce::AudioProcessor& module, int y,
                           const cardbody::BodyGeometry& g, bool apply, bool tabbed) {
    for (const auto& section : plan.sections) {
        if (!section.visible || section.footer)
            continue;
        if (section.hasHeader() && !tabbed) {
            if (apply && section.header != nullptr)
                section.header->setBounds(g.contentX, y, g.contentW, cardbody::kSectionHeaderHeight);
            y += cardbody::kSectionHeaderHeight;
        }
        y = layoutCells(plan, module, cellsOf(plan, section.items, true), section.columns, y, g, apply, tabbed);
    }
    return y;
}

int CardBody::layout(int y, const cardbody::BodyGeometry& g, bool apply, bool tabbed) const {
    return layoutCardBodySections(plan_, module_, y, g, apply, tabbed);
}

int CardBody::layoutItems(const std::vector<int>& indices, int columns, int y, const cardbody::BodyGeometry& g,
                          bool apply, bool tabbed) const {
    return layoutCardBodyItems(plan_, module_, indices, columns, y, g, apply, tabbed);
}

} // namespace synth
