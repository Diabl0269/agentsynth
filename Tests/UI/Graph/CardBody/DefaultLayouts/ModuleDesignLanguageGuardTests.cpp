// ModuleDesignLanguageGuardTests.cpp
//
// Guard for the module design language (docs/layout/module-card-layout.md#lane-presentation): every built-in default
// card layout is a lane grid with the zones in one order, the roles in one order, the Out lane last with Mix, Pan and
// Output, nothing near a lane's hairline, nothing touching, no wide spare block in a lane, every control on the card,
// and Tab going column first. A type exempt from lanes by design is in kExempt with its reason (only Macros);
// the list never grows, and a listed type that has lanes fails.
#include "../../GraphEditor/GraphEditorTestHelpers.h"
#include "../CardBodyTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "UI/Graph/CardBody/CardBodyLanes.h"
#include "UI/Graph/CardBody/CardBodyMeasure.h"
#include "UI/Graph/CardBody/DefaultCardLayouts.h"
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>

using namespace cardbody_test;
using synth::CardPresentation;
using synth::CardSection;

namespace {

// Types whose default layout is deliberately not a lane grid, and why. Never add a type here.
const std::map<juce::String, const char*>& exemptFromLanes() {
    static const std::map<juce::String, const char*> kExempt = [] {
        std::map<juce::String, const char*> list;
        list["Macros"] = "a numbered controller bank: column-first lanes would put Macro 2 under Macro 1; its own "
                         "Row/Grid/Faders/XY presets";
        return list;
    }();
    return kExempt;
}

// The module's output controls, by parameter id: the Out lane holds those the module has (a VCA's Level is "gain").
const std::vector<juce::String> kOutControlIds = {"mix",        "pan",     "outputLevel", "level",
                                                  "makeupGain", "ceiling", "gain"};

bool isOutControlId(const juce::String& id) {
    return std::find(kOutControlIds.begin(), kOutControlIds.end(), id) != kOutControlIds.end();
}

// True when the module has any output control: a card that has none (an envelope, a Math, an Envelope Follower) has no
// Out lane, and its last lane is a role lane.
bool hasOutControl(const juce::String& type) {
    auto module = synth::AIStateMapper::createModule(type);
    return module != nullptr && std::any_of(kOutControlIds.begin(), kOutControlIds.end(), [&](const juce::String& id) {
               return findParameterByID(module.get(), id) != nullptr;
           });
}

// A card whose one control group is a single lane (a Drum Kit's pads, a VCA's or Voice Mixer's Level, a Math's Clip, a
// Poly MIDI's voice rules) has one lane, not two.
bool isSingleLane(const juce::String& type) {
    return type == "Drum Kit" || type == "VCA" || type == "Voice Mixer" || type == "Math" || type == "Poly MIDI";
}

// A mode row wider than the lanes under it spreads them by at most this much more.
constexpr int kLaneSlack = 8;
constexpr int kKnobCell = 60; // the widest spare block a lane may leave beside its content

// The lane roles in their order down the card's lanes; a lane's id names its roles joined by "-".
int roleRank(const juce::String& role) {
    // "custom" is a lane a type shows only in its Custom mode (the Filter's Morph X / Y).
    static const juce::StringArray order = {"hero", "pitch", "shape", "time", "motion", "custom", "voice"};
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

// A section of nothing but views: the picture on top of a lane grid.
bool isPicture(const synth::CardSection& section) {
    return section.presentation != CardPresentation::Lane && section.id != CardSection::kModeRowId &&
           !section.items.empty() &&
           std::all_of(section.items.begin(), section.items.end(),
                       [](const synth::CardItem& item) { return std::holds_alternative<synth::CardViewItem>(item); });
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
    for (const auto& [type, reason] : exemptFromLanes())
        EXPECT_NE(defaults.find(type), nullptr) << type << " is exempt but has no default layout: remove it";
    int checked = 0;
    for (const auto& type : libraryTypes()) {
        const auto* entry = defaults.find(type);
        if (entry == nullptr)
            continue;
        SCOPED_TRACE(type.toStdString());
        const auto& layout = entry->layout;
        if (exemptFromLanes().count(type) > 0) {
            EXPECT_FALSE(hasLanes(layout)) << "has lanes now: it is exempt and must not";
            continue;
        }
        ++checked;
        if (!hasLanes(layout)) {
            ADD_FAILURE() << type << ": a default layout that is not a lane grid is not exempt: it must be a lane grid";
            continue;
        }

        // Zones: an optional picture (a view-only section: the dynamics cards' gain-reduction meter), an optional mode
        // row, then the lanes, the Out lane last, no footer.
        size_t at = 0;
        const auto& sections = layout.sections;
        if (isPicture(sections[at]))
            ++at;
        if (sections[at].id == CardSection::kModeRowId)
            ++at;
        std::vector<const CardSection*> lanes;
        for (; at < sections.size(); ++at) {
            EXPECT_EQ(sections[at].presentation, CardPresentation::Lane) << "section " << sections[at].id;
            EXPECT_NE(sections[at].id, CardSection::kFooterId) << "no footer";
            lanes.push_back(&sections[at]);
        }
        const bool hasOut = hasOutControl(type);
        ASSERT_GE(lanes.size(), isSingleLane(type) ? 1u : 2u) << "a card with one control group has one lane";
        if (hasOut)
            EXPECT_EQ(lanes.back()->id, CardSection::kOutLaneId) << "the Out lane is the last lane";
        for (size_t i = 0; i + 1 < lanes.size(); ++i)
            EXPECT_NE(lanes[i]->id, CardSection::kOutLaneId) << "the Out lane is only ever last";
        if (!hasOut)
            EXPECT_NE(lanes.back()->id, CardSection::kOutLaneId) << type << " has no output control: no Out lane";

        // Lane roles run hero, pitch, shape, time, motion, voice.
        int previous = -1;
        for (size_t i = 0; i + (hasOut ? 1 : 0) < lanes.size(); ++i)
            for (int rank : roleRanks(lanes[i]->id)) {
                EXPECT_GE(rank, 0) << "unknown lane role in " << lanes[i]->id;
                EXPECT_GE(rank, previous) << "lane roles out of order at " << lanes[i]->id;
                previous = rank;
            }

        // The Out lane: the in-order subset of Mix, Pan and the output control (Output on an effect, Level on a source,
        // Makeup on a compressor, Ceiling on a limiter) that the module has, and those nowhere else.
        std::vector<juce::String> out;
        if (hasOut)
            for (const auto& item : lanes.back()->items)
                if (const auto* param = std::get_if<synth::CardParamItem>(&item))
                    out.push_back(param->paramId);
        const auto rank = [](const juce::String& id) { return id == "mix" ? 0 : id == "pan" ? 1 : 2; };
        if (hasOut) {
            ASSERT_FALSE(out.empty());
            for (size_t i = 0; i < out.size(); ++i) {
                EXPECT_TRUE(isOutControlId(out[i])) << out[i] << " is not an Out control";
                if (i > 0)
                    EXPECT_LT(rank(out[i - 1]), rank(out[i])) << "Out order is Mix, Pan, then the output control";
            }
            EXPECT_NE(rank(out.back()), 1) << "the Out lane ends with the output control, not Pan";
        }
        for (size_t i = 0; i < sections.size(); ++i)
            if (sections[i].id != CardSection::kOutLaneId)
                for (const auto& item : sections[i].items)
                    if (const auto* param = std::get_if<synth::CardParamItem>(&item))
                        EXPECT_FALSE(isOutControlId(param->paramId)) << param->paramId << " belongs in the Out lane";
        EXPECT_TRUE(std::none_of(layout.hidden.begin(), layout.hidden.end(), isOutControlId))
            << "no Out control in More";
    }
    EXPECT_GE(checked, 20) << "the effect and source cards are checked";
}

TEST(ModuleDesignLanguage, EveryLaneCardKeepsItsControlsClearOfTheRulesAndEachOtherAndInsideTheCard) {
    const auto& defaults = synth::DefaultCardLayouts::builtIn();
    for (const auto& type : libraryTypes()) {
        const auto* entry = defaults.find(type);
        if (entry == nullptr || exemptFromLanes().count(type) > 0 || !hasLanes(entry->layout))
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
        EXPECT_GE(rules.size(), isSingleLane(type) ? 0u : 1u);
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

        // A lane's title and the hairline under it stay clear of every control box, value boxes included (the Out
        // lane's corner readouts sit just under them).
        for (const auto& section : plan.sections)
            for (const juce::Component* part :
                 {(const juce::Component*)section.header, (const juce::Component*)section.titleRule})
                if (part != nullptr && !part->getBounds().isEmpty())
                    for (const auto* item : items)
                        EXPECT_FALSE(boxOf(*item).expanded(0, 1).intersects(part->getBounds()))
                            << item->param->paramID << " touches the title or rule of lane " << section.id;

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
            if (extent.size() < 2)
                continue; // a card with one lane (the Drum Kit's Out) is carried by what sits under it
            EXPECT_LE(left, kKnobCell + kLaneSlack) << "lane " << lane << " leaves a spare block on its left";
            EXPECT_LE(right, kKnobCell + kLaneSlack) << "lane " << lane << " leaves a spare block on its right";
            ++slot;
        }
    }
}

TEST(ModuleDesignLanguage, TabGoesDownAColumnThenAcrossTheLanesAndTheModeRowComesFirst) {
    const auto& defaults = synth::DefaultCardLayouts::builtIn();
    for (const auto& type : libraryTypes()) {
        const auto* entry = defaults.find(type);
        if (entry == nullptr || exemptFromLanes().count(type) > 0 || !hasLanes(entry->layout))
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
        // A chrome pill in the mode row (Show Scope) is a stop of the mode row: before every lane stop.
        for (size_t at = 0; at < stops.size(); ++at)
            if (plan.hasModeRow() && stops[at]->getTitle() == synth::cardbody::kShowScopeText)
                for (size_t later = 0; later < at; ++later)
                    for (const auto& item : plan.items)
                        if (item.param != nullptr && item.widget == stops[later])
                            EXPECT_LT(item.laneSpot.lane, 0) << "Show Scope comes before the lane stops";
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

// ---- The effect cards ------------------------------------------------------------------------------------------
// Source/UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsEffects.cpp: the designed default card of
// Delay, Reverb, Chorus, Phaser, Flanger, Distortion, Bitcrusher, Ring Modulator and Pitch Shifter, read
// on real cards: the sections in order, the named widgets, the conditions (a swap keeps its cell), every
// control reachable by keyboard with a title and a tooltip, and the size estimate against the real card.

using Kind = synth::CardBodyItem::Kind;

namespace {

struct Expected {
    juce::String type;
    std::vector<std::vector<juce::String>> sections; ///< Param ids per section: the mode row, the lanes, the Out lane.
    std::map<juce::String, Kind> kinds;
};

const std::vector<juce::String> kOut = {"mix", "pan", "outputLevel"};

std::vector<Expected> expectations() {
    return {
        {"Delay",
         {{"tempoSync", "time", "timeDiv"}, {"feedback", "pingPong"}, kOut},
         {{"tempoSync", Kind::Toggle},
          {"time", Kind::KnobLarge},
          {"timeDiv", Kind::Choice},
          {"pingPong", Kind::Toggle},
          {"mix", Kind::Knob},
          {"outputLevel", Kind::Knob}}},
        {"Reverb",
         {{"roomSize"}, {"damping", "preDelay"}, {"width"}, kOut},
         {{"roomSize", Kind::KnobLarge}, {"width", Kind::Knob}, {"mix", Kind::Knob}}},
        {"Chorus",
         {{"rate"}, {"centreDelay", "depth", "feedback"}, kOut},
         {{"rate", Kind::KnobLarge}, {"outputLevel", Kind::Knob}}},
        {"Flanger",
         {{"rate"}, {"centreDelay", "depth", "feedback"}, kOut},
         {{"rate", Kind::KnobLarge}, {"centreDelay", Kind::Knob}}},
        {"Phaser",
         {{"rate"}, {"centreFreq", "depth", "feedback"}, kOut},
         {{"rate", Kind::KnobLarge}, {"centreFreq", Kind::Knob}}},
        {"Distortion",
         {{"type", "oversampling"}, {"drive"}, kOut},
         {{"type", Kind::Segmented}, {"drive", Kind::KnobLarge}, {"oversampling", Kind::Choice}}},
        {"Bitcrusher",
         {{"depth", "rate", "dither"}, kOut},
         {{"dither", Kind::Knob}, {"depth", Kind::Knob}, {"outputLevel", Kind::Knob}}},
        {"Ring Modulator",
         {{"oversampling"}, {"drive"}, {"character"}, kOut},
         {{"drive", Kind::KnobLarge}, {"oversampling", Kind::Choice}, {"character", Kind::Knob}}},
        {"Pitch Shifter",
         {{"shiftMode"}, {"pitch", "shiftHz"}, {"fine", "window", "feedback"}, kOut},
         {{"shiftMode", Kind::Segmented},
          {"pitch", Kind::KnobLarge},
          {"shiftHz", Kind::KnobLarge},
          {"outputLevel", Kind::Knob}}},
    };
}

juce::Component* widget(CardCanvas& canvas, NodeID id, const juce::String& paramId) {
    return canvas.card(id)->getCardBody()->findWidget(paramId);
}

void flip(CardCanvas& canvas, NodeID id, const juce::String& paramId, float plainValue) {
    auto* param = findParameterByID(canvas.processor(id), paramId);
    ASSERT_NE(param, nullptr) << paramId;
    param->setValueNotifyingHost(param->convertTo0to1(plainValue));
    canvas.card(id)->getCardBody()->flushPendingConditionUpdate();
}

} // namespace

TEST(EffectsDefaultLayout, EachCardHasTheDesignedSectionsAndWidgetsInOrder) {
    for (const auto& expected : expectations()) {
        SCOPED_TRACE(expected.type.toStdString());
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(expected.type), 0, 0);
        canvas.editor.updateComponents();
        const auto& plan = canvas.card(id)->getCardBody()->getPlan();
        ASSERT_EQ(plan.sections.size(), expected.sections.size());
        for (size_t s = 0; s < expected.sections.size(); ++s) {
            std::vector<juce::String> ids;
            for (int index : plan.sections[s].items)
                ids.push_back(plan.items[(size_t)index].param->paramID);
            EXPECT_EQ(ids, expected.sections[s]) << "section " << s;
        }
        EXPECT_FALSE(plan.sections.back().footer) << "no footer: Mix, Pan and Output moved to the Out lane";
        EXPECT_TRUE(plan.sections.back().outLane) << "the Out lane is always the last lane";
        EXPECT_TRUE(plan.more.empty()) << "nothing waits in the More row";
        for (const auto& [paramId, kind] : expected.kinds) {
            const int index = plan.findParam(paramId);
            ASSERT_GE(index, 0) << paramId;
            EXPECT_EQ(plan.items[(size_t)index].kind, kind) << paramId;
        }
        for (const auto& section : plan.sections)
            if (section.title.has_value())
                EXPECT_NE(*section.title, section.title->toUpperCase()) << "section titles are sentence case";
    }
}

TEST(EffectsDefaultLayout, EveryShownControlIsKeyboardReachableWithATitleAndATooltip) {
    for (const auto& expected : expectations()) {
        SCOPED_TRACE(expected.type.toStdString());
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(expected.type), 0, 0);
        canvas.editor.updateComponents();
        auto* card = canvas.card(id);
        const auto tabbable = card->createKeyboardFocusTraverser()->getAllComponents(card);
        const auto& plan = card->getCardBody()->getPlan();
        for (const auto& item : plan.items) {
            if (item.param == nullptr || item.section < 0 || !item.shown ||
                !plan.sections[(size_t)item.section].visible)
                continue;
            SCOPED_TRACE(item.param->paramID.toStdString());
            auto* control = card->getCardBody()->findWidget(item.param->paramID);
            ASSERT_NE(control, nullptr);
            EXPECT_TRUE(control->isVisible());
            EXPECT_TRUE(control->getWantsKeyboardFocus());
            EXPECT_NE(std::find(tabbable.begin(), tabbable.end(), control), tabbable.end());
            EXPECT_TRUE(control->getTitle().isNotEmpty());
            auto* tip = dynamic_cast<juce::SettableTooltipClient*>(control);
            ASSERT_NE(tip, nullptr);
            EXPECT_TRUE(tip->getTooltip().isNotEmpty());
        }
    }
}

TEST(EffectsDefaultLayout, TheDelayTempoSyncSwapsTimeForItsDivisionInOneCell) {
    CardCanvas canvas;
    const auto id = canvas.add(synth::AIStateMapper::createModule("Delay"), 0, 0);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    auto* time = widget(canvas, id, "time");
    auto* division = widget(canvas, id, "timeDiv");
    ASSERT_NE(time, nullptr);
    ASSERT_NE(division, nullptr);
    const auto cardBounds = card->getBounds();
    const auto feedbackBounds = widget(canvas, id, "feedback")->getBounds();

    EXPECT_TRUE(time->isVisible()) << "free time is the default";
    EXPECT_FALSE(division->isVisible());
    flip(canvas, id, "tempoSync", 1.0f);
    EXPECT_TRUE(division->isVisible());
    EXPECT_FALSE(time->isVisible());
    EXPECT_EQ(division->getPosition(), time->getPosition()) << "the swap keeps its cell";
    EXPECT_EQ(card->getBounds(), cardBounds);
    EXPECT_EQ(widget(canvas, id, "feedback")->getBounds(), feedbackBounds);
    flip(canvas, id, "tempoSync", 0.0f);
    EXPECT_TRUE(time->isVisible());
    EXPECT_EQ(card->getBounds(), cardBounds);
}

TEST(EffectsDefaultLayout, ThePitchShifterFrequencyModeSwapsShiftForPitchAndDimsFine) {
    CardCanvas canvas;
    const auto id = canvas.add(synth::AIStateMapper::createModule("Pitch Shifter"), 0, 0);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    auto* pitch = widget(canvas, id, "pitch");
    auto* shift = widget(canvas, id, "shiftHz");
    auto* fine = widget(canvas, id, "fine");
    const auto cardBounds = card->getBounds();
    const auto fineBounds = fine->getBounds();
    EXPECT_TRUE(pitch->isVisible());
    EXPECT_FALSE(shift->isVisible());
    EXPECT_FALSE(synth::theme::AppLookAndFeel::paintsDimmed(*fine));

    flip(canvas, id, "shiftMode", 1.0f); // Frequency
    EXPECT_FALSE(pitch->isVisible());
    EXPECT_TRUE(shift->isVisible());
    EXPECT_EQ(shift->getPosition(), pitch->getPosition()) << "Shift takes the cell Pitch had";
    EXPECT_EQ(card->getBounds(), cardBounds) << "a swap and a dim never resize the card";
    EXPECT_EQ(fine->getBounds(), fineBounds) << "Fine keeps its cell";
    EXPECT_TRUE(fine->isVisible());
    EXPECT_TRUE(fine->isEnabled());
    EXPECT_TRUE(fine->getWantsKeyboardFocus()) << "a dimmed Fine is still a Tab stop";
    EXPECT_TRUE(synth::theme::AppLookAndFeel::paintsDimmed(*fine));
    EXPECT_TRUE(card->getCardBody()->getPlan().more.empty() ||
                std::count(card->getCardBody()->getPlan().more.begin(), card->getCardBody()->getPlan().more.end(),
                           card->getCardBody()->getPlan().findParam("shiftHz")) == 0);

    flip(canvas, id, "shiftMode", 0.0f);
    EXPECT_TRUE(pitch->isVisible());
    EXPECT_FALSE(shift->isVisible());
    EXPECT_FALSE(synth::theme::AppLookAndFeel::paintsDimmed(*fine));
    EXPECT_EQ(card->getBounds(), cardBounds);
}

TEST(EffectsDefaultLayout, TheEstimateEqualsTheRealCardForEveryEffect) {
    CardCanvas canvas;
    std::map<juce::String, NodeID> ids;
    int x = 0;
    for (const auto& expected : expectations()) {
        ids[expected.type] = canvas.add(synth::AIStateMapper::createModule(expected.type), x, 0);
        x += 400;
    }
    canvas.editor.updateComponents();
    for (const auto& [type, id] : ids) {
        SCOPED_TRACE(type.toStdString());
        const auto estimate = synth::measureDataDrivenCardSize(type);
        ASSERT_TRUE(estimate.has_value());
        auto* card = canvas.card(id);
        EXPECT_EQ(*estimate, juce::Point<int>(card->getWidth(), card->getHeight()));
    }
}

TEST(EffectsDefaultLayout, ChorusAndFlangerRelabelCentreDelayAndKeepTheFullNameInTheTooltip) {
    for (const char* type : {"Chorus", "Flanger"}) {
        SCOPED_TRACE(type);
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
        canvas.editor.updateComponents();
        const auto& plan = canvas.card(id)->getCardBody()->getPlan();
        auto* label = dynamic_cast<juce::Label*>(plan.items[(size_t)plan.findParam("centreDelay")].label);
        ASSERT_NE(label, nullptr);
        EXPECT_EQ(label->getText(), "Delay");
        EXPECT_EQ(label->getTooltip(), "Centre Delay");
    }
}

// Regression test for FRO2324: the Limiter's Ceiling title lost the tail of its g. A caption is at least as tall as its
// font (ascent plus descent), so a descender is never cut.
TEST(CaptionHeightFit, EveryCaptionOnEveryCardIsTallEnoughForItsFont) {
    std::set<juce::String> types;
    for (const auto& type : libraryTypes())
        types.insert(type);
    for (const auto& type : synth::DefaultCardLayouts::builtIn().types())
        types.insert(type);
    ASSERT_GT(types.size(), 20u);

    int captions = 0;
    for (const auto& type : types) {
        for (const bool moreOpen : {false, true}) {
            SCOPED_TRACE(type + (moreOpen ? " more=open" : " more=folded"));
            CardCanvas canvas;
            auto processor = synth::AIStateMapper::createModule(type);
            if (processor == nullptr)
                continue;
            const auto id = canvas.add(std::move(processor), 0, 0);
            canvas.editor.updateComponents();
            auto* card = canvas.card(id);
            ASSERT_NE(card, nullptr);
            auto* body = card->getCardBody();
            if (body == nullptr)
                continue;
            if (moreOpen)
                body->setMoreUnfolded(true);
            for (const auto& item : body->getPlan().items) {
                auto* label = dynamic_cast<juce::Label*>(item.label);
                if (label == nullptr || !label->isVisible() || label->getBounds().isEmpty() ||
                    label->getText().isEmpty())
                    continue;
                const auto font = label->getFont();
                const int textHeight = (int)std::ceil(font.getAscent() + font.getDescent());
                EXPECT_GE(label->getHeight(), textHeight)
                    << "caption \"" << label->getText().toStdString() << "\" is " << label->getHeight()
                    << " px tall for a " << textHeight << " px font";
                ++captions;
            }
        }
    }
    EXPECT_GT(captions, 100);
}
