// WavetableDefaultLayoutTests.cpp
//
// The Wavetable's designed default card layout, drawn by a real card on a canvas with no override: five
// `tab` sections drawn as one tab strip in order, Position and Warp pinned above it, Pan on Tune and
// Sync In on Phase, a tab switch that moves nothing on the card, the size estimate against the real
// card, the strip's keys and accessibility, the tab kept per card and never saved, and the tabs listed,
// hidden from, reordered and renamed in the layout editor.
// Source/UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsSources.cpp, Source/UI/Graph/CardBody/CardBodyTabs.cpp.

#include "../../../Accessibility/AccessibilityAudit.h"
#include "../../CardLayoutEditor/CardLayoutEditorTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "UI/Graph/CardBody/CardBodyMeasure.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include <set>

using namespace cardbody_test;
using synth::ui::CardSegmentedSwitch;

namespace {

const juce::StringArray kTabs{"Tune", "Unison", "Phase", "Sub", "File"};

struct Wavetable {
    NodeID id;
    ModuleComponent* card = nullptr;
    synth::CardBody* body = nullptr;
    CardSegmentedSwitch* strip = nullptr;
};

Wavetable build(CardCanvas& canvas, int x = 0) {
    Wavetable w;
    w.id = canvas.add(synth::AIStateMapper::createModule("Wavetable"), x, 0);
    canvas.editor.updateComponents();
    w.card = canvas.card(w.id);
    w.body = w.card->getCardBody();
    w.strip = dynamic_cast<CardSegmentedSwitch*>(w.body->getTabStrip(0));
    return w;
}

juce::Component* widget(const Wavetable& w, const juce::String& paramId) { return w.body->findWidget(paramId); }

// The tab `paramId` sits on; "" for a control outside the tabs, "<more>" for one in the More row.
juce::String tabOf(const Wavetable& w, const juce::String& paramId) {
    const auto& plan = w.body->getPlan();
    const int item = plan.findParam(paramId);
    if (item < 0)
        return "<none>";
    const int section = plan.items[(size_t)item].section;
    if (section < 0)
        return "<more>";
    return plan.sections[(size_t)section].tabGroup >= 0 ? plan.tabTitle(section) : juce::String();
}

// Everything on the card a tab switch must leave alone: its size, every jack centre, and the bounds and
// visibility of every child but the tabs' own controls and captions.
struct Geometry {
    juce::Rectangle<int> card;
    std::vector<juce::Point<int>> jacks;
    std::vector<std::pair<juce::Rectangle<int>, bool>> children;

    bool operator==(const Geometry& other) const {
        return card == other.card && jacks == other.jacks && children == other.children;
    }
};

Geometry geometryOf(const Wavetable& w) {
    std::set<juce::Component*> tabbed;
    for (const auto& item : w.body->getPlan().items)
        if (item.widget != nullptr && w.body->isTabbed(*item.widget)) {
            tabbed.insert(item.widget);
            tabbed.insert(item.label);
        }
    Geometry g;
    g.card = w.card->getBounds();
    auto* module = dynamic_cast<ModuleBase*>(w.card->getModule());
    for (int i = 0; i < module->getVisibleInputPortCount(); ++i)
        g.jacks.push_back(w.card->getPortCenter(i, true));
    for (int i = 0; i < module->getVisibleOutputPortCount(); ++i)
        g.jacks.push_back(w.card->getPortCenter(i, false));
    for (auto* child : w.card->getChildren())
        if (tabbed.count(child) == 0)
            g.children.emplace_back(child->getBounds(), child->isVisible());
    return g;
}

const std::vector<std::vector<const char*>> kTabControls = {
    {"octave", "coarse", "fine", "level", "pan"},
    {"stack", "unison", "detune", "width", "blend"},
    {"syncMode", "phase", "randomPhase", "spread"},
    {"subOctave", "subShape", "subLevel"},
    {"importMode", "interpolation"},
};

} // namespace

TEST(WavetableDefaultLayout, TheFiveTabsAreOneStripInOrderUnderThePinnedControls) {
    CardCanvas canvas;
    const auto w = build(canvas);
    const auto& plan = w.body->getPlan();
    ASSERT_EQ(plan.tabGroups.size(), 1u) << "consecutive tab sections are one strip";
    juce::StringArray titles;
    for (int section : plan.tabGroups[0].sections)
        titles.add(plan.tabTitle(section));
    EXPECT_EQ(titles, kTabs);

    ASSERT_NE(w.strip, nullptr);
    ASSERT_EQ(w.strip->getNumSegments(), kTabs.size());
    for (int i = 0; i < kTabs.size(); ++i)
        EXPECT_EQ(w.strip->getSegment(i)->getButtonText(), kTabs[i]);
    EXPECT_EQ(w.strip->getSelectedIndex(), 0) << "a new card opens on Tune";
    EXPECT_EQ(w.strip->getWidth(), w.card->getWidth() - 2 * synth::cardbody::kContentMargin);

    for (const auto* pinned : {"position", "warp", "warpAmount"})
        EXPECT_LT(widget(w, pinned)->getBottom(), w.strip->getY()) << pinned << " sits above the strip";
    EXPECT_GT(widget(w, "octave")->getY(), w.strip->getBottom()) << "the tab's controls sit under the strip";
    EXPECT_FALSE(w.body->hasMoreRow()) << "every parameter is placed";
    ASSERT_TRUE(w.body->hasFooter());
    EXPECT_GT(widget(w, "poly")->getY(), widget(w, "octave")->getBottom()) << "Poly joins the footer row";
    EXPECT_EQ(w.body->findWidget("table"), nullptr) << "Table is the chrome's, beside the display";
}

TEST(WavetableDefaultLayout, PositionAndWarpArePinnedOnEveryTab) {
    CardCanvas canvas;
    const auto w = build(canvas);
    std::vector<juce::Rectangle<int>> pinned;
    for (const auto* id : {"position", "warp", "warpAmount"}) {
        EXPECT_EQ(tabOf(w, id), "") << id << " is outside the tabs";
        pinned.push_back(widget(w, id)->getBounds());
    }
    for (int tab = 0; tab < kTabs.size(); ++tab) {
        w.body->selectTab(0, tab);
        int i = 0;
        for (const auto* id : {"position", "warp", "warpAmount"}) {
            EXPECT_TRUE(widget(w, id)->isVisible()) << id << " on " << kTabs[tab];
            EXPECT_EQ(widget(w, id)->getBounds(), pinned[(size_t)i++]) << id << " on " << kTabs[tab];
        }
    }
}

TEST(WavetableDefaultLayout, EachTabHoldsItsControlsWithPanOnTuneAndSyncInOnPhase) {
    CardCanvas canvas;
    const auto w = build(canvas);
    EXPECT_EQ(tabOf(w, "pan"), "Tune");
    EXPECT_EQ(tabOf(w, "syncMode"), "Phase");
    for (int tab = 0; tab < kTabs.size(); ++tab) {
        SCOPED_TRACE(kTabs[tab].toStdString());
        w.body->selectTab(0, tab);
        for (int other = 0; other < kTabs.size(); ++other)
            for (const auto* id : kTabControls[(size_t)other]) {
                EXPECT_EQ(tabOf(w, id), kTabs[other]) << id;
                EXPECT_EQ(widget(w, id)->isVisible(), other == tab) << id;
            }
    }
}

TEST(WavetableDefaultLayout, ATabSwitchChangesNoCardGeometry) {
    CardCanvas canvas;
    const auto w = build(canvas);
    const auto atRest = geometryOf(w);
    const int footerY = widget(w, "poly")->getY();
    for (int tab : {1, 2, 3, 4, 0, 3}) {
        SCOPED_TRACE(kTabs[tab].toStdString());
        w.strip->setSelectedIndex(tab, juce::sendNotificationSync); // what a click or a key does
        EXPECT_EQ(w.body->getSelectedTab(0), tab);
        EXPECT_TRUE(geometryOf(w) == atRest) << "the card, a jack or a control outside the tabs moved";
        for (const auto* id : kTabControls[(size_t)tab]) {
            const auto bounds = widget(w, id)->getBounds();
            EXPECT_GT(bounds.getY(), w.strip->getBottom()) << id;
            EXPECT_LT(bounds.getBottom(), footerY) << id << " overlaps the footer";
            EXPECT_TRUE(w.card->getLocalBounds().contains(bounds)) << id;
        }
    }
}

// The jacks of the tabs' knobs stay in the gutter on every tab (a knob on a tab never binds its jack),
// so the gutter is two left-hand columns whichever tab is shown; only the pinned knobs take theirs.
TEST(WavetableDefaultLayout, OnlyThePinnedKnobsTakeTheirJacks) {
    CardCanvas canvas;
    const auto w = build(canvas);
    const auto drawn = w.card->drawnInputJackIndices();
    EXPECT_EQ((int)drawn.size(), WavetableOscillatorModule::kNumJacks - 2);
    EXPECT_TRUE(w.card->isInputJackKnobBound(WavetableOscillatorModule::kJackPosition));
    EXPECT_EQ(w.card->getPortCenter(WavetableOscillatorModule::kJackPosition, true).y,
              juce::roundToInt(w.card->getModTargetKnobAnchor(WavetableOscillatorModule::kJackPosition)->y));
    std::set<int> columns;
    for (int jack : drawn)
        columns.insert(w.card->getPortCenter(jack, true).x);
    EXPECT_EQ(columns.size(), 2u);
}

TEST(WavetableDefaultLayout, TheEstimateEqualsTheRealCard) {
    CardCanvas canvas;
    const auto w = build(canvas);
    const juce::Point<int> real(w.card->getWidth(), w.card->getHeight());
    const auto measured = synth::measureDataDrivenCardSize("Wavetable");
    ASSERT_TRUE(measured.has_value()) << "the Wavetable is measured from its plan, not a table";
    EXPECT_EQ(*measured, real);
    EXPECT_EQ(GraphEditor::estimateModuleSize("Wavetable"), real);
    const auto g = synth::cardbody::BodyGeometry::forCardWidth(w.card->getWidth());
    for (int tab = 0; tab < kTabs.size(); ++tab) {
        w.body->selectTab(0, tab);
        EXPECT_EQ(w.body->layout(300, g, false), w.body->layout(300, g, true)) << kTabs[tab];
    }
}

TEST(WavetableDefaultLayout, TheStripIsOneTabStopAndTheArrowKeysSwitchTabs) {
    CardCanvas canvas;
    const auto w = build(canvas);
    ASSERT_NE(w.strip, nullptr);
    EXPECT_TRUE(w.strip->getWantsKeyboardFocus());
    const auto stops = w.card->createKeyboardFocusTraverser()->getAllComponents(w.card);
    EXPECT_EQ(std::count(stops.begin(), stops.end(), w.strip), 1);
    for (int i = 0; i < w.strip->getNumSegments(); ++i) {
        auto* segment = w.strip->getSegment(i);
        EXPECT_FALSE(segment->getWantsKeyboardFocus());
        EXPECT_EQ(std::count(stops.begin(), stops.end(), segment), 0);
        EXPECT_EQ(segment->getTitle(), kTabs[i]);
        EXPECT_TRUE(segment->getTooltip().contains(kTabs[i]));
    }
    EXPECT_TRUE(w.strip->getTitle().isNotEmpty());
    EXPECT_TRUE(w.strip->getTooltip().containsIgnoreCase("left and right"));
    const auto handler = w.strip->createAccessibilityHandler();
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->getRole(), juce::AccessibilityRole::group);

    const auto press = [&](int keyCode, int tab, const char* shown) {
        EXPECT_TRUE(w.strip->keyPressed(juce::KeyPress(keyCode)));
        EXPECT_EQ(w.body->getSelectedTab(0), tab);
        EXPECT_TRUE(widget(w, shown)->isVisible()) << shown;
    };
    press(juce::KeyPress::rightKey, 1, "detune");
    press(juce::KeyPress::rightKey, 2, "syncMode");
    press(juce::KeyPress::endKey, 4, "importMode");
    press(juce::KeyPress::rightKey, 4, "importMode"); // stops at the end
    press(juce::KeyPress::leftKey, 3, "subLevel");
    press(juce::KeyPress::homeKey, 0, "pan");
    EXPECT_FALSE(widget(w, "detune")->isVisible());
    EXPECT_FALSE(w.strip->keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::commandModifier, 0)))
        << "a modified arrow is left to the app's shortcuts";

    EXPECT_TRUE(synth::test::auditAccessibility(*w.card).empty()) << "every control on the card is named";
}

// The selected tab is the card's own: another Wavetable card keeps its tab, and nothing about it reaches
// the patch (no layout override, no card view state).
TEST(WavetableDefaultLayout, TheSelectedTabIsPerCardAndNeverSaved) {
    CardCanvas canvas;
    const auto first = build(canvas, 0);
    const auto second = build(canvas, 700);
    first.body->selectTab(0, 2);
    EXPECT_EQ(first.body->getSelectedTab(0), 2);
    EXPECT_EQ(canvas.card(second.id)->getCardBody()->getSelectedTab(0), 0);
    EXPECT_TRUE(synth::getCardLayoutOverride(canvas.engine.getGraph(), first.id).isVoid());
    auto* module = dynamic_cast<ModuleBase*>(canvas.processor(first.id));
    EXPECT_TRUE(module->getCardViewState().isDefault());
}

// The layout editor lists each tab as a group headed "Tab: <title>"; a control can be hidden from a
// tab, moved within it, and the tab renamed, and the card keeps one strip throughout.
TEST(WavetableDefaultLayout, TheLayoutEditorListsTheTabsAndHidesMovesAndRenamesWithinThem) {
    cardlayouteditor_test::EditorCanvas rig;
    const auto id = rig.add(synth::AIStateMapper::createModule("Wavetable"));
    auto* editor = rig.openFromModuleMenu(id);
    ASSERT_NE(editor, nullptr);
    for (int tab = 0; tab < kTabs.size(); ++tab) {
        const int row = cardlayouteditor_test::rowOf(*editor, "#" + juce::String(tab + 1));
        EXPECT_EQ(editor->getRowForTest(row)->getTitle(), "Tab: " + kTabs[tab]);
    }
    EXPECT_EQ(editor->getRowForTest(cardlayouteditor_test::rowOf(*editor, "#0"))
                  ->getTitle()
                  .upToFirstOccurrenceOf(":", false, false),
              "Group");

    editor->triggerRowToggleForTest(cardlayouteditor_test::rowOf(*editor, "pan"));
    EXPECT_TRUE(rig.storedLayout(id)->hidden.contains("pan"));
    EXPECT_TRUE(rig.card(id)->getCardBody()->hasMoreRow()) << "a hidden control goes to the More row";

    const juce::KeyPress cmdDown(juce::KeyPress::downKey, juce::ModifierKeys::commandModifier, 0);
    ASSERT_TRUE(editor->pressKeyOnRowForTest(cardlayouteditor_test::rowOf(*editor, "octave"), cmdDown));
    const auto stored = rig.storedLayout(id);
    ASSERT_TRUE(stored.has_value());
    const auto& tune = stored->sections[1];
    ASSERT_GE(tune.items.size(), 2u);
    EXPECT_EQ(std::get<synth::CardParamItem>(tune.items[0]).paramId, "coarse");
    EXPECT_EQ(std::get<synth::CardParamItem>(tune.items[1]).paramId, "octave");
    EXPECT_EQ(tune.presentation, synth::CardPresentation::Tab) << "an edit keeps the section a tab";

    const int header = cardlayouteditor_test::rowOf(*editor, "#1");
    editor->setRowLabelForTest(header, "Pitch");
    editor->commitRowLabelForTest(header);
    auto* card = rig.card(id);
    ASSERT_EQ(card->getCardBody()->getPlan().tabGroups.size(), 1u);
    auto* strip = dynamic_cast<CardSegmentedSwitch*>(card->getCardBody()->getTabStrip(0));
    ASSERT_NE(strip, nullptr);
    EXPECT_EQ(strip->getSegment(0)->getButtonText(), "Pitch");
    EXPECT_LT(card->getCardBody()->findWidget("coarse")->getX(), card->getCardBody()->findWidget("octave")->getX());
}
