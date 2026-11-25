// OnCardCells.cpp -- reading the on-card editor's cells off a card's plan and writing their free
// positions into a layout.
#include "OnCardCells.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/CardBody/CardBodyLayoutWalk.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <algorithm>
#include <map>
#include <utility>

namespace synth::ui {

namespace {

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

// A cell for `members`, or nothing when no member is drawn on the card now (a view, an empty swap cell). The
// cell is the box the card gives its kind, not just the widget in it: a position written for the cell puts that
// box there and centres the widget in it, so a narrower widget (a fader) must not be taken for the whole box.
// A footer, tab, lane or mode-row cell never moves and is laid out by its own row, not centred in a box: widening it
// would push its outline off the card's edge.
bool makeCell(const CardBodyPlan& plan, int sectionIndex, const std::vector<int>& members,
              const cardbody::BodyGeometry& geometry, bool widen, OnCardCell& cell) {
    const auto& section = plan.sections[(size_t)sectionIndex];
    cell.panelOnly = section.footer || section.tabGroup >= 0 || section.lane || section.mode;
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
    const auto box = cardBodyCellSize(shown->kind, section.columns, geometry);
    const int widgetWidth = rect.getWidth();
    if (widen && !cell.panelOnly && !shown->pill && box.x > widgetWidth)
        rect = rect.withX(rect.getX() - (box.x - widgetWidth) / 2).withWidth(box.x);
    cell.key = shown->param->paramID;
    cell.caption = shown->captionText();
    cell.section = sectionIndex;
    cell.rect = rect;
    cell.widget = shown->widget;
    cell.widgetRel = shown->widget->getBounds().translated(-rect.getX(), -rect.getY());
    cell.widgetInk = widgetInkRect(*shown->widget).translated((float)-rect.getX(), (float)-rect.getY());
    if (shown->label != nullptr && shown->label->isVisible()) {
        cell.label = shown->label;
        cell.labelRel = shown->label->getBounds().translated(-rect.getX(), -rect.getY());
        if (auto* caption = dynamic_cast<juce::Label*>(shown->label); caption != nullptr)
            cell.captionText = captionTextRect(*caption).translated((float)-rect.getX(), (float)-rect.getY());
    }
    return true;
}

} // namespace

// The outlines of the very glyphs Label::paint draws (the same fitted-text arrangement), so the rectangle is the real
// ink: the font's line box is taller than its letters and would leave a gap the outline never needed.
juce::Rectangle<float> captionTextRect(juce::Label& label) {
    const auto font = label.getLookAndFeel().getLabelFont(label);
    const auto area = label.getBorderSize().subtractedFrom(label.getLocalBounds()).toFloat();
    juce::GlyphArrangement glyphs;
    glyphs.addFittedText(font, label.getText(), area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                         label.getJustificationType(), std::max(1, (int)(area.getHeight() / font.getHeight())),
                         label.getMinimumHorizontalScale());
    juce::Path ink;
    glyphs.createPath(ink);
    return ink.getBounds().translated((float)label.getX(), (float)label.getY());
}

// Painted at 2x so a hairline tick counts; a pixel counts once it is a tenth opaque (fainter is anti-aliasing).
juce::Rectangle<float> widgetInkRect(juce::Component& widget) {
    if (widget.getWidth() <= 0 || widget.getHeight() <= 0)
        return {};
    if (widget.isOpaque())
        return widget.getBounds().toFloat();
    constexpr int scale = 2;
    juce::Image image(juce::Image::ARGB, widget.getWidth() * scale, widget.getHeight() * scale, true,
                      juce::SoftwareImageType());
    {
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale((float)scale));
        widget.paintEntireComponent(g, true);
    }
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    int left = image.getWidth(), top = image.getHeight(), right = -1, bottom = -1;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            if (pixels.getPixelColour(x, y).getAlpha() >= 26) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
    if (right < 0)
        return {};
    return juce::Rectangle<float>((float)left, (float)top, (float)(right + 1 - left), (float)(bottom + 1 - top)) /
               (float)scale +
           widget.getPosition().toFloat();
}

std::vector<OnCardCell> collectCells(const ModuleComponent& card) {
    std::vector<OnCardCell> cells;
    std::vector<std::pair<int, std::vector<int>>> sources; // each cell's section and members, to rebuild it narrower
    const auto* body = card.getCardBody();
    if (body == nullptr)
        return cells;
    const auto& plan = body->getPlan();
    const auto g = card.bodyGeometry();
    for (int s = 0; s < (int)plan.sections.size(); ++s) {
        const auto& section = plan.sections[(size_t)s];
        if (!section.visible)
            continue;
        for (size_t i = 0; i < section.items.size();) {
            const auto members = membersAt(plan, section, i);
            i += members.size();
            OnCardCell cell;
            if (plan.items[(size_t)members.front()].kind != CardBodyItem::Kind::View &&
                makeCell(plan, s, members, g, true, cell)) {
                cells.push_back(std::move(cell));
                sources.emplace_back(s, members);
            }
        }
    }
    // A cell takes the whole box its card gives it unless that runs into another cell (the Sequencer's knobs sit closer
    // together than the box is wide): then it is the widget and caption alone.
    for (size_t i = 0; i < cells.size(); ++i) {
        const bool runsIntoAnother = std::any_of(cells.begin(), cells.end(), [&](const OnCardCell& other) {
            return &other != &cells[i] && other.rect.intersects(cells[i].rect);
        });
        if (runsIntoAnother)
            makeCell(plan, sources[i].first, sources[i].second, g, false, cells[i]);
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

oncard::Limits limitsFor(const synth::CardBody& body, int section, const cardbody::BodyGeometry& g) {
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
    struct Placed {
        juce::Point<int> at;
        int planSection;
    };
    std::map<juce::String, Placed> positions;
    for (const auto& cell : cells)
        for (const auto& id : cell.paramIds)
            positions[id] = {{cell.rect.getX() - contentX, cell.rect.getY() - top}, cell.section};
    int planSection = 0; // the footer comes last in the plan whatever its place in the layout
    for (auto& section : layout.sections) {
        const int thisSection = section.id == CardSection::kFooterId ? -1 : planSection++;
        for (auto& item : section.items)
            if (auto* param = std::get_if<CardParamItem>(&item)) {
                // A parameter named in two looks of a card (the ADSR's Sustain) has a place in each; only the
                // look the cell was dragged in is written.
                if (const auto found = positions.find(param->paramId);
                    found != positions.end() &&
                    (found->second.planSection < 0 || found->second.planSection == thisSection))
                    param->at = found->second.at;
            } else if (auto* view = std::get_if<CardViewItem>(&item)) {
                for (const auto& shown : views)
                    if (shown.view == view->view)
                        view->at = juce::Point<int>(shown.rect.getX() - contentX, shown.rect.getY() - top);
            }
    }
    return layout;
}

} // namespace synth::ui
