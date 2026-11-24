// CardBodyLanes.cpp -- the lane grid and the mode row of a card body. A lane grid is consecutive `lane` sections side
// by side: each lane packs its cells into columns of a band two knob cells high (a column is filled top to bottom while
// the next cell fits, a lone cell in a column is centred), lanes are apart by a 1 px hairline with kLaneClear of
// clear space either side, a titled lane has its title as a header over a hairline, and the Out lane (small knobs) is
// always last and bottom aligned. Spare width is shared evenly among the lanes; when the lanes need more than the
// card has, the card widens (cardBodyLaneWidthNeeded). Cell widths are fixed per kind, not measured from the font
// (a pill's and a title's width come from the shared text measure), so the static estimate and the live card agree.
// docs/layout/module-card-layout.md#lane-presentation.
#include "CardBodyLanes.h"
#include "CardBodyCells.h"
#include "CardBodyLayoutWalk.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace synth {

namespace cardbody {

std::vector<std::vector<int>> packLaneColumns(const std::vector<int>& heights, int band) {
    std::vector<std::vector<int>> columns;
    int used = 0;
    for (int i = 0; i < (int)heights.size(); ++i) {
        if (columns.empty() || used + heights[(size_t)i] > band) {
            columns.emplace_back();
            used = 0;
        }
        columns.back().push_back(i);
        used += heights[(size_t)i];
    }
    return columns;
}

std::vector<int> shareLaneWidths(const std::vector<int>& needs, int available) {
    const int count = (int)needs.size();
    if (count == 0)
        return {};
    const int needed = std::accumulate(needs.begin(), needs.end(), 0) + (count - 1) * kLaneRuleWidth;
    const int spare = std::max(0, available - needed);
    std::vector<int> widths;
    for (int i = 0; i < count; ++i)
        widths.push_back(needs[(size_t)i] + spare / count + (i < spare % count ? 1 : 0));
    return widths;
}

int laneCardWidthFor(int lanesWidth, int modeWidth) {
    const int content = std::max(lanesWidth, modeWidth) + 2 * kContentMargin;
    return std::max(synth::LayoutUtil::kSingleWidth, (content + 3) / 4 * 4);
}

} // namespace cardbody

namespace {

using Kind = CardBodyItem::Kind;
using cardcells::Cell;
using Theme = synth::theme::AppLookAndFeel;
using namespace cardbody;

constexpr int kKnobCellWidth = 60;
constexpr int kKnobWidgetInset = 2; // a knob keeps this much of its cell either side, so two columns never touch
constexpr int kHeroCellWidth = 72;
constexpr int kPillHeight = Theme::kTogglePillHeight;

// ---- Cells -----------------------------------------------------------------------------------------------------

constexpr int kSyncButtonHeight = 16;
constexpr int kSyncButtonPad = 6;  // beside the word, inside the button
constexpr int kSyncCaptionPad = 6; // a caption's own side padding, as the caption box is sized to its words
constexpr int kSyncCaptionGap = 2; // between the caption and its Sync button
constexpr float kCaptionFontHeight = 13.0f;
constexpr int kFaderCaptionPad = 5; // a vertical fader's caption keeps this much beside its words

struct LaneCell {
    Cell cell;
    int width = 0;
    int height = 0;
    bool small = false;
    bool pill = false;
    int sync = -1;        // the Sync button (an item index) at the end of this cell's caption, or -1
    int captionWidth = 0; // that caption's box, sized to its words
    int syncWidth = 0;    // and the button
};

// The members of a cell's items: the swap group's, or the one item.
std::vector<int> cellMembers(const CardBodyPlan& plan, const Cell& cell) {
    if (cell.group >= 0)
        return plan.swapGroups[(size_t)cell.group].members;
    return cell.item >= 0 ? std::vector<int>{cell.item} : std::vector<int>{};
}

// The Sync button whose host is a member of `cell`: it takes no cell of its own (CardBodyItem::isLaneSync).
int syncButtonOf(const CardBodyPlan& plan, const Cell& cell) {
    const auto members = cellMembers(plan, cell);
    for (int i = 0; i < (int)plan.items.size(); ++i)
        if (plan.items[(size_t)i].isLaneSync(plan))
            for (int host : plan.items[(size_t)i].syncHosts)
                if (std::find(members.begin(), members.end(), host) != members.end())
                    return i;
    return -1;
}

int laneCellHeight(Kind kind) {
    switch (kind) {
    case Kind::Knob:
        return kLabelHeight + kKnobHeight;
    case Kind::KnobLarge:
        return kLabelHeight + kKnobLargeHeight;
    case Kind::FaderV:
        return kLabelHeight + kFaderVHeight;
    case Kind::FaderVLong:
        return kLabelHeight + kFaderVLongHeight;
    case Kind::FaderH:
        return kLabelHeight + kFaderHHeight;
    case Kind::XYPad:
        return kLabelHeight + kXYPadHeight;
    case Kind::Button:
        return kLabelHeight + kButtonHeight;
    case Kind::Choice:
    case Kind::Segmented:
    case Kind::Stepper:
        return kLabelHeight + kRowHeight;
    case Kind::Toggle:
        return kRowHeight;
    case Kind::View:
        break;
    }
    return 0;
}

int laneCellWidth(Kind kind) {
    switch (kind) {
    case Kind::Knob:
        return kKnobCellWidth;
    case Kind::KnobLarge:
        return kHeroCellWidth;
    case Kind::Choice:
        return 96;
    case Kind::Segmented:
    case Kind::FaderH:
        return 124;
    case Kind::Stepper:
        return 104;
    case Kind::FaderV:
    case Kind::FaderVLong:
        return 40;
    case Kind::XYPad:
        return kXYPadWidth;
    case Kind::Button:
        return kButtonWidth;
    case Kind::Toggle:
    case Kind::View:
        break;
    }
    return 0;
}

LaneCell laneCellOf(const CardBodyPlan& plan, const Cell& cell) {
    LaneCell out;
    out.cell = cell;
    const auto* item = cell.item >= 0 ? &plan.items[(size_t)cell.item] : nullptr;
    out.pill = item != nullptr && item->pill && cell.group < 0;
    out.small = item != nullptr && item->small && cell.group < 0;
    out.height = laneCellHeight(cell.runKind);
    out.width = laneCellWidth(cell.runKind);
    if (out.small) {
        out.width = kSmallKnobCellWidth;
        out.height = kSmallKnobCellHeight;
    } else if (out.pill) {
        out.width = Theme::togglePillWidth(item->captionText());
    } else if (item != nullptr && item->keyField) {
        out.width = kKeyFieldWidth;
    }
    if (CardBodyItem::isVerticalFader(cell.runKind) && !out.small &&
        !out.pill) // a long caption (Threshold) widens a narrow fader
        for (int member : cellMembers(plan, cell))
            out.width =
                std::max(out.width, Theme::uiTextWidth(plan.items[(size_t)member].captionText(), kCaptionFontHeight) +
                                        2 * kFaderCaptionPad);
    if (!out.small && !out.pill) {
        out.sync = syncButtonOf(plan, cell);
        if (out.sync >= 0) {
            for (int member : cellMembers(plan, cell))
                out.captionWidth = std::max(
                    out.captionWidth, Theme::uiTextWidth(plan.items[(size_t)member].captionText(), kCaptionFontHeight) +
                                          2 * kSyncCaptionPad);
            out.syncWidth =
                Theme::uiTextWidth(plan.items[(size_t)out.sync].captionText(), Theme::kTogglePillFontHeight) +
                2 * kSyncButtonPad;
            out.width = std::max(out.width, out.captionWidth + kSyncCaptionGap + out.syncWidth);
        }
    }
    out.height += kLaneCellGap; // a cell's slot in its column includes the gap to the next one
    return out;
}

// ---- Lanes -----------------------------------------------------------------------------------------------------

struct Lane {
    int section = -1;
    bool out = false;
    std::vector<LaneCell> cells;
    std::vector<std::vector<int>> columns;
    std::vector<int> columnWidths;
    int content = 0; // the columns, or the title when that is wider
    int titleWidth = 0;
    bool titled = false;
};

juce::String titleText(const CardBodyPlan::Section& section) { return section.title->trim(); }

// A title's width as the header draws it: bold, with a little letter spacing. No padding on top: the header label
// has no border, so a title as wide as its lane's controls fits without widening the lane.
int titleWidthOf(const juce::String& text) {
    return Theme::uiTextWidth(text, kLaneTitleFontHeight) +
           juce::roundToInt(kLaneTitleKerning * kLaneTitleFontHeight * (float)text.length());
}

// The lane grid that starts at section `first`: its lanes in layout order but for the Out lane, which goes last, and
// the band they share (the tallest control may grow it past kLaneBandHeight).
struct LaneGrid {
    std::vector<Lane> lanes;
    int band = kLaneBandHeight;
    bool titled = false;
};

// True when every control of `cell` is a `show` control whose condition does not hold now (a swap group with one
// member is such a cell; a group of alternatives always has one showing).
bool cellIsHiddenByCondition(const CardBodyPlan& plan, const Cell& cell) {
    std::vector<int> members;
    if (cell.group >= 0)
        members = plan.swapGroups[(size_t)cell.group].members;
    else if (cell.item >= 0)
        members = {cell.item};
    if (members.empty())
        return false;
    for (int member : members) {
        const auto& item = plan.items[(size_t)member];
        if (item.shown || !item.when.has_value() || item.when->effect != CardConditionEffect::Show)
            return false;
    }
    return true;
}

LaneGrid laneGridFrom(const CardBodyPlan& plan, int first) {
    LaneGrid grid;
    for (int s = first; s < (int)plan.sections.size() && plan.sections[(size_t)s].lane; ++s) {
        const auto& section = plan.sections[(size_t)s];
        // A lane a condition hides (the Filter's Morph lane outside Custom) takes no room; one fading out still does.
        if (!section.visible && !section.holding)
            continue;
        Lane lane;
        lane.section = s;
        lane.out = section.outLane;
        for (const auto& cell : cardcells::cellsOf(plan, section.items, true)) {
            if (cell.runKind == Kind::View || (cell.item < 0 && cell.group < 0))
                continue;
            // A control a condition shows only in some modes (the Filter's Shift and Depth) takes no room until then.
            if (cellIsHiddenByCondition(plan, cell))
                continue;
            lane.cells.push_back(laneCellOf(plan, cell));
            grid.band = std::max(grid.band, lane.cells.back().height - kLaneCellGap);
        }
        lane.titled = section.hasHeader();
        grid.titled = grid.titled || lane.titled;
        grid.lanes.push_back(std::move(lane));
    }
    std::stable_partition(grid.lanes.begin(), grid.lanes.end(), [](const Lane& lane) { return !lane.out; });
    // A grid whose lanes stack nothing (every column holds one cell) has no use for the two-knob band: it is as high as
    // its tallest cell, so a lone Level knob leaves no empty block under its title.
    bool stacked = false;
    int tallest = 0;
    for (const auto& lane : grid.lanes) {
        std::vector<int> heights;
        for (const auto& cell : lane.cells) {
            heights.push_back(cell.height);
            tallest = std::max(tallest, cell.height - kLaneCellGap);
        }
        for (const auto& column : packLaneColumns(heights, grid.band + kLaneCellGap))
            stacked = stacked || column.size() > 1;
    }
    if (!stacked && tallest > 0)
        grid.band = tallest;
    for (auto& lane : grid.lanes) {
        std::vector<int> heights;
        for (const auto& cell : lane.cells)
            heights.push_back(cell.height);
        lane.columns = packLaneColumns(heights, grid.band + kLaneCellGap);
        for (const auto& column : lane.columns) {
            int width = 0;
            for (int index : column)
                width = std::max(width, lane.cells[(size_t)index].width);
            lane.columnWidths.push_back(width);
            lane.content += (lane.columnWidths.size() > 1 ? kLaneColumnGap : 0) + width;
        }
        if (lane.titled) {
            lane.titleWidth = titleWidthOf(titleText(plan.sections[(size_t)lane.section]));
            lane.content = std::max(lane.content, lane.titleWidth);
        }
    }
    return grid;
}

// What a lane needs: its content, and kLaneClear beside each hairline it has.
std::vector<int> needsOf(const LaneGrid& grid) {
    std::vector<int> needs;
    const int count = (int)grid.lanes.size();
    for (int i = 0; i < count; ++i)
        needs.push_back(grid.lanes[(size_t)i].content + (i > 0 ? kLaneClear : 0) + (i < count - 1 ? kLaneClear : 0));
    return needs;
}

int gridWidthNeeded(const LaneGrid& grid) {
    const auto needs = needsOf(grid);
    return std::accumulate(needs.begin(), needs.end(), 0) + std::max(0, (int)needs.size() - 1) * kLaneRuleWidth;
}

int firstLaneSection(const CardBodyPlan& plan) {
    for (int s = 0; s < (int)plan.sections.size(); ++s)
        if (plan.sections[(size_t)s].lane)
            return s;
    return -1;
}

// A cell's caption and its Sync button as one unit across the top of the cell: the caption (sized to its words) and the
// button right after it, centred together. Every member of a swap group is captioned the same, so each takes the box.
void placeCaptionSync(const CardBodyPlan& plan, const LaneCell& cell, int x, int y) {
    const int total = cell.captionWidth + kSyncCaptionGap + cell.syncWidth;
    const int start = x + (cell.width - total) / 2;
    for (int member : cellMembers(plan, cell.cell))
        if (auto* label = dynamic_cast<juce::Label*>(plan.items[(size_t)member].label)) {
            label->setJustificationType(juce::Justification::centred);
            label->setBounds(start, y, cell.captionWidth, kCaptionBoxHeight);
        }
    if (auto* button = plan.items[(size_t)cell.sync].widget)
        button->setBounds(start + cell.captionWidth + kSyncCaptionGap, y + (kCaptionBoxHeight - kSyncButtonHeight) / 2,
                          cell.syncWidth, kSyncButtonHeight);
}

void placeLaneCellBody(const CardBodyPlan& plan, juce::AudioProcessor& module, const LaneCell& cell, int x, int y,
                       const cardbody::BodyGeometry& g) {
    const auto* item = cell.cell.item >= 0 ? &plan.items[(size_t)cell.cell.item] : nullptr;
    if (item == nullptr)
        return;
    if (cell.small) {
        if (item->label != nullptr)
            item->label->setBounds(x, y, cell.width, kSmallKnobCaptionHeight);
        if (item->widget != nullptr)
            item->widget->setBounds(x, y + kSmallKnobCaptionHeight, cell.width, kSmallKnobWidgetHeight);
    } else if (cell.pill) {
        if (item->widget != nullptr)
            item->widget->setBounds(x, y + (cell.height - kPillHeight) / 2, cell.width, kPillHeight);
    } else if (cell.cell.runKind == Kind::Knob && cell.cell.group < 0) {
        if (item->label != nullptr)
            item->label->setBounds(x, y, cell.width, kCaptionBoxHeight);
        if (item->widget != nullptr)
            item->widget->setBounds(x + kKnobWidgetInset, y + kLabelHeight, cell.width - 2 * kKnobWidgetInset,
                                    kKnobHeight);
    } else {
        cardcells::placeCellIn(plan, module, cell.cell, {x, y, cell.width, cell.height}, g);
    }
}

void placeLaneCell(const CardBodyPlan& plan, juce::AudioProcessor& module, const LaneCell& cell, int x, int y,
                   const cardbody::BodyGeometry& g) {
    placeLaneCellBody(plan, module, cell, x, y, g);
    if (cell.sync >= 0)
        placeCaptionSync(plan, cell, x, y);
}

void recordSpot(const CardBodyPlan& plan, const LaneCell& cell, int lane, int column, int row, int gridX, int gridY) {
    const auto spot = CardBodyItem::LaneSpot{lane, column, row, gridY, gridX};
    if (cell.sync >= 0) // the Sync button is a stop of its host's cell
        plan.items[(size_t)cell.sync].laneSpot = spot;
    if (cell.cell.group < 0) {
        if (cell.cell.item >= 0)
            plan.items[(size_t)cell.cell.item].laneSpot = spot;
        return;
    }
    for (int member : plan.swapGroups[(size_t)cell.cell.group].members)
        plan.items[(size_t)member].laneSpot = spot;
}

void placeLane(const CardBodyPlan& plan, juce::AudioProcessor& module, const Lane& lane, int index, int count,
               int slotX, int slotWidth, int y, int titleBand, int band, const cardbody::BodyGeometry& g) {
    const auto& section = plan.sections[(size_t)lane.section];
    const int usableX = slotX + (index > 0 ? kLaneClear : 0);
    const int usableW = slotWidth - (index > 0 ? kLaneClear : 0) - (index < count - 1 ? kLaneClear : 0);
    if (section.divider != nullptr)
        section.divider->setBounds(
            index > 0 ? juce::Rectangle<int>(slotX - kLaneRuleWidth, y, kLaneRuleWidth, titleBand + band)
                      : juce::Rectangle<int>());
    if (section.header != nullptr)
        section.header->setBounds(usableX, y, usableW, kLaneTitleHeight);
    if (section.titleRule != nullptr)
        section.titleRule->setBounds(lane.titled
                                         ? juce::Rectangle<int>(usableX, y + kLaneTitleHeight, usableW, kLaneRuleWidth)
                                         : juce::Rectangle<int>());
    section.cellTop = y + titleBand;
    section.cellBottom = y + titleBand + band;
    const int bandTop = y + titleBand;
    int columnX = usableX + (usableW - lane.content) / 2;
    int columnsWidth = 0;
    for (size_t c = 0; c < lane.columns.size(); ++c)
        columnsWidth += (c > 0 ? kLaneColumnGap : 0) + lane.columnWidths[c];
    columnX += (lane.content - columnsWidth) / 2; // a title wider than the columns leaves them centred under it
    for (size_t c = 0; c < lane.columns.size(); ++c) {
        const auto& column = lane.columns[c];
        int height = 0;
        for (int i : column)
            height += lane.cells[(size_t)i].height;
        height -= kLaneCellGap; // no gap after a column's last cell
        // The Out lane sits on the band's bottom edge, a lone cell in a column is centred, the rest start at the top.
        int cellY = bandTop;
        if (lane.out)
            cellY = bandTop + band - height;
        else if (column.size() == 1)
            cellY = bandTop + (band - height) / 2;
        int row = 0;
        for (int i : column) {
            const auto& cell = lane.cells[(size_t)i];
            recordSpot(plan, cell, index, (int)c, row, g.contentX, bandTop);
            placeLaneCell(plan, module, cell, columnX + (lane.columnWidths[c] - cell.width) / 2, cellY, g);
            cellY += cell.height;
            ++row;
        }
        columnX += lane.columnWidths[c] + kLaneColumnGap;
    }
}

// ---- The mode row ----------------------------------------------------------------------------------------------

// A caption drawn inline before its control, in the pill's text size (as the footer does).
int inlineCaptionWidth(const juce::String& text) {
    constexpr int kGap = 6;
    return Theme::uiTextWidth(text, Theme::kTogglePillFontHeight) + kGap + (kCaptionSideInset - 5);
}

// One entry at the mode row's right end: a control, a swap group of pills sharing one cell (`members`, each as wide as
// its own text, the cell as wide as the widest), or a card chrome toggle (Show Scope: `chrome`, its text in `text`).
struct ModeItem {
    int item = -1;
    std::vector<int> members;
    juce::Component* chrome = nullptr;
    int width = 0;
    int captionWidth = 0; // an inline caption (a choice at the right end); 0 for a pill
    bool pill = false;
};

struct ModeRow {
    int selector = -1; // the first control, on the left
    int selectorMin = 0;
    std::vector<ModeItem> right;
    int rightWidth = 0;
};

ModeRow modeRowOf(const CardBodyPlan& plan, int sectionIndex) {
    ModeRow row;
    for (const auto& cell : cardcells::cellsOf(plan, plan.sections[(size_t)sectionIndex].items, true)) {
        if (cell.item < 0 || cell.runKind == Kind::View)
            continue;
        const auto& item = plan.items[(size_t)cell.item];
        if (plan.isModeSelector(cell.item)) {
            row.selector = cell.item;
            const auto labels = item.param != nullptr ? cardBodySegmentLabels(*item.param) : juce::StringArray();
            // A segment squeezes its name to 70% before it ellipsises, so each one is as wide as its longest name
            // needs.
            int segmentWidth = kModeSegmentWidth;
            for (const auto& label : labels)
                segmentWidth = std::max(segmentWidth, (int)std::ceil(Theme::uiTextWidth(label, 15.0f) * 0.7f) + 12);
            row.selectorMin = item.kind == Kind::Segmented ? std::max(2, labels.size()) * segmentWidth : 96;
            continue;
        }
        ModeItem entry;
        entry.item = cell.item;
        entry.pill = item.pill;
        if (cell.group >= 0) {
            entry.members = plan.swapGroups[(size_t)cell.group].members;
            for (int member : entry.members)
                entry.width = std::max(entry.width, Theme::togglePillWidth(plan.items[(size_t)member].captionText()));
        } else if (item.pill) {
            entry.members = {cell.item};
            entry.width = Theme::togglePillWidth(item.captionText());
        } else {
            entry.members = {cell.item};
            entry.captionWidth = inlineCaptionWidth(item.captionText());
            entry.width = entry.captionWidth + kModeInlineControlWidth;
        }
        row.rightWidth += (row.right.empty() ? 0 : kFooterGap) + entry.width;
        row.right.push_back(entry);
    }
    // The card's chrome toggles (Show Scope) are pills after the layout's own, in the order the card gives them.
    for (const auto& pill : plan.modeRowPills) {
        ModeItem entry;
        entry.chrome = pill.widget;
        entry.pill = true;
        entry.width = Theme::togglePillWidth(pill.text);
        row.rightWidth += (row.right.empty() ? 0 : kFooterGap) + entry.width;
        row.right.push_back(entry);
    }
    return row;
}

int modeRowWidthNeeded(const ModeRow& row) {
    if (row.selector < 0)
        return row.rightWidth;
    return row.selectorMin + (row.right.empty() ? 0 : kBlockGap + row.rightWidth);
}

} // namespace

int cardBodyLaneWidthNeeded(const CardBodyPlan& plan) {
    const int first = firstLaneSection(plan);
    if (first < 0)
        return 0;
    int mode = 0;
    for (int s = 0; s < (int)plan.sections.size(); ++s)
        if (plan.sections[(size_t)s].mode)
            mode = std::max(mode, modeRowWidthNeeded(modeRowOf(plan, s)));
    return laneCardWidthFor(gridWidthNeeded(laneGridFrom(plan, first)), mode);
}

int layoutCardBodyLanes(const CardBodyPlan& plan, juce::AudioProcessor& module, int first, int y,
                        const cardbody::BodyGeometry& g, bool apply) {
    const auto grid = laneGridFrom(plan, first);
    if (grid.lanes.empty())
        return y;
    const int titleBand = grid.titled ? kLaneTitleBand : 0;
    if (apply) {
        for (int s = first; s < (int)plan.sections.size() && plan.sections[(size_t)s].lane; ++s) {
            const auto& hidden = plan.sections[(size_t)s];
            if (hidden.visible || hidden.holding)
                continue;
            for (auto* part : {hidden.divider, hidden.header, hidden.titleRule}) // no stale place for a hidden lane
                if (part != nullptr)
                    part->setBounds({});
        }
        const auto widths = shareLaneWidths(needsOf(grid), g.contentW);
        int x = g.contentX;
        for (int i = 0; i < (int)grid.lanes.size(); ++i) {
            placeLane(plan, module, grid.lanes[(size_t)i], i, (int)grid.lanes.size(), x, widths[(size_t)i], y,
                      titleBand, grid.band, g);
            x += widths[(size_t)i] + kLaneRuleWidth;
        }
    }
    return y + titleBand + grid.band;
}

int layoutCardBodyModeRow(const CardBodyPlan& plan, juce::AudioProcessor&, int section, int y,
                          const cardbody::BodyGeometry& g, bool apply) {
    const auto row = modeRowOf(plan, section);
    const bool selector = row.selector >= 0;
    const int widgetY = y + (selector ? kLabelHeight : 0);
    if (apply) {
        plan.modeRowTop = y;
        plan.modeRowBottom = widgetY + kRowHeight;
        int right = g.contentX + g.contentW;
        for (auto it = row.right.rbegin(); it != row.right.rend(); ++it) {
            const int x = right - it->width;
            if (it->chrome != nullptr) {
                it->chrome->setBounds(x, widgetY + (kRowHeight - kPillHeight) / 2, it->width, kPillHeight);
            } else {
                for (int member : it->members) {
                    const auto& item = plan.items[(size_t)member];
                    if (it->pill) {
                        if (item.widget != nullptr)
                            item.widget->setBounds(x, widgetY + (kRowHeight - kPillHeight) / 2,
                                                   Theme::togglePillWidth(item.captionText()), kPillHeight);
                    } else {
                        if (item.label != nullptr)
                            item.label->setBounds(x, widgetY, it->captionWidth, kRowHeight);
                        if (item.widget != nullptr)
                            item.widget->setBounds(x + it->captionWidth, widgetY, kModeInlineControlWidth, kRowHeight);
                    }
                }
            }
            right = x - kFooterGap;
        }
        if (selector) {
            const auto& item = plan.items[(size_t)row.selector];
            const int width = g.contentW - (row.right.empty() ? 0 : row.rightWidth + kBlockGap);
            if (item.label != nullptr)
                item.label->setBounds(g.contentX, y, width, kCaptionBoxHeight);
            if (item.widget != nullptr)
                item.widget->setBounds(g.contentX, widgetY, width, kRowHeight);
        }
    }
    return widgetY + kRowHeight;
}

} // namespace synth
