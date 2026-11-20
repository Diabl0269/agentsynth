// ModuleDesignLanguageGuardTests.cpp
//
// Guard for the module design language (docs/layout/module-card-layout.md#lane-presentation): every built-in default
// card layout is a lane grid with the zones in one order, the roles in one order, the Out lane last with Mix, Pan and
// Output, nothing near a lane's hairline, nothing touching, no wide spare block in a lane, every control on the card,
// and Tab going column first. A type that has not moved to lanes yet is in kNotYetMigrated with its reason; that list
// only shrinks (a listed type that already has lanes fails, so the entry is removed the day its family moves).
#include "../CardBodyTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "UI/Graph/CardBody/CardBodyLanes.h"
#include "UI/Graph/CardBody/DefaultCardLayouts.h"
#include "UI/Layout/LayoutUtil.h"
#include <algorithm>

using namespace cardbody_test;
using synth::CardPresentation;
using synth::CardSection;

namespace {

// Types whose default layout is not a lane grid yet, and why. Never add a type here.
const std::map<juce::String, const char*>& notYetMigrated() {
    static const std::map<juce::String, const char*> kNotYetMigrated = [] {
        std::map<juce::String, const char*> list;
        for (const char* type :
             {"Oscillator",    "Filter",      "VCA",      "ADSR",      "Amp Env", "Filter Env", "LFO",
              "Compressor",    "Gate",        "Limiter",  "Wavetable", "Sampler", "Sequencer",  "Poly Sequencer",
              "Macros",        "Noise",       "Drum Kit", "Pluck",     "Mallet",  "Poly MIDI",  "Envelope Follower",
              "Sample & Hold", "Voice Mixer", "Math"})
            list[type] = "own family, later";
        return list;
    }();
    return kNotYetMigrated;
}

constexpr int kKnobCell = 60; // the widest spare block a lane may leave beside its content

// The lane roles in their order down the card's lanes; a lane's id names its roles joined by "-".
int roleRank(const juce::String& role) {
    static const juce::StringArray order = {"hero", "pitch", "shape", "time", "motion", "voice"};
    return order.indexOf(role);
}

std::vector<int> roleRanks(const juce::String& laneId) {
    std::vector<int> ranks;
    juce::StringArray roles;
    roles.addTokens(laneId, "-", "");
    for (const auto& role : roles)
        ranks.push_back(roleRank(role));
    return ranks;
}

bool hasLanes(const synth::CardLayout& layout) {
    for (const auto& section : layout.sections)
        if (section.presentation == CardPresentation::Lane)
            return true;
    return false;
}

// A control's box on the card: its widget joined with its caption.
juce::Rectangle<int> boxOf(const synth::CardBodyItem& item) {
    auto box = item.widget != nullptr ? item.widget->getBounds() : juce::Rectangle<int>();
    if (item.label != nullptr && item.label->isVisible())
        box = box.getUnion(item.label->getBounds());
    return box;
}

std::vector<const synth::CardBodyItem*> shownLaneItems(const synth::CardBodyPlan& plan) {
    std::vector<const synth::CardBodyItem*> items;
    for (const auto& item : plan.items)
        if (item.param != nullptr && item.shown && item.widget != nullptr && item.widget->isVisible() &&
            item.laneSpot.lane >= 0)
            items.push_back(&item);
    return items;
}

} // namespace

TEST(ModuleDesignLanguage, EveryDefaultLayoutIsALaneGridInTheOneOrder) {
    const auto& defaults = synth::DefaultCardLayouts::builtIn();
    for (const auto& [type, reason] : notYetMigrated())
        EXPECT_NE(defaults.find(type), nullptr)
            << type << " is in kNotYetMigrated but has no default layout: remove it";
    int checked = 0;
    for (const auto& type : libraryTypes()) {
        const auto* entry = defaults.find(type);
        if (entry == nullptr)
            continue;
        SCOPED_TRACE(type.toStdString());
        const auto& layout = entry->layout;
        if (notYetMigrated().count(type) > 0) {
            EXPECT_FALSE(hasLanes(layout)) << "has lanes now: remove it from kNotYetMigrated";
            continue;
        }
        ++checked;
        if (!hasLanes(layout)) {
            ADD_FAILURE() << type << ": a default layout that is not a lane grid belongs in kNotYetMigrated";
            continue;
        }

        // Zones: an optional mode row, then the lanes, the Out lane last, no footer.
        size_t at = 0;
        const auto& sections = layout.sections;
        if (sections[at].id == CardSection::kModeRowId)
            ++at;
        std::vector<const CardSection*> lanes;
        for (; at < sections.size(); ++at) {
            EXPECT_EQ(sections[at].presentation, CardPresentation::Lane) << "section " << sections[at].id;
            EXPECT_NE(sections[at].id, CardSection::kFooterId) << "no footer";
            lanes.push_back(&sections[at]);
        }
        ASSERT_GE(lanes.size(), 2u);
        EXPECT_EQ(lanes.back()->id, CardSection::kOutLaneId) << "the Out lane is the last lane";
        for (size_t i = 0; i + 1 < lanes.size(); ++i)
            EXPECT_NE(lanes[i]->id, CardSection::kOutLaneId) << "the Out lane is only ever last";

        // Lane roles run hero, pitch, shape, time, motion, voice.
        int previous = -1;
        for (size_t i = 0; i + 1 < lanes.size(); ++i)
            for (int rank : roleRanks(lanes[i]->id)) {
                EXPECT_GE(rank, 0) << "unknown lane role in " << lanes[i]->id;
                EXPECT_GE(rank, previous) << "lane roles out of order at " << lanes[i]->id;
                previous = rank;
            }

        // The Out lane: Mix, Pan, Output in that order, and those three nowhere else.
        std::vector<juce::String> out;
        for (const auto& item : lanes.back()->items)
            if (const auto* param = std::get_if<synth::CardParamItem>(&item))
                out.push_back(param->paramId);
        EXPECT_EQ(out, (std::vector<juce::String>{"mix", "pan", "outputLevel"}));
        for (size_t i = 0; i < sections.size(); ++i)
            if (sections[i].id != CardSection::kOutLaneId)
                for (const auto& item : sections[i].items)
                    if (const auto* param = std::get_if<synth::CardParamItem>(&item))
                        EXPECT_TRUE(param->paramId != "mix" && param->paramId != "pan" &&
                                    param->paramId != "outputLevel")
                            << param->paramId << " belongs in the Out lane";
        EXPECT_TRUE(std::none_of(layout.hidden.begin(), layout.hidden.end(), [](const juce::String& id) {
            return id == "mix" || id == "pan" || id == "outputLevel";
        })) << "no Out control in More";
    }
    EXPECT_GE(checked, 9) << "the effect cards are checked";
}

TEST(ModuleDesignLanguage, EveryLaneCardKeepsItsControlsClearOfTheRulesAndEachOtherAndInsideTheCard) {
    const auto& defaults = synth::DefaultCardLayouts::builtIn();
    for (const auto& type : libraryTypes()) {
        const auto* entry = defaults.find(type);
        if (entry == nullptr || notYetMigrated().count(type) > 0 || !hasLanes(entry->layout))
            continue;
        SCOPED_TRACE(type.toStdString());
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
        canvas.editor.updateComponents();
        auto* card = canvas.card(id);
        ASSERT_NE(card, nullptr);
        const auto& plan = card->getCardBody()->getPlan();
        const auto items = shownLaneItems(plan);
        ASSERT_FALSE(items.empty());

        for (const auto* item : items) {
            SCOPED_TRACE(item->param->paramID.toStdString());
            EXPECT_TRUE(card->getLocalBounds().contains(boxOf(*item))) << "inside the card";
        }

        // The rules between lanes: each lane section but the first owns one, on its left.
        std::vector<juce::Rectangle<int>> rules;
        for (const auto& section : plan.sections)
            if (section.lane && section.divider != nullptr && !section.divider->getBounds().isEmpty())
                rules.push_back(section.divider->getBounds());
        EXPECT_GE(rules.size(), 1u);
        for (const auto& rule : rules)
            for (const auto* item : items) {
                const auto box = boxOf(*item);
                if (box.getBottom() <= rule.getY() || box.getY() >= rule.getBottom())
                    continue;
                const int gap =
                    box.getRight() <= rule.getX() ? rule.getX() - box.getRight() : box.getX() - rule.getRight();
                EXPECT_GE(gap, synth::cardbody::kLaneClear)
                    << item->param->paramID << " is within " << synth::cardbody::kLaneClear << " px of a rule";
            }

        // Nothing touches: two controls in different cells are at least a pixel apart.
        for (size_t a = 0; a < items.size(); ++a)
            for (size_t b = a + 1; b < items.size(); ++b) {
                const auto& sa = items[a]->laneSpot;
                const auto& sb = items[b]->laneSpot;
                if (sa.lane == sb.lane && sa.column == sb.column && sa.row == sb.row)
                    continue; // swap members share a cell
                EXPECT_FALSE(boxOf(*items[a]).expanded(1).intersects(boxOf(*items[b])))
                    << items[a]->param->paramID << " touches " << items[b]->param->paramID;
            }

        // No spare block wider than one knob cell beside a lane's content.
        std::map<int, juce::Range<int>> extent;
        for (const auto* item : items) {
            const auto box = boxOf(*item);
            auto& range = extent.try_emplace(item->laneSpot.lane, box.getX(), box.getRight()).first->second;
            range = range.getUnionWith({box.getX(), box.getRight()});
        }
        std::vector<int> edges = {synth::cardbody::kContentMargin};
        for (const auto& rule : rules)
            edges.push_back(rule.getX());
        edges.push_back(card->getWidth() - synth::cardbody::kContentMargin);
        std::sort(edges.begin(), edges.end());
        ASSERT_EQ(edges.size(), extent.size() + 1) << "one slot per lane";
        size_t slot = 0;
        for (const auto& [lane, range] : extent) {
            const int left = range.getStart() - edges[slot] - (slot > 0 ? synth::cardbody::kLaneClear : 0);
            const int right =
                edges[slot + 1] - range.getEnd() - (slot + 1 < extent.size() ? synth::cardbody::kLaneClear : 0);
            EXPECT_LE(left, kKnobCell) << "lane " << lane << " leaves a spare block on its left";
            EXPECT_LE(right, kKnobCell) << "lane " << lane << " leaves a spare block on its right";
            ++slot;
        }
    }
}

TEST(ModuleDesignLanguage, TabGoesDownAColumnThenAcrossTheLanesAndTheModeRowComesFirst) {
    const auto& defaults = synth::DefaultCardLayouts::builtIn();
    for (const auto& type : libraryTypes()) {
        const auto* entry = defaults.find(type);
        if (entry == nullptr || notYetMigrated().count(type) > 0 || !hasLanes(entry->layout))
            continue;
        SCOPED_TRACE(type.toStdString());
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
        canvas.editor.updateComponents();
        auto* card = canvas.card(id);
        ASSERT_NE(card, nullptr);
        const auto& plan = card->getCardBody()->getPlan();
        const auto stops = card->getKeyboardControls(); // the order the card's own Tab walks

        // The body's stops in Tab order, as (lane, column, row); the mode row's controls carry no lane.
        std::vector<std::array<int, 3>> order;
        bool sawLane = false;
        for (auto* stop : stops)
            for (const auto& item : plan.items)
                if (item.param != nullptr && item.widget == stop) {
                    const auto& spot = item.laneSpot;
                    if (spot.lane < 0)
                        EXPECT_FALSE(sawLane) << item.param->paramID << ": the mode row comes before the lanes";
                    else
                        sawLane = true;
                    order.push_back({spot.lane, spot.column, spot.row});
                }
        ASSERT_FALSE(order.empty());
        for (size_t i = 0; i + 1 < order.size(); ++i)
            EXPECT_LE(order[i], order[i + 1]) << "stop " << i << " breaks column-first order";
    }
}

// The lane grid's pure steps (CardBodyLanes.h): packing columns, sharing spare width, the card width.
using Columns = std::vector<std::vector<int>>;

TEST(LanePacker, ColumnsFillTopToBottomBeforeTheNextColumnStarts) {
    // Knob cells are 76 high in a 152 band: two fit a column, the third starts the next.
    EXPECT_EQ(synth::cardbody::packLaneColumns({76, 76, 76}, synth::cardbody::kLaneBandHeight), (Columns{{0, 1}, {2}}));
    EXPECT_EQ(synth::cardbody::packLaneColumns({76, 76, 76, 76, 76}, synth::cardbody::kLaneBandHeight),
              (Columns{{0, 1}, {2, 3}, {4}}));
    // A cell that does not fit what is left of the column goes to the next one; a tall cell takes its own column.
    EXPECT_EQ(synth::cardbody::packLaneColumns({100, 76}, synth::cardbody::kLaneBandHeight), (Columns{{0}, {1}}));
    EXPECT_EQ(synth::cardbody::packLaneColumns({152, 76}, synth::cardbody::kLaneBandHeight), (Columns{{0}, {1}}));
    EXPECT_TRUE(synth::cardbody::packLaneColumns({}, synth::cardbody::kLaneBandHeight).empty());
}

TEST(LanePacker, ALoneCellIsItsOwnColumnSoTheLaneCentresIt) {
    // One cell is one column holding one cell; the lane layout centres a column narrower than its slot.
    const auto columns = synth::cardbody::packLaneColumns({76}, synth::cardbody::kLaneBandHeight);
    ASSERT_EQ(columns.size(), 1u);
    EXPECT_EQ(columns[0], std::vector<int>({0}));
}

TEST(LanePacker, SpareWidthIsSharedEvenlyAndARemainderGoesToTheFirstLanes) {
    // Needs 100 + 80, one 1 px rule, 200 available: 19 spare, 9 each and the extra pixel to the first.
    EXPECT_EQ(synth::cardbody::shareLaneWidths({100, 80}, 200), (std::vector<int>{110, 89}));
    const auto even = synth::cardbody::shareLaneWidths({100, 80, 60}, 300);
    EXPECT_EQ(even[0] + even[1] + even[2] + 2 * synth::cardbody::kLaneRuleWidth, 300)
        << "the lanes and rules fill the width";
    EXPECT_LE(*std::max_element(even.begin(), even.end()) - 100, 21);
}

TEST(LanePacker, LanesGetOnlyTheirNeedsWhenTheCardIsTooNarrow) {
    EXPECT_EQ(synth::cardbody::shareLaneWidths({100, 80}, 150), (std::vector<int>{100, 80}));
    EXPECT_TRUE(synth::cardbody::shareLaneWidths({}, 150).empty());
}

TEST(LanePacker, TheCardWidensToFitTheLanesInMultiplesOfFourNeverUnderTheSingleWidth) {
    EXPECT_EQ(synth::cardbody::laneCardWidthFor(100, 0), synth::LayoutUtil::kSingleWidth);
    const int wide = synth::cardbody::laneCardWidthFor(301, 0);
    EXPECT_EQ(wide, (301 + 2 * synth::cardbody::kContentMargin + 3) / 4 * 4);
    EXPECT_EQ(wide % 4, 0);
    EXPECT_GE(wide, 301 + 2 * synth::cardbody::kContentMargin);
    EXPECT_EQ(synth::cardbody::laneCardWidthFor(100, 400), synth::cardbody::laneCardWidthFor(400, 100))
        << "the wider of lanes and mode row decides";
}
