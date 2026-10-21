// CardBodyLayout.cpp -- laying a card body out: each shown section's items in card order, as cells (one
// item, or one swap group showing whichever member's condition holds), grouped into runs of one kind (a
// combo stack, toggle rows, a knob grid, a full-width view) placed by the shared run layouts; a run of
// tab sections is one tab strip over the selected tab. One function measures (apply = false) and places
// (apply = true), so the measured height and the real positions cannot drift apart; with null widgets
// (a plan never built) it measures statically. The footer row is CardBodyFooter.cpp.
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
        // A one-row member sits in the middle of a taller cell; its caption stays on the cell's top edge like the
        // taller member's.
        const int captionY = bounds.getY();
        if (plan.swapGroups[(size_t)cell.group].cellKind == Kind::FaderV && item.kind != Kind::FaderV &&
            area.getHeight() > bounds.getHeight())
            bounds.setY(area.getY() + (area.getHeight() - bounds.getHeight()) / 2);
        item.widget->setBounds(bounds);
        if (item.label != nullptr)
            item.label->setBounds(bounds.getX(), captionY - cardbody::kLabelHeight, bounds.getWidth(),
                                  cardbody::kLabelHeight);
    }
}

// A view's height now: a live view that folds parts of itself in and out (the step grid) says so; a plan that is only
// measured, and every other view, uses the factory's final height.
int viewHeightOf(const CardBodyItem& view, const CardViewFactory& factory, juce::AudioProcessor& module) {
    if (const auto* animated = dynamic_cast<const CardAnimatedHeightView*>(view.widget))
        return animated->currentLayoutHeight();
    return factory.preferredHeight(module);
}

int layoutRunKind(Kind kind, const std::vector<cardbody::CaptionedWidget>& captioned,
                  const std::vector<juce::Component*>& plain, int columns, int y, const cardbody::BodyGeometry& g,
                  bool apply) {
    switch (kind) {
    case Kind::Choice:
        return cardbody::layoutChoiceRun(captioned, y, g, apply);
    case Kind::Toggle:
        return cardbody::layoutToggleRun(plain, y, g, apply);
    case Kind::Knob:
        return cardbody::layoutKnobRun(captioned, columns, y, g, apply);
    case Kind::KnobLarge:
        return cardbody::layoutGridRun(captioned, columns, cardbody::kKnobLargeHeight, 0, y, g, apply);
    case Kind::FaderV:
        return cardbody::layoutGridRun(captioned, columns, cardbody::kFaderVHeight,
                                       synth::ui::CardFader::kVerticalWidth, y, g, apply);
    case Kind::FaderH:
        return cardbody::layoutCaptionedRows(captioned, cardbody::kFaderHHeight, true, y, g, apply);
    case Kind::Segmented:
        return cardbody::layoutCaptionedRows(captioned, cardbody::kRowHeight, true, y, g, apply);
    case Kind::Stepper:
        return cardbody::layoutCaptionedRows(captioned, cardbody::kRowHeight, false, y, g, apply);
    case Kind::View:
        break;
    }
    return y;
}

int layoutRun(const CardBodyPlan& plan, juce::AudioProcessor& module, const std::vector<Cell>& run, int columns, int y,
              const cardbody::BodyGeometry& g, bool apply) {
    const auto kind = run.front().runKind;
    if (kind == Kind::View) {
        const auto& view = plan.items[(size_t)run.front().item];
        const auto* factory = findCardViewFactory(view.view);
        const float shown = view.reveal >= 0.0f ? view.reveal : (view.open ? 1.0f : 0.0f);
        if (factory == nullptr || shown <= 0.0f || (apply && view.widget == nullptr))
            return y;
        // A view fading in or out takes its height in step, so the rows around it slide.
        return cardbody::layoutViewRow(
            view.widget, juce::roundToInt((float)viewHeightOf(view, *factory, module) * shown), y, g, apply);
    }
    std::vector<cardbody::CaptionedWidget> captioned;
    std::vector<juce::Component*> plain;
    for (const auto& cell : run) {
        const auto* item = cell.item >= 0 ? &plan.items[(size_t)cell.item] : nullptr;
        captioned.emplace_back(item != nullptr ? item->widget : nullptr, item != nullptr ? item->label : nullptr);
        plain.push_back(item != nullptr ? item->widget : nullptr);
    }
    y = layoutRunKind(kind, captioned, plain, columns, y, g, apply);
    if (apply)
        for (const auto& cell : run)
            if (cell.group >= 0 && cell.item >= 0)
                fitSwapCell(plan, cell);
    return y;
}

int layoutCells(const CardBodyPlan& plan, juce::AudioProcessor& module, const std::vector<Cell>& cells, int columns,
                int y, const cardbody::BodyGeometry& g, bool apply) {
    for (size_t i = 0; i < cells.size();) {
        const size_t end = runEnd(cells, i);
        const std::vector<Cell> run(cells.begin() + (long)i, cells.begin() + (long)end);
        y = layoutRun(plan, module, run, columns, y, g, apply);
        i = end;
    }
    return y;
}

// A freeform section's cell: its size, and the free position of its item (a swap group's first member
// that has one), nullopt = flows after the positioned cells.
struct FreeCell {
    Cell cell;
    juce::Rectangle<int> size; // width and height only
    std::optional<juce::Point<int>> at;
};

// The cell's natural size: a view's by its factory, any other kind's by cardBodyCellSize.
juce::Rectangle<int> naturalCellSize(const CardBodyPlan& plan, juce::AudioProcessor& module, const Cell& cell,
                                     int columns, const cardbody::BodyGeometry& g) {
    if (cell.runKind != Kind::View) {
        const auto size = cardBodyCellSize(cell.runKind, columns, g);
        return {size.x, size.y};
    }
    const auto& view = plan.items[(size_t)cell.item];
    const auto* factory = findCardViewFactory(view.view);
    const float shown = view.reveal >= 0.0f ? view.reveal : (view.open ? 1.0f : 0.0f);
    return {g.contentW, factory != nullptr ? juce::roundToInt((float)viewHeightOf(view, *factory, module) * shown) : 0};
}

std::optional<juce::Point<int>> positionOf(const CardBodyPlan& plan, const Cell& cell, int section) {
    if (cell.group < 0)
        return cell.item >= 0 ? plan.atIn(cell.item, section) : std::nullopt;
    for (int member : plan.swapGroups[(size_t)cell.group].members)
        if (const auto at = plan.atIn(member, section))
            return at;
    return std::nullopt;
}

// One cell placed exactly as its run layout would place a run of one, in a geometry that is the cell's
// own box (not a double-width card, so a combo stays one column; one column, so a knob fills the box
// instead of a third of it).
void placeFreeCell(const CardBodyPlan& plan, juce::AudioProcessor& module, const FreeCell& free,
                   juce::Point<int> topLeft, const cardbody::BodyGeometry& g) {
    auto cellGeometry = g;
    cellGeometry.width = 0;
    cellGeometry.contentX = cellGeometry.narrowX = topLeft.x;
    cellGeometry.contentW = cellGeometry.narrowW = free.size.getWidth();
    layoutRun(plan, module, {free.cell}, 1, topLeft.y, cellGeometry, true);
}

// A freeform section: cells with a position sit at it (relative to the content origin and the section's
// top, kept inside the content width); the rest flow after them, left to right in rows below the lowest
// positioned cell. Measuring and placing share the walk, so the height cannot drift.
int layoutFreeSection(const CardBodyPlan& plan, juce::AudioProcessor& module, const CardBodyPlan::Section& section,
                      int top, const cardbody::BodyGeometry& g, bool apply) {
    std::vector<FreeCell> positioned;
    std::vector<FreeCell> flowing;
    const int sectionIndex = (int)(&section - plan.sections.data());
    for (const auto& cell : cellsOf(plan, section.items, true)) {
        FreeCell free{cell, naturalCellSize(plan, module, cell, section.columns, g),
                      positionOf(plan, cell, sectionIndex)};
        if (free.size.isEmpty())
            continue;
        (free.at ? positioned : flowing).push_back(free);
    }
    int bottom = top;
    const auto place = [&](const FreeCell& free, juce::Point<int> topLeft) {
        if (apply)
            placeFreeCell(plan, module, free, topLeft, g);
        bottom = std::max(bottom, topLeft.y + free.size.getHeight());
    };
    for (const auto& free : positioned) {
        const int maxX = std::max(g.contentX, g.contentX + g.contentW - free.size.getWidth());
        place(free, {juce::jlimit(g.contentX, maxX, g.contentX + free.at->x), std::max(top, top + free.at->y)});
    }
    int x = g.contentX;
    int rowY = bottom;
    int rowBottom = bottom;
    for (const auto& free : flowing) {
        if (x > g.contentX && x + free.size.getWidth() > g.contentX + g.contentW) {
            x = g.contentX;
            rowY = rowBottom;
        }
        place(free, {x, rowY});
        rowBottom = std::max(rowBottom, rowY + free.size.getHeight());
        x += free.size.getWidth();
    }
    return bottom > top ? bottom + 6 : top;
}

int layoutSectionCells(const CardBodyPlan& plan, juce::AudioProcessor& module, const CardBodyPlan::Section& section,
                       int y, const cardbody::BodyGeometry& g, bool apply) {
    if (section.freeform)
        return layoutFreeSection(plan, module, section, y, g, apply);
    return layoutCells(plan, module, cellsOf(plan, section.items, true), section.columns, y, g, apply);
}

// A tab group: the strip across the content width, a gap either side of it, then the selected tab's
// section. The group takes
// its tallest tab's height whichever is selected, so a tab switch never resizes the card; the other
// tabs are only measured (their widgets are hidden and keep their bounds).
int layoutTabGroup(const CardBodyPlan& plan, juce::AudioProcessor& module, const CardBodyPlan::TabGroup& group, int y,
                   const cardbody::BodyGeometry& g, bool apply) {
    y += cardbody::kTabStripGap;
    if (apply && group.strip != nullptr)
        group.strip->setBounds(g.contentX, y, g.contentW, cardbody::kRowHeight);
    y += cardbody::kRowHeight + cardbody::kTabStripGap;
    int tallest = 0;
    for (int tab = 0; tab < (int)group.sections.size(); ++tab) {
        const auto& section = plan.sections[(size_t)group.sections[(size_t)tab]];
        if (!section.visible)
            continue;
        const bool selected = tab == group.selected;
        tallest = std::max(tallest, layoutSectionCells(plan, module, section, y, g, apply && selected) - y);
    }
    return y + tallest;
}

// One section: its header row (a titled one), then its cells; `cellTop` and `cellBottom` are written when placing.
int layoutOneSection(const CardBodyPlan& plan, juce::AudioProcessor& module, const CardBodyPlan::Section& section,
                     int y, const cardbody::BodyGeometry& g, bool apply) {
    if (section.hasHeader()) {
        if (apply && section.header != nullptr)
            section.header->setBounds(g.contentX, y, g.contentW, cardbody::kSectionHeaderHeight);
        y += cardbody::kSectionHeaderHeight;
    }
    if (apply)
        section.cellTop = y;
    y = layoutSectionCells(plan, module, section, y, g, apply);
    if (apply)
        section.cellBottom = y;
    return y;
}

// An alternative group (the ADSR's Time and Tempo looks): every member is measured from the same y and the
// group takes the tallest, so the card keeps one size whichever look is shown; only the first member that
// holds is placed. A member that is not shown keeps the bounds it had.
int layoutAltGroup(const CardBodyPlan& plan, juce::AudioProcessor& module, const CardBodyPlan::AltGroup& group, int y,
                   const cardbody::BodyGeometry& g, bool apply) {
    int tallest = 0;
    bool placed = false;
    for (int s : group.sections) {
        const auto& section = plan.sections[(size_t)s];
        const bool place = apply && section.visible && !placed;
        placed = placed || place;
        tallest = std::max(tallest, layoutOneSection(plan, module, section, y, g, place) - y);
    }
    return y + tallest;
}

// ---- Row presentation -------------------------------------------------------------------------------------------
// Consecutive `row` sections sit side by side: each is a block (its header row over its cells in one line), blocks
// flow left to right and wrap to a new line only when the card is too narrow. Cell widths are fixed per kind (not
// measured from the font) so the estimate and the real card agree.
constexpr int kRowBlockGap = 14;
constexpr int kRowCellGap = 4;
constexpr int kRowLineGap = 6;

int rowCellWidth(Kind kind) {
    switch (kind) {
    case Kind::Knob:
        return 56;
    case Kind::KnobLarge:
        return 64;
    case Kind::Toggle:
        return 60;
    case Kind::Choice:
        return 96;
    case Kind::Segmented:
        return 124;
    case Kind::Stepper:
        return 104;
    case Kind::FaderH:
        return 124;
    case Kind::FaderV:
        return 40;
    case Kind::View:
        break;
    }
    return 0;
}

struct RowBlock {
    int section = -1;
    std::vector<Cell> cells;
    int width = 0;
    int cellHeight = 0;
};

RowBlock rowBlockOf(const CardBodyPlan& plan, int sectionIndex, const cardbody::BodyGeometry& g) {
    RowBlock block;
    block.section = sectionIndex;
    const auto& section = plan.sections[(size_t)sectionIndex];
    for (const auto& cell : cellsOf(plan, section.items, true)) {
        if (cell.runKind == Kind::View)
            continue;
        block.cells.push_back(cell);
        block.width += (block.cells.size() > 1 ? kRowCellGap : 0) + rowCellWidth(cell.runKind);
        block.cellHeight = std::max(block.cellHeight, cardBodyCellSize(cell.runKind, 1, g).y);
    }
    return block;
}

// The run of row sections that starts at `first`: the blocks of the ones shown now.
std::vector<RowBlock> rowBlocksFrom(const CardBodyPlan& plan, int first, const cardbody::BodyGeometry& g) {
    std::vector<RowBlock> blocks;
    for (int s = first; s < (int)plan.sections.size() && plan.sections[(size_t)s].row; ++s) {
        const auto& section = plan.sections[(size_t)s];
        if (section.visible || section.holding)
            blocks.push_back(rowBlockOf(plan, s, g));
    }
    return blocks;
}

void placeRowBlock(const CardBodyPlan& plan, juce::AudioProcessor& module, const RowBlock& block, int x, int y,
                   int headerH, int cellBand, bool lineStart, const cardbody::BodyGeometry& g) {
    const auto& section = plan.sections[(size_t)block.section];
    // A 1 px rule centred in the gap before the block, as tall as its controls; none at the start of a line.
    if (section.divider != nullptr)
        section.divider->setBounds(lineStart ? juce::Rectangle<int>()
                                             : juce::Rectangle<int>(x - kRowBlockGap / 2, y + headerH, 1, cellBand));
    if (section.header != nullptr)
        section.header->setBounds(x, y, std::max(block.width, 60), cardbody::kSectionHeaderHeight);
    section.cellTop = y + headerH;
    section.cellBottom = y + headerH + cellBand;
    int cellX = x;
    for (const auto& cell : block.cells) {
        const int w = rowCellWidth(cell.runKind);
        const int h = cardBodyCellSize(cell.runKind, 1, g).y;
        // A cell shorter than the line (a pill beside knobs) sits at the line's vertical middle.
        const int cellY = y + headerH + (cellBand - h) / 2;
        if (cell.item >= 0 || cell.group >= 0) {
            FreeCell free{cell, juce::Rectangle<int>(0, 0, w, h), std::nullopt};
            if (cell.item >= 0)
                placeFreeCell(plan, module, free, {cellX, cellY}, g);
        }
        cellX += w + kRowCellGap;
    }
}

// The group of row sections starting at `first`: lines of blocks. Measuring and placing share the walk.
int layoutRowGroup(const CardBodyPlan& plan, juce::AudioProcessor& module, int first, int y,
                   const cardbody::BodyGeometry& g, bool apply) {
    const auto blocks = rowBlocksFrom(plan, first, g);
    size_t i = 0;
    while (i < blocks.size()) {
        size_t end = i;
        int used = 0;
        for (; end < blocks.size(); ++end) {
            const int next = used + (end > i ? kRowBlockGap : 0) + std::max(blocks[end].width, 60);
            if (end > i && next > g.contentW)
                break;
            used = next;
        }
        bool headed = false;
        int band = 0;
        for (size_t b = i; b < end; ++b) {
            headed = headed || plan.sections[(size_t)blocks[b].section].hasHeader();
            band = std::max(band, blocks[b].cellHeight);
        }
        const int headerH = headed ? cardbody::kSectionHeaderHeight : 0;
        if (apply) {
            int x = g.contentX;
            for (size_t b = i; b < end; ++b) {
                placeRowBlock(plan, module, blocks[b], x, y, headerH, band, b == i, g);
                x += std::max(blocks[b].width, 60) + kRowBlockGap;
            }
        }
        y += headerH + band + kRowLineGap;
        i = end;
    }
    return y;
}

} // namespace

// The widths and heights the run layouts give a cell of its kind.
juce::Point<int> cardBodyCellSize(CardBodyItem::Kind kind, int columns, const cardbody::BodyGeometry& g) {
    using namespace cardbody;
    const int gridW = gridCellWidth(columns, g);
    switch (kind) {
    case Kind::Knob:
        return {gridW, kLabelHeight + kKnobHeight};
    case Kind::KnobLarge:
        return {gridW, kLabelHeight + kKnobLargeHeight};
    case Kind::FaderV:
        return {gridW, kLabelHeight + kFaderVHeight};
    case Kind::FaderH:
        return {g.contentW, kLabelHeight + kFaderHHeight};
    case Kind::Segmented:
        return {g.contentW, kLabelHeight + kRowHeight};
    case Kind::Stepper:
    case Kind::Choice:
        return {g.narrowW, kLabelHeight + kRowHeight};
    case Kind::Toggle:
        return {g.contentW, kRowHeight};
    case Kind::View:
        break;
    }
    return {};
}

int layoutCardBodyItems(const CardBodyPlan& plan, juce::AudioProcessor& module, const std::vector<int>& indices,
                        int columns, int y, const cardbody::BodyGeometry& g, bool apply) {
    return layoutCells(plan, module, cellsOf(plan, indices, false), columns, y, g, apply);
}

// A titled section starts with its header row across the content width; an untitled one (every
// automatic layout) has none, so those cards keep their exact geometry. A section whose visibleWhen
// does not hold takes no space; the footer is laid out on its own, under the card's chrome. A run of
// tab sections is laid out once, as its tab group, where its first section stands.
int layoutCardBodySections(const CardBodyPlan& plan, juce::AudioProcessor& module, int y,
                           const cardbody::BodyGeometry& g, bool apply) {
    for (int s = 0; s < (int)plan.sections.size(); ++s) {
        const auto& section = plan.sections[(size_t)s];
        if (section.footer)
            continue;
        if (section.row) {
            if (s == 0 || !plan.sections[(size_t)s - 1].row)
                y = layoutRowGroup(plan, module, s, y, g, apply);
            continue;
        }
        if (section.tabGroup >= 0) {
            const auto& group = plan.tabGroups[(size_t)section.tabGroup];
            if (group.sections.front() == s)
                y = layoutTabGroup(plan, module, group, y, g, apply);
            continue;
        }
        if (section.altGroup >= 0) {
            const auto& group = plan.altGroups[(size_t)section.altGroup];
            if (group.sections.front() == s)
                y = layoutAltGroup(plan, module, group, y, g, apply);
            continue;
        }
        if (!section.visible && !section.holding) // a holding one is still on its way out
            continue;
        y = layoutOneSection(plan, module, section, y, g, apply);
    }
    return y;
}

int CardBody::layout(int y, const cardbody::BodyGeometry& g, bool apply) const {
    refreshReveals();
    return layoutCardBodySections(plan_, module_, y, g, apply);
}

int CardBody::layoutItems(const std::vector<int>& indices, int columns, int y, const cardbody::BodyGeometry& g,
                          bool apply) const {
    return layoutCardBodyItems(plan_, module_, indices, columns, y, g, apply);
}

} // namespace synth
