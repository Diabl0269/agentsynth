// CardBodyLayoutTests.cpp
//
// Source/UI/Graph/CardBody/: the automatic layout reproduces the generic card (order, heights, the size
// estimate), one function measures and places, the More row appears only when a layout hides something
// and folds/unfolds the card's height, and no value drag ever changes a card's height. Pixel parity of
// every built card with the pre-CardBody card is CardBodyGoldenTests.cpp.

#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "CardBodyTestHelpers.h"
#include "Modules/ADSRModule.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/CardBody/CardBodyMeasure.h"
#include "UI/Graph/CardBody/DefaultCardLayouts.h"
#include "UI/Graph/CardBody/DefaultLayouts/AdsrTimeTempo.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include <cmath>
#include <set>

using namespace cardbody_test;
using synth::CardBodyItem;
using synth::CardBodyPlan;
using synth::CardWidget;
namespace cb = synth::cardbody;

namespace {

int kindRank(CardBodyItem::Kind kind) {
    switch (kind) {
    case CardBodyItem::Kind::Choice:
        return 0;
    case CardBodyItem::Kind::Toggle:
        return 1;
    case CardBodyItem::Kind::View:
        return 2;
    case CardBodyItem::Kind::Knob:
        return 3;
    default:
        return -1; // the automatic layout never draws the other kinds
    }
}

int declarationIndex(juce::AudioProcessor& module, const juce::RangedAudioParameter* param) {
    return module.getParameters().indexOf(const_cast<juce::RangedAudioParameter*>(param));
}

juce::Component* childWithId(juce::Component& parent, const juce::String& id) {
    for (auto* child : parent.getChildren())
        if (child->getComponentID() == id)
            return child;
    return nullptr;
}

juce::Slider* knobNamed(ModuleComponent& card, const juce::String& name) {
    return dynamic_cast<juce::Slider*>(childWithId(card, name));
}

} // namespace

TEST(CardBodyLayout, AutomaticLayoutStacksCombosTogglesThresholdThenKnobsInDeclarationOrder) {
    for (const auto& type : synth::AIStateMapper::moduleFactoryTypeNames()) {
        SCOPED_TRACE(type.toStdString());
        auto module = synth::AIStateMapper::createModule(type);
        if (module == nullptr)
            continue;
        const auto plan = CardBodyPlan::forModule(*module, std::nullopt);
        ASSERT_EQ(plan.sections.size(), 1u);
        EXPECT_TRUE(plan.more.empty()) << "the automatic layout never hides anything";
        int lastRank = -1, lastIndex = -1;
        for (int index : plan.sections.front().items) {
            const auto& item = plan.items[(size_t)index];
            const int rank = kindRank(item.kind);
            EXPECT_GE(rank, lastRank) << "combos, then toggles, then the Threshold view, then knobs";
            if (rank != lastRank)
                lastIndex = -1;
            if (item.param != nullptr) {
                const int declared = declarationIndex(*module, item.param);
                EXPECT_GT(declared, lastIndex) << "each group in declaration order";
                lastIndex = declared;
            }
            lastRank = rank;
        }
    }
}

// The library drag ghost and drop placement read estimateModuleSize; every card drawn from layout data
// is now measured from its card-body plan rather than a hand-kept table, and must equal the real card.
TEST(CardBodyLayout, EveryDataDrivenLibraryCardIsMeasuredFromItsPlan) {
    AudioEngine engine;
    GraphEditor editor(engine);
    int measured = 0;
    for (const auto& type : libraryTypes()) {
        SCOPED_TRACE(type.toStdString());
        auto module = synth::AIStateMapper::createModule(type);
        ASSERT_NE(module, nullptr);
        auto* mb = dynamic_cast<ModuleBase*>(module.get());
        const bool dataDriven = mb != nullptr && synth::cardBodyLayoutIsDataDriven(*module) &&
                                mb->getModuleType() != ModuleType::AudioInput;
        const auto size = synth::measureDataDrivenCardSize(type);
        EXPECT_EQ(size.has_value(), dataDriven);
        if (!size.has_value())
            continue;
        ++measured;
        ModuleComponent card(module.get(), NodeID(1), editor);
        EXPECT_EQ(*size, juce::Point<int>(card.getWidth(), card.getHeight()));
    }
    EXPECT_GT(measured, 20) << "most library cards are drawn from layout data";
}

TEST(CardBodyLayout, MeasureAndApplyReturnTheSameHeight) {
    CardCanvas canvas;
    std::vector<NodeID> ids;
    int x = 0;
    for (const auto& type : libraryTypes()) {
        auto module = synth::AIStateMapper::createModule(type);
        if (module == nullptr || !synth::cardBodyLayoutIsDataDriven(*module))
            continue;
        // A second copy of each card hides its first parameter, so the More row is measured too.
        auto copy = synth::AIStateMapper::createModule(type);
        const auto plan = CardBodyPlan::forModule(*copy, std::nullopt);
        std::optional<synth::CardLayout> hiding;
        if (!plan.items.empty() && plan.items.front().param != nullptr)
            hiding = automaticLayoutHiding(*copy, {plan.items.front().param->paramID});
        ids.push_back(canvas.add(std::move(module), x, 0));
        ids.push_back(canvas.add(std::move(copy), x, 1000, hiding));
        x += 300;
    }
    canvas.editor.updateComponents();

    for (auto id : ids) {
        auto* card = canvas.card(id);
        ASSERT_NE(card, nullptr);
        auto* body = card->getCardBody();
        ASSERT_NE(body, nullptr);
        SCOPED_TRACE(card->getModule()->getName().toStdString());
        const auto g = synth::cardbody::BodyGeometry::forCardWidth(card->getWidth());
        for (bool unfolded : {false, true}) {
            if (body->hasMoreRow())
                body->setMoreUnfolded(unfolded);
            const int measuredBody = body->layout(100, g, /*apply*/ false);
            EXPECT_EQ(measuredBody, body->layout(100, g, /*apply*/ true));
            EXPECT_EQ(body->layoutMoreRow(500, g, false), body->layoutMoreRow(500, g, true));
        }
    }
}

TEST(CardBodyLayout, NoLibraryCardShowsAMoreRowWithoutAHiddenParameter) {
    // Types whose card deliberately leaves rarely-touched parameters unplaced, so they sit in the More row: the
    // shared output stage's Mix and/or Pan on the FX (docs/modules/fx-modules.md#mix-and-pan-shared-stage), and
    // the voice-feel options (Oscillator: Phase, Rand, Glide Mode/Curve, Legato; ADSR: Legato).
    static const std::set<juce::String> kMoreRowByDesign = {
        "Compressor", "Gate",       "Parametric EQ", "Bitcrusher",    "Chorus",    "Delay",
        "Distortion", "Flanger",    "Phaser",        "Pitch Shifter", "Reverb",    "Ring Modulator",
        "Limiter",    "Oscillator", "ADSR",          "Amp Env",       "Filter Env"};
    CardCanvas canvas;
    std::vector<NodeID> ids;
    int x = 0;
    for (const auto& type : libraryTypes())
        if (auto module = synth::AIStateMapper::createModule(type)) {
            ids.push_back(canvas.add(std::move(module), x, 0));
            x += 600;
        }
    canvas.editor.updateComponents();
    for (auto id : ids) {
        auto* card = canvas.card(id);
        ASSERT_NE(card, nullptr);
        if (kMoreRowByDesign.count(card->getModule()->getName()) > 0)
            continue;
        EXPECT_EQ(childWithId(*card, "cardMoreRow"), nullptr) << card->getModule()->getName();
        if (auto* body = card->getCardBody())
            EXPECT_FALSE(body->hasMoreRow());
    }
}

// The More row: a hidden parameter's widget is built and bound but folded away under a titled,
// focusable button at the bottom of the card; unfolding grows the card, folding gives the height back.
TEST(CardBodyLayout, HiddenParameterFoldsIntoTheMoreRowAndUnfoldingGrowsTheCard) {
    CardCanvas canvas;
    auto filter = std::make_unique<FilterModule>();
    const auto layout = automaticLayoutHiding(*filter, {"drive"});
    const auto plain = canvas.add(std::make_unique<FilterModule>(), 0, 0);
    const auto hidden = canvas.add(std::move(filter), 400, 0, layout);
    canvas.editor.updateComponents();
    auto* plainCard = canvas.card(plain);
    auto* card = canvas.card(hidden);
    ASSERT_NE(card, nullptr);
    ASSERT_NE(plainCard, nullptr);

    auto* more = dynamic_cast<juce::Button*>(childWithId(*card, "cardMoreRow"));
    ASSERT_NE(more, nullptr);
    EXPECT_EQ(more->getTitle(), "More controls (1)");
    EXPECT_TRUE(more->getTooltip().isNotEmpty());
    EXPECT_TRUE(more->getWantsKeyboardFocus());
    EXPECT_TRUE(more->isVisible());
    const int moreIndex = card->getIndexOfChildComponent(more);
    for (auto* child : card->getChildren())
        if (dynamic_cast<juce::Slider*>(child) != nullptr || dynamic_cast<juce::ComboBox*>(child) != nullptr)
            EXPECT_LT(card->getIndexOfChildComponent(child), moreIndex) << "built after every body control";

    auto* drive = knobNamed(*card, "Drive");
    ASSERT_NE(drive, nullptr);
    EXPECT_FALSE(drive->isVisible()) << "folded";
    EXPECT_GE(more->getY(), knobNamed(*card, "Level")->getBottom()) << "the More row sits under the body";
    EXPECT_LE(more->getBottom(), card->getHeight());

    const int folded = card->getHeight();
    ASSERT_TRUE(static_cast<juce::Component*>(more)->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_TRUE(card->getCardBody()->isMoreUnfolded());
    EXPECT_TRUE(drive->isVisible());
    EXPECT_GT(card->getHeight(), folded);
    EXPECT_GT(drive->getY(), more->getY()) << "unfolded controls sit under the More button";
    EXPECT_TRUE(card->getLocalBounds().contains(drive->getBounds()));

    ASSERT_TRUE(static_cast<juce::Component*>(more)->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    EXPECT_FALSE(card->getCardBody()->isMoreUnfolded());
    EXPECT_FALSE(drive->isVisible());
    EXPECT_EQ(card->getHeight(), folded);
}

TEST(CardBodyLayout, MoreButtonIsATabStopAfterTheBodyControls) {
    CardCanvas canvas;
    auto filter = std::make_unique<FilterModule>();
    const auto layout = automaticLayoutHiding(*filter, {"resonance"});
    const auto id = canvas.add(std::move(filter), 0, 0, layout);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    ASSERT_NE(card, nullptr);
    auto* more = childWithId(*card, "cardMoreRow");
    ASSERT_NE(more, nullptr);

    juce::KeyboardFocusTraverser traverser;
    const auto stops = traverser.getAllComponents(card);
    const auto moreAt = std::find(stops.begin(), stops.end(), more);
    ASSERT_NE(moreAt, stops.end()) << "the More button is keyboard-reachable";
    for (auto it = stops.begin(); it != moreAt; ++it)
        EXPECT_NE(*it, knobNamed(*card, "Resonance")) << "a folded control is not a Tab stop";
    const auto cutoffAt = std::find(stops.begin(), stops.end(), knobNamed(*card, "Cutoff"));
    EXPECT_LT(cutoffAt, moreAt);
}

// Card height drives canvas make-room, so a card that grew while a knob turned would shove its
// neighbours around under the cursor. Every knob on every library card (and a card with an unfolded
// More row) is dragged end to end with a real synthesized mouse drag.
TEST(CardBodyLayout, CardHeightNeverChangesWhileAKnobIsDragged) {
    CardCanvas canvas;
    std::vector<NodeID> ids;
    int x = 0;
    for (const auto& type : libraryTypes())
        if (auto module = synth::AIStateMapper::createModule(type);
            module != nullptr && synth::cardBodyLayoutIsDataDriven(*module)) {
            ids.push_back(canvas.add(std::move(module), x, 0));
            x += 300;
        }
    auto filter = std::make_unique<FilterModule>();
    const auto layout = automaticLayoutHiding(*filter, {"drive"});
    const auto withMore = canvas.add(std::move(filter), x, 0, layout);
    ids.push_back(withMore);
    canvas.editor.updateComponents();
    canvas.card(withMore)->getCardBody()->setMoreUnfolded(true);

    for (auto id : ids) {
        auto* card = canvas.card(id);
        ASSERT_NE(card, nullptr);
        for (auto* child : card->getChildren()) {
            auto* knob = dynamic_cast<juce::Slider*>(child);
            if (knob == nullptr || !knob->isVisible())
                continue;
            SCOPED_TRACE((card->getModule()->getName() + " / " + knob->getComponentID()).toStdString());
            const int height = card->getHeight();
            const auto centre = knob->getLocalBounds().getCentre();
            const auto mods = plainLeftClick();
            knob->mouseDown(makeModuleClickWithMods(*knob, centre, mods));
            for (int dy : {-200, 200, -40})
                knob->mouseDrag(makeModuleClickWithMods(*knob, centre + juce::Point<int>(0, dy), mods));
            EXPECT_EQ(card->getHeight(), height) << "mid-drag";
            knob->mouseUp(makeModuleClickWithMods(*knob, centre + juce::Point<int>(0, -40), mods));
            EXPECT_EQ(card->getHeight(), height);
        }
    }
}

namespace {

struct Cell {
    juce::Component widget;
    juce::Component label;
};

std::vector<cb::GridCell> gridOf(std::vector<Cell>& cells, const std::vector<int>& heights,
                                 const std::vector<int>& widths) {
    std::vector<cb::GridCell> out;
    for (size_t i = 0; i < cells.size(); ++i)
        out.push_back({{&cells[i].widget, &cells[i].label}, heights[i], widths[i]});
    return out;
}

} // namespace

TEST(MixedGridRun, ConsecutiveCellsOfDifferentHeightsShareOneGridAndARowIsAsTallAsItsTallestCell) {
    const auto g = cb::BodyGeometry::forCardWidth(280);
    // Five columns: fader, fader, fader, knob, fader: one row, as tall as a fader cell.
    std::vector<Cell> cells(5);
    const auto grid =
        gridOf(cells, {cb::kFaderVHeight, cb::kFaderVHeight, cb::kFaderVHeight, cb::kKnobHeight, cb::kFaderVHeight},
               {40, 40, 40, 0, 40});
    const int end = cb::layoutMixedGridRun(grid, 5, 100, g, true);
    EXPECT_EQ(end, 100 + cb::kLabelHeight + cb::kFaderVHeight);
    const int cellW = cb::gridCellWidth(5, g);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(cells[(size_t)i].label.getY(), 100) << i << ": one row";
        EXPECT_EQ(cells[(size_t)i].label.getX(), g.contentX + i * cellW);
        EXPECT_EQ(cells[(size_t)i].widget.getY(), 100 + cb::kLabelHeight);
    }
    EXPECT_EQ(cells[3].widget.getWidth(), cellW) << "a knob fills its cell";
    EXPECT_EQ(cells[0].widget.getWidth(), 40);
    EXPECT_EQ(cells[0].widget.getX(), g.contentX + (cellW - 40) / 2) << "a narrower widget is centred in its cell";
    // Measuring places nothing and returns the same height.
    EXPECT_EQ(cb::layoutMixedGridRun(grid, 5, 100, g, false), end);
}

TEST(MixedGridRun, ARunOfOneKindLaysOutExactlyAsTheSingleKindRunDid) {
    const auto g = cb::BodyGeometry::forCardWidth(280);
    for (const int columns : {2, 3, 5}) {
        std::vector<Cell> mixed(7), single(7);
        std::vector<cb::CaptionedWidget> captioned;
        for (auto& cell : single)
            captioned.emplace_back(&cell.widget, &cell.label);
        const auto grid = gridOf(mixed, std::vector<int>(7, cb::kFaderVHeight), std::vector<int>(7, 40));
        const int mixedEnd = cb::layoutMixedGridRun(grid, columns, 50, g, true);
        const int singleEnd = cb::layoutGridRun(captioned, columns, cb::kFaderVHeight, 40, 50, g, true);
        EXPECT_EQ(mixedEnd, singleEnd) << columns;
        for (size_t i = 0; i < 7; ++i) {
            EXPECT_EQ(mixed[i].widget.getBounds(), single[i].widget.getBounds()) << columns << " " << i;
            EXPECT_EQ(mixed[i].label.getBounds(), single[i].label.getBounds()) << columns << " " << i;
        }
    }
}

namespace {

constexpr CardWidget kGridWidgets[] = {CardWidget::Knob, CardWidget::KnobLarge, CardWidget::FaderV,
                                       CardWidget::FaderVLong};

bool isGridKind(CardBodyItem::Kind kind) {
    return kind == CardBodyItem::Kind::Knob || kind == CardBodyItem::Kind::KnobLarge ||
           CardBodyItem::isVerticalFader(kind);
}

// The kind of the cell an item stands in (a swap group's cell takes its tallest member's kind).
CardBodyItem::Kind cellKindOf(const synth::CardBodyPlan& plan, const CardBodyItem& item) {
    return item.swapGroup >= 0 ? plan.swapGroups[(size_t)item.swapGroup].cellKind : item.kind;
}

// What the section draws now: its cells in order (a swap group once, as its shown member), each with its kind.
struct DrawnCell {
    const CardBodyItem* item;
    CardBodyItem::Kind kind;
};

std::vector<DrawnCell> cellsOfSection(const synth::CardBodyPlan& plan, const synth::CardBodyPlan::Section& section) {
    std::vector<DrawnCell> cells;
    std::set<int> groups;
    for (int index : section.items) {
        const auto& item = plan.items[(size_t)index];
        if (item.widget == nullptr || item.param == nullptr)
            continue;
        if (item.swapGroup < 0 && !item.shown)
            continue; // a control the card hides (a Macros knob past the count) takes no cell
        if (item.swapGroup >= 0 && !groups.insert(item.swapGroup).second)
            continue;
        const CardBodyItem* shown = &item;
        if (item.swapGroup >= 0)
            for (int member : plan.swapGroups[(size_t)item.swapGroup].members)
                if (plan.items[(size_t)member].shown && plan.items[(size_t)member].widget != nullptr) {
                    shown = &plan.items[(size_t)member];
                    break;
                }
        cells.push_back({shown, cellKindOf(plan, item)});
    }
    return cells;
}

// The rows the grid cells of every maximal run in `section` use against the rows a grid of that many cells needs.
// Empty when they agree.
juce::String rowBreaks(ModuleComponent& card, const synth::CardBodyPlan::Section& section,
                       const synth::CardBodyPlan& plan) {
    const auto g = card.bodyGeometry();
    const int columns = g.isDoubleWidth() ? section.columns * 2 : section.columns;
    const auto cells = cellsOfSection(plan, section);
    for (size_t i = 0; i < cells.size();) {
        if (!isGridKind(cells[i].kind)) {
            ++i;
            continue;
        }
        size_t end = i;
        std::set<int> rows;
        while (end < cells.size() && isGridKind(cells[end].kind)) {
            const auto* item = cells[end].item;
            rows.insert(item->label != nullptr ? item->label->getY() : item->widget->getY() - cb::kLabelHeight);
            ++end;
        }
        const int needed = (int)std::ceil((double)(end - i) / columns);
        if ((int)rows.size() != needed)
            return juce::String((int)(end - i)) + " grid cells in " + juce::String(columns) + " columns use " +
                   juce::String((int)rows.size()) + " rows, not " + juce::String(needed);
        i = end;
    }
    return {};
}

// The first pair of drawn cells (caption and widget) that overlap, or empty.
juce::String firstOverlap(const synth::CardBodyPlan& plan, const synth::CardBodyPlan::Section& section) {
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> rects;
    for (const auto& cell : cellsOfSection(plan, section)) {
        auto rect = cell.item->widget->getBounds();
        if (cell.item->label != nullptr)
            rect = rect.getUnion(cell.item->label->getBounds());
        rects.emplace_back(cell.item->param->paramID, rect);
    }
    for (size_t i = 0; i < rects.size(); ++i)
        for (size_t j = i + 1; j < rects.size(); ++j)
            if (rects[i].second.reduced(1).intersects(rects[j].second.reduced(1)))
                return rects[i].first + " and " + rects[j].first;
    return {};
}

} // namespace

// The row-break guard: every default layout, every flowing grid section, every control in it that can be another
// grid kind, restyled to each other grid kind: the grid cells still use exactly the rows a grid of that many cells
// needs and nothing overlaps (the measured height equals the real card's in MeasureAndApplyReturnTheSameHeight).
TEST(MixedGridRun, EveryDefaultLayoutKeepsItsGridRowsWhenAnyControlTakesAnotherGridKind) {
    ModuleLibraryComponent library;
    int variations = 0;
    for (const auto& type : library.getDraggableModuleNames()) {
        const auto* entry = synth::DefaultCardLayouts::builtIn().find(type);
        if (entry == nullptr)
            continue;
        for (int s = 0; s < (int)entry->layout.sections.size(); ++s) {
            const auto& section = entry->layout.sections[(size_t)s];
            if (section.id == synth::CardSection::kFooterId || section.items.size() < 2)
                continue;
            for (size_t i = 0; i < section.items.size(); ++i) {
                const auto* param = std::get_if<synth::CardParamItem>(&section.items[i]);
                if (param == nullptr || param->at.has_value())
                    continue;
                for (const auto widget : kGridWidgets) {
                    if (widget == param->widget)
                        continue;
                    auto layout = entry->layout;
                    std::get<synth::CardParamItem>(layout.sections[(size_t)s].items[i]).widget = widget;
                    CardCanvas canvas;
                    const auto id = canvas.add(synth::AIStateMapper::createModule(type), 100, 100, layout);
                    canvas.editor.updateComponents();
                    auto* card = canvas.card(id);
                    ASSERT_NE(card, nullptr);
                    const auto& plan = card->getCardBody()->getPlan();
                    const int planIndex = plan.findParam(param->paramId);
                    if (planIndex < 0 || !isGridKind(plan.items[(size_t)planIndex].kind) ||
                        plan.items[(size_t)planIndex].kind == CardBodyItem::Kind::Choice)
                        continue; // the control has no such look (a switch, a choice): nothing was restyled
                    ++variations;
                    const juce::String where = type + ": " + param->paramId + " as widget " + juce::String((int)widget);
                    for (const auto& planned : plan.sections) {
                        if (planned.footer || planned.freeform || planned.row || !planned.visible)
                            continue;
                        EXPECT_EQ(rowBreaks(*card, planned, plan), "") << where << " in section " << planned.id;
                        EXPECT_EQ(firstOverlap(plan, planned), "") << where << " in section " << planned.id;
                    }
                }
            }
        }
    }
    EXPECT_GT(variations, 40) << "the enumeration found the default layouts' grid controls";
}

// Sustain as a knob in the ADSR's stage row, in both looks of the Controls modes: it stays in the row, and the card
// is no taller than with five faders (a knob is shorter than a fader).
TEST(MixedGridRun, ASustainKnobStaysInTheStageRowOfEveryAdsrLook) {
    for (const auto mode : {synth::AdsrTimeTempo::Shared, synth::AdsrTimeTempo::Separate}) {
        auto layout = synth::withAdsrTimeTempo(synth::DefaultCardLayouts::builtIn().find("ADSR")->layout, mode);
        for (auto& section : layout.sections)
            for (auto& item : section.items)
                if (auto* param = std::get_if<synth::CardParamItem>(&item);
                    param != nullptr && param->paramId == "sustain")
                    param->widget = CardWidget::Knob;
        CardCanvas faders, knob;
        const auto before =
            faders.add(std::make_unique<ADSRModule>(), 100, 100,
                       synth::withAdsrTimeTempo(synth::DefaultCardLayouts::builtIn().find("ADSR")->layout, mode));
        const auto after = knob.add(std::make_unique<ADSRModule>(), 100, 100, layout);
        faders.editor.updateComponents();
        knob.editor.updateComponents();
        auto* body = knob.card(after)->getCardBody();
        const int row = body->findWidget("attack")->getY();
        for (const char* id : {"hold", "decay", "sustain", "release"})
            EXPECT_EQ(body->findWidget(id)->getY(), row) << id << " " << (int)mode << ": same row as Atk";
        EXPECT_LE(knob.card(after)->getHeight(), faders.card(before)->getHeight()) << (int)mode;
    }
}
