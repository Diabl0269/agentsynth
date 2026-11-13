// The port connections panel shows a modulation cable the way the knob's sources panel does: the amount bar, the typed
// percent, the curve chip and show in timeline, from the jack the cable lands on and from the jack it leaves (an LFO's
// output), and from an output that also feeds audio (an Oscillator: its audio rows stay plain). A drag of the amount
// is one undo step; an audio row has none of it.
// docs/layout/cables.md#port-connections-panel.

#include "../ModDot/ModDotTestFixture.h"

#include "Modules/OscillatorModule.h"
#include "UI/Graph/ModDot/ModDotSourceRow.h"
#include "UI/Graph/ModDot/ModDotSourcesPage.h"
#include "UI/Graph/PortPanel/PortConnectionList.h"
#include "UI/Graph/PortPanel/PortConnectionsPanel.h"
#include "UI/Graph/PortPanel/PortPanelController.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/ModuleViews/ModCurve/ModCurveChip.h"
#include "UI/ModuleViews/ModCurve/ModCurvePopover.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <algorithm>
#include <map>

namespace {

using synth::ui::ModDotSourceRow;
using synth::ui::PortConnectionRow;
using synth::ui::PortConnectionsPanel;
using synth::ui::PortRef;

struct NoMotion {
    NoMotion() { synth::ui::setReducedMotionForTest(true); }
    ~NoMotion() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

// The LFO -> VCA Gain cable on the Gain CV jack (no knob, so the cable lands on a visible jack), with the panel a click
// on either end of it opens captured by a launcher (a real window needs a display).
struct ModPanelFixture : Fixture {
    std::unique_ptr<juce::Component> heldPanel;

    ModPanelFixture()
        : Fixture(/*modulated*/ true, /*gainAsKnob*/ false) {
        editor->getPortPanel().panelLauncher = [this](std::unique_ptr<juce::Component> content, juce::Component&,
                                                      juce::Rectangle<int>) { heldPanel = std::move(content); };
    }
    ~ModPanelFixture() { heldPanel.reset(); }

    GraphEditor::VisibleCable modCable() {
        for (const auto& c : editor->buildVisibleCables())
            if (c.id.attenUid != 0)
                return c;
        return {};
    }
    ModuleBase* module(juce::AudioProcessorGraph::NodeID id) {
        auto* node = engine.getGraph().getNodeForId(id);
        return node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    }
    PortRef destinationJack() {
        const auto c = modCable();
        return {vcaId, module(vcaId)->mapInputChannel(c.id.dstPort).visibleJackIndex, true, false};
    }
    PortRef sourceJack() {
        const auto c = modCable();
        return {lfoId, module(lfoId)->mapOutputChannel(c.id.srcPort).visibleJackIndex, false, false};
    }
    PortConnectionsPanel* open(const PortRef& port) {
        editor->getPortPanel().open(*editor, juce::Rectangle<int>(0, 0, 10, 10), port);
        return editor->getPortPanel().getPanel();
    }
};

juce::MouseEvent clickAt(juce::Component& c, juce::Point<int> p) { return makeModuleClickWithMods(c, p, kPlain); }

// Every control of a modulation row a person can reach: on screen, enabled, inside the row and taking the keyboard.
void expectReachable(ModDotSourceRow& row) {
    for (juce::Component* c : std::initializer_list<juce::Component*>{&row.bar(), &row.amountButton(), &row.curveChip(),
                                                                      &row.timelineButton(), &row.removeButton()}) {
        EXPECT_TRUE(c->isVisible());
        EXPECT_TRUE(c->isEnabled());
        EXPECT_TRUE(c->getWantsKeyboardFocus());
        EXPECT_GT(c->getWidth(), 0);
        EXPECT_GT(c->getHeight(), 0);
        EXPECT_TRUE(row.getLocalBounds().contains(c->getBounds())) << c->getTitle();
    }
}

} // namespace

TEST_F(ModuleComponentTest, ADestinationJackListsTheModulationWithAmountPercentAndCurve) {
    NoMotion motion;
    ModPanelFixture f;
    auto* panel = f.open(f.destinationJack());
    ASSERT_NE(panel, nullptr);
    ASSERT_EQ(panel->rowCount(), 1);
    auto* row = panel->rowAt(0);
    ASSERT_NE(row->modRow(), nullptr) << "a modulation cable gets the mod controls";
    EXPECT_EQ(row->getHeight(), PortConnectionRow::kHeight) << "a mod row is one line";
    EXPECT_NEAR(row->modRow()->bar().amount(), 0.5f, 1e-4f);
    EXPECT_EQ(row->modRow()->amountButton().getButtonText(), synth::ui::ModDotAmountBar::percentText(0.5f));
    EXPECT_EQ(row->modRow()->curveChip().getWidth(), synth::ui::ModCurveChip::kWidth);
    EXPECT_NE(row->modRow()->curveChip().target(), nullptr) << "the chip edits this cable's curve";
    expectReachable(*row->modRow());
    EXPECT_EQ(&row->removeButton(), &row->modRow()->removeButton()) << "the panel's keys find one remove button";
}

TEST_F(ModuleComponentTest, ALfoOutputJackListsItsModulationsWithTheSameControls) {
    NoMotion motion;
    ModPanelFixture f;
    f.addSecondLfo(); // a second LFO onto the same Gain: the first LFO's jack still lists only its own cable
    auto* panel = f.open(f.sourceJack());
    ASSERT_NE(panel, nullptr);
    ASSERT_EQ(panel->rowCount(), 1);
    auto* row = panel->rowAt(0);
    ASSERT_NE(row->modRow(), nullptr) << "the source side lists the same controls";
    EXPECT_EQ(row->modRow()->attenuverterId(), f.attenId);
    EXPECT_NEAR(row->modRow()->bar().amount(), 0.5f, 1e-4f);
    expectReachable(*row->modRow());
}

TEST_F(ModuleComponentTest, DraggingTheAmountInThePanelIsOneUndoStepFromEitherJack) {
    NoMotion motion;
    for (const bool fromSource : {false, true}) {
        ModPanelFixture f;
        auto* panel = f.open(fromSource ? f.sourceJack() : f.destinationJack());
        ASSERT_NE(panel, nullptr);
        auto& bar = panel->rowAt(0)->modRow()->bar();
        const bool undoBefore = f.undo.canUndo();

        bar.mouseDown(clickAt(bar, {(int)bar.xForValue(0.5f), 5}));
        bar.mouseDrag(clickAt(bar, {(int)bar.xForValue(0.9f), 5}));
        bar.mouseDrag(clickAt(bar, {(int)bar.xForValue(-0.3f), 5}));
        bar.mouseUp(clickAt(bar, {(int)bar.xForValue(-0.3f), 5}));
        EXPECT_NEAR(f.amount(f.attenId), -0.3f, 0.05f) // the bar is a few dozen pixels wide here
            << (fromSource ? "source" : "destination");

        ASSERT_TRUE(f.undo.undo());
        EXPECT_NEAR(f.amount(f.attenId), 0.5f, 1e-4f) << "one undo undoes the whole drag";
        EXPECT_EQ(f.undo.canUndo(), undoBefore) << "and nothing else was recorded";
    }
}

TEST_F(ModuleComponentTest, ATypedPercentInThePanelIsOneUndoStep) {
    NoMotion motion;
    ModPanelFixture f;
    auto* panel = f.open(f.destinationJack());
    ASSERT_NE(panel, nullptr);
    auto& row = *panel->rowAt(0)->modRow();
    const bool undoBefore = f.undo.canUndo();

    row.beginAmountEdit();
    ASSERT_TRUE(row.isEditingAmount());
    row.amountEditor()->setText("-25", false);
    EXPECT_TRUE(row.amountEditor()->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));

    EXPECT_NEAR(f.amount(f.attenId), -0.25f, 1e-4f);
    ASSERT_TRUE(f.undo.undo());
    EXPECT_NEAR(f.amount(f.attenId), 0.5f, 1e-4f);
    EXPECT_EQ(f.undo.canUndo(), undoBefore);
}

TEST_F(ModuleComponentTest, ThePanelFollowsAnAmountChangedElsewhere) {
    NoMotion motion;
    ModPanelFixture f;
    auto* panel = f.open(f.destinationJack());
    ASSERT_NE(panel, nullptr);
    setAmount(f.engine.getGraph(), f.attenId, -0.75f);
    panel->sync();
    auto& row = *panel->rowAt(0)->modRow();
    EXPECT_NEAR(row.bar().amount(), -0.75f, 1e-4f);
    EXPECT_EQ(row.amountButton().getButtonText(), synth::ui::ModDotAmountBar::percentText(-0.75f));
}

TEST_F(ModuleComponentTest, RemovingAModulationRowStaysOneUndoStep) {
    NoMotion motion;
    ModPanelFixture f;
    auto* panel = f.open(f.destinationJack());
    ASSERT_NE(panel, nullptr);
    clickNow(panel->rowAt(0)->removeButton());
    EXPECT_EQ(panel->rowCount(), 0);
    EXPECT_EQ(f.modCable().id.attenUid, 0u) << "the modulation is gone";
    ASSERT_TRUE(f.undo.undo());
    EXPECT_NE(f.modCable().id.attenUid, 0u) << "one undo brings it back";
}

TEST_F(ModuleComponentTest, AnAudioRowKeepsItsSwatchNameAndTrash) {
    NoMotion motion;
    ModPanelFixture f;
    auto osc = f.engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    f.editor->updateComponents();
    f.editor->connectPorts(osc->nodeID, 0, f.vcaId, 0, false, false);
    f.refresh();
    auto* panel = f.open({f.vcaId, 0, true, false});
    ASSERT_NE(panel, nullptr);
    int audioRows = 0;
    for (int i = 0; i < panel->rowCount(); ++i) {
        auto* row = panel->rowAt(i);
        if (row->connection().isModulation())
            continue;
        ++audioRows;
        EXPECT_EQ(row->modRow(), nullptr) << "an audio cable has no amount or curve";
        EXPECT_EQ(row->getHeight(), PortConnectionRow::kHeight);
        EXPECT_TRUE(row->removeButton().isVisible());
    }
    EXPECT_GE(audioRows, 1);
}

// Sibling-gap guard: the mod-dot guard enumerates controls that carry a dot, so a SURFACE that lists
// modulations without the dot was invisible to it. Every surface that lists a modulation lists it with the same
// controls (show in timeline included). A new surface that lists modulations is one more entry below.
TEST_F(ModuleComponentTest, EverySurfaceThatListsAModulationOffersAmountPercentAndCurve) {
    NoMotion motion;
    struct Surface {
        const char* name;
        std::function<ModDotSourceRow*(ModPanelFixture&)> row;
    };
    const std::vector<Surface> surfaces = {
        {"the knob's mod dot panel (sources page)",
         [](ModPanelFixture& f) -> ModDotSourceRow* {
             f.editor->getModDot().openPopover(f.vcaId, f.gainChannel, *f.vcaCard);
             auto* popover = f.popover();
             return popover != nullptr ? popover->sourcesPage().rowAt(0) : nullptr;
         }},
        {"the port panel of the jack the cable lands on",
         [](ModPanelFixture& f) -> ModDotSourceRow* {
             auto* panel = f.open(f.destinationJack());
             return panel != nullptr && panel->rowAt(0) != nullptr ? panel->rowAt(0)->modRow() : nullptr;
         }},
        {"the port panel of the jack the cable leaves (an LFO output)",
         [](ModPanelFixture& f) -> ModDotSourceRow* {
             auto* panel = f.open(f.sourceJack());
             return panel != nullptr && panel->rowAt(0) != nullptr ? panel->rowAt(0)->modRow() : nullptr;
         }},
    };
    for (const auto& surface : surfaces) {
        ModPanelFixture f;
        auto* row = surface.row(f);
        ASSERT_NE(row, nullptr) << surface.name << " lists the modulation without its controls";
        SCOPED_TRACE(surface.name);
        expectReachable(*row);
        EXPECT_NEAR(row->bar().amount(), 0.5f, 1e-4f);
    }
}

// ---- an output jack's mod rows: amount, curve and show in timeline on one line, audio rows plain beside them ----
// The rows are told apart by cable (an attenuverter chain or not), never by module type.

namespace {

// The curve box a chip opens, kept instead of shown (a real window needs a display).
struct CurveBoxLaunched {
    std::unique_ptr<synth::ui::ModCurveBox> box;
    CurveBoxLaunched() {
        synth::ui::test_hooks::modCurvePopoverLauncherForTest() = [this](std::unique_ptr<synth::ui::ModCurveBox> b,
                                                                         juce::Component&) { box = std::move(b); };
    }
    ~CurveBoxLaunched() {
        box.reset();
        synth::ui::test_hooks::modCurvePopoverLauncherForTest() = nullptr;
    }
};

// The LFO -> VCA Gain knob routing of the mod dot fixture, plus (a) a second VCA whose Gain knob the same LFO
// modulates, or (b) an Oscillator whose output feeds the VCA's audio input AND the second VCA's Gain knob.
struct RowsFixture : Fixture {
    std::unique_ptr<juce::Component> heldPanel;
    juce::AudioProcessorGraph::NodeID vca2Id, oscId, secondAttenId;

    enum class Shape { LfoOnTwoKnobs, OscAudioAndMod };

    explicit RowsFixture(Shape shape)
        : Fixture(/*modulated*/ true, /*gainAsKnob*/ true) {
        auto& graph = engine.getGraph();
        auto vca2 = graph.addNode(std::make_unique<VCAModule>());
        vca2Id = vca2->nodeID;
        showGainAsKnob(graph, vca2Id);
        if (shape == Shape::LfoOnTwoKnobs) {
            secondAttenId = engine.addModRouting(lfoId, 0, vca2Id, gainChannel);
        } else {
            auto osc = graph.addNode(std::make_unique<OscillatorModule>());
            oscId = osc->nodeID;
            editor->updateComponents();
            editor->connectPorts(oscId, 0, vcaId, 0, false, false);
            secondAttenId = engine.addModRouting(oscId, 0, vca2Id, gainChannel);
        }
        setAmount(graph, secondAttenId, -0.25f);
        engine.updateModuleNames();
        refresh();
        editor->getPortPanel().panelLauncher = [this](std::unique_ptr<juce::Component> content, juce::Component&,
                                                      juce::Rectangle<int>) { heldPanel = std::move(content); };
    }
    ~RowsFixture() { heldPanel.reset(); }

    ModuleBase* module(juce::AudioProcessorGraph::NodeID id) {
        auto* node = engine.getGraph().getNodeForId(id);
        return node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    }
    PortConnectionsPanel* openOutput(juce::AudioProcessorGraph::NodeID id) {
        const PortRef port{id, module(id)->mapOutputChannel(0).visibleJackIndex, false, false};
        editor->getPortPanel().open(*editor, juce::Rectangle<int>(0, 0, 10, 10), port);
        return editor->getPortPanel().getPanel();
    }
};

juce::MouseEvent at(juce::Component& c, float x) { return makeModuleClickWithMods(c, {(int)x, 5}, kPlain); }

// A mod row's controls, all on screen inside the row, enabled and taking the keyboard.
void expectModControls(PortConnectionRow& row) {
    auto* mod = row.modRow();
    ASSERT_NE(mod, nullptr) << row.connection().label << " is a modulation: it gets the mod controls";
    for (juce::Component* c : std::initializer_list<juce::Component*>{
             &mod->bar(), &mod->amountButton(), &mod->curveChip(), &mod->timelineButton(), &mod->removeButton()}) {
        SCOPED_TRACE(c->getTitle().toStdString());
        EXPECT_TRUE(c->isVisible());
        EXPECT_TRUE(c->isEnabled());
        EXPECT_TRUE(c->getWantsKeyboardFocus());
        EXPECT_GT(c->getWidth(), 0);
        EXPECT_TRUE(row.getLocalBounds().contains(c->getBounds() + mod->getPosition()));
    }
    EXPECT_EQ(mod->timelineButton().getTooltip(), "Show " + mod->source().sourceName + " in timeline")
        << "the same title and tooltip as the knob's panel";
    EXPECT_EQ(row.getHeight(), PortConnectionRow::kHeight) << "a mod row is one line";
    EXPECT_GE(mod->bar().getWidth(), ModDotSourceRow::kMinBar) << "the amount bar keeps a usable width";
}

} // namespace

TEST_F(ModuleComponentTest, AnLfoOutputListsEachModulationWithAmountCurveAndShowInTimeline) {
    NoMotion motion;
    RowsFixture f(RowsFixture::Shape::LfoOnTwoKnobs);
    auto* panel = f.openOutput(f.lfoId);
    ASSERT_NE(panel, nullptr);
    ASSERT_EQ(panel->rowCount(), 2);
    for (int i = 0; i < 2; ++i) {
        SCOPED_TRACE(i);
        expectModControls(*panel->rowAt(i));
    }
}

// Tab reads a mod row like a person does, left to right: fader, percent, curve, timeline, remove, on the port panel's
// row and the knob's alike. (Sorting by top edge visited the tall timeline button first.)
TEST_F(ModuleComponentTest, TabWalksAModRowInReadingOrderOnBothPanels) {
    NoMotion motion;
    const auto stopsOf = [](ModDotSourceRow& row) {
        std::vector<juce::Component*> stops;
        for (auto* c : row.createFocusTraverser()->getAllComponents(&row))
            if (c->getWantsKeyboardFocus() && c->getParentComponent() == &row)
                stops.push_back(c);
        return stops;
    };
    RowsFixture f(RowsFixture::Shape::LfoOnTwoKnobs);
    auto* panel = f.openOutput(f.lfoId);
    ASSERT_NE(panel, nullptr);
    auto* portRow = panel->rowAt(0)->modRow();
    ASSERT_NE(portRow, nullptr);
    const std::vector<juce::Component*> portOrder = {&portRow->bar(), &portRow->amountButton(), &portRow->curveChip(),
                                                     &portRow->timelineButton(), &portRow->removeButton()};
    EXPECT_EQ(stopsOf(*portRow), portOrder);

    ModPanelFixture k;
    k.editor->getModDot().openPopover(k.vcaId, k.gainChannel, *k.vcaCard);
    ASSERT_NE(k.popover(), nullptr);
    auto* oneLine = k.popover()->sourcesPage().rowAt(0);
    ASSERT_NE(oneLine, nullptr);
    std::vector<juce::Component*> oneLineOrder = {&oneLine->bar(), &oneLine->amountButton(), &oneLine->curveChip()};
    if (oneLine->timelineButton().isVisible())
        oneLineOrder.push_back(&oneLine->timelineButton());
    oneLineOrder.push_back(&oneLine->removeButton());
    EXPECT_EQ(stopsOf(*oneLine), oneLineOrder);
}

TEST_F(ModuleComponentTest, AnOutputUsedAsAudioAndAsModSourceListsPlainAudioRowsAndAModRow) {
    NoMotion motion;
    RowsFixture f(RowsFixture::Shape::OscAudioAndMod);
    auto* panel = f.openOutput(f.oscId);
    ASSERT_NE(panel, nullptr);
    ASSERT_EQ(panel->rowCount(), 2);
    int audioRows = 0, modRows = 0;
    for (int i = 0; i < panel->rowCount(); ++i) {
        auto& row = *panel->rowAt(i);
        SCOPED_TRACE(row.connection().label.toStdString());
        if (row.connection().isModulation()) {
            ++modRows;
            expectModControls(row);
            EXPECT_EQ(row.modRow()->attenuverterId(), f.secondAttenId);
        } else {
            ++audioRows;
            EXPECT_EQ(row.modRow(), nullptr) << "an audio cable has no amount, curve or timeline";
            EXPECT_EQ(row.getHeight(), PortConnectionRow::kHeight) << "and keeps the one-line row";
            EXPECT_TRUE(row.removeButton().isVisible());
        }
    }
    EXPECT_EQ(audioRows, 1);
    EXPECT_EQ(modRows, 1);
}

TEST_F(ModuleComponentTest, DraggingAnOutputRowsAmountBarIsOneUndoStep) {
    NoMotion motion;
    RowsFixture f(RowsFixture::Shape::LfoOnTwoKnobs);
    auto* panel = f.openOutput(f.lfoId);
    ASSERT_NE(panel, nullptr);
    auto* row = panel->rowAt(1)->modRow();
    ASSERT_NE(row, nullptr);
    const auto atten = row->attenuverterId();
    const float before = f.amount(atten);
    const bool undoBefore = f.undo.canUndo();
    auto& bar = row->bar();
    bar.mouseDown(at(bar, bar.xForValue(before)));
    bar.mouseDrag(at(bar, bar.xForValue(0.4f)));
    bar.mouseDrag(at(bar, bar.xForValue(0.8f)));
    bar.mouseUp(at(bar, bar.xForValue(0.8f)));
    EXPECT_NEAR(f.amount(atten), 0.8f, 0.02f);

    ASSERT_TRUE(f.undo.undo());
    EXPECT_NEAR(f.amount(atten), before, 1e-4f) << "one undo undoes the whole drag";
    EXPECT_EQ(f.undo.canUndo(), undoBefore) << "and nothing else was recorded";
}

TEST_F(ModuleComponentTest, AnOutputRowsCurveChipOpensTheCurveEditor) {
    NoMotion motion;
    RowsFixture f(RowsFixture::Shape::OscAudioAndMod);
    CurveBoxLaunched launched;
    auto* panel = f.openOutput(f.oscId);
    ASSERT_NE(panel, nullptr);
    PortConnectionRow* modRow = nullptr;
    for (int i = 0; i < panel->rowCount(); ++i)
        if (panel->rowAt(i)->modRow() != nullptr)
            modRow = panel->rowAt(i);
    ASSERT_NE(modRow, nullptr);
    clickNow(modRow->modRow()->curveChip());
    ASSERT_NE(launched.box, nullptr) << "the chip opens the curve editor";
    EXPECT_NE(modRow->modRow()->curveChip().target(), nullptr) << "on this cable's curve";
}

// Show in timeline reveals exactly what the knob's own panel reveals for the same routing, for each knob the jack
// drives.
TEST_F(ModuleComponentTest, AnOutputRowsShowInTimelineRevealsTheSameLaneAsTheModDotPanel) {
    NoMotion motion;
    RowsFixture f(RowsFixture::Shape::LfoOnTwoKnobs);
    std::vector<synth::ui::ModulatorInfo> revealed;
    f.editor->getModDot().host.revealModulator = [&](const synth::ui::ModulatorInfo& m) { revealed.push_back(m); };
    auto* panel = f.openOutput(f.lfoId);
    ASSERT_NE(panel, nullptr);
    std::map<juce::uint32, juce::String> fromPortPanel; // attenuverter -> what its row revealed
    for (int i = 0; i < panel->rowCount(); ++i) {
        auto* mod = panel->rowAt(i)->modRow();
        ASSERT_NE(mod, nullptr);
        revealed.clear();
        clickNow(mod->timelineButton());
        ASSERT_EQ(revealed.size(), 1u) << "the port row's button reveals the lane";
        fromPortPanel[mod->attenuverterId().uid] = revealed[0].key();
    }
    ASSERT_EQ(fromPortPanel.size(), 2u);
    EXPECT_NE(fromPortPanel[f.attenId.uid], fromPortPanel[f.secondAttenId.uid]) << "each row reveals its own knob";

    for (const auto& [vca, atten] : {std::pair{f.vcaId, f.attenId}, std::pair{f.vca2Id, f.secondAttenId}}) {
        auto* card = findModuleComp(*f.editor, f.module(vca));
        ASSERT_NE(card, nullptr);
        f.editor->getModDot().openPopover(vca, f.gainChannel, *card);
        auto* popover = f.popover();
        ASSERT_NE(popover, nullptr);
        auto* row = popover->sourcesPage().rowFor(atten);
        ASSERT_NE(row, nullptr);
        revealed.clear();
        clickNow(row->timelineButton());
        ASSERT_EQ(revealed.size(), 1u);
        EXPECT_EQ(revealed[0].key(), fromPortPanel[atten.uid]) << "the same call as the mod dot panel's";
    }
}

// A removed mod row leaves from its own height through the shared row motion, and Cmd+Z grows it back to that height.
TEST_F(ModuleComponentTest, AModRowRemovedAndUndoneGrowsBackToItsOwnHeight) {
    NoMotion motion;
    RowsFixture f(RowsFixture::Shape::OscAudioAndMod);
    auto* panel = f.openOutput(f.oscId);
    ASSERT_NE(panel, nullptr);
    panel->setForceAnimateForTest(true);
    PortConnectionRow* modRow = nullptr;
    for (int i = 0; i < panel->rowCount(); ++i)
        if (panel->rowAt(i)->modRow() != nullptr)
            modRow = panel->rowAt(i);
    ASSERT_NE(modRow, nullptr);
    const auto id = modRow->cableId();
    const int full = modRow->getHeight();
    ASSERT_EQ(full, PortConnectionRow::kHeight);

    clickNow(modRow->removeButton());
    EXPECT_EQ(panel->slotHeightFor(id), full) << "the leaving row starts from its own height";
    panel->applyTimelineAtMs(panel->timeline().totalMs());

    ASSERT_TRUE(f.undo.undo());
    f.refresh();
    panel->sync();
    ASSERT_NE(panel->rowFor(id), nullptr) << "Cmd+Z brings the row back";
    EXPECT_EQ(panel->slotHeightFor(id), 0) << "it grows from nothing";
    panel->applyTimelineAtMs(panel->timeline().totalMs());
    EXPECT_EQ(panel->slotHeightFor(id), full) << "and lands at its own height";
}

// ---- one line per row, long names stay short, the full name is on hover ----

namespace {

constexpr const char* kLongTitle = "Rhythm Section Drum Kit With A Very Long Custom Name";

// One LFO driving the Gain knob of `count` VCAs; the first is called `firstTitle`, the rest are "VCA n".
struct ManyTargets : RowsFixture {
    explicit ManyTargets(int count, const juce::String& firstTitle = {})
        : RowsFixture(Shape::LfoOnTwoKnobs) {
        auto& graph = engine.getGraph();
        for (int i = 2; i < count; ++i) {
            auto vca = graph.addNode(std::make_unique<VCAModule>());
            showGainAsKnob(graph, vca->nodeID);
            engine.addModRouting(lfoId, 0, vca->nodeID, gainChannel);
        }
        if (firstTitle.isNotEmpty())
            graph.getNodeForId(vcaId)->properties.set("displayName", firstTitle);
        engine.updateModuleNames();
        refresh();
    }
};

int textWidth(const juce::String& text) {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(juce::Font(juce::FontOptions(12.5f)), text, 0.0f, 0.0f);
    return (int)std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth());
}

// Every control of a one-line mod row is inside the row, none touches another, and the fader keeps its minimum.
void expectOneLineRowFits(PortConnectionRow& row, const juce::String& what) {
    SCOPED_TRACE(what.toStdString());
    auto* mod = row.modRow();
    ASSERT_NE(mod, nullptr);
    EXPECT_EQ(row.getHeight(), PortConnectionRow::kHeight);
    std::vector<juce::Component*> parts = {&mod->nameArea(),       &mod->bar(),
                                           &mod->amountButton(),   &mod->curveChip(),
                                           &mod->timelineButton(), &mod->removeButton()};
    std::sort(parts.begin(), parts.end(), [](auto* a, auto* b) { return a->getX() < b->getX(); });
    for (size_t i = 0; i < parts.size(); ++i) {
        SCOPED_TRACE(parts[i]->getTitle().toStdString());
        EXPECT_TRUE(parts[i]->isVisible());
        EXPECT_TRUE(mod->getLocalBounds().contains(parts[i]->getBounds())) << "nothing is clipped";
        if (i > 0)
            EXPECT_GT(parts[i]->getX(), parts[i - 1]->getRight()) << "no two controls touch";
    }
    EXPECT_GE(mod->bar().getWidth(), ModDotSourceRow::kMinBar);
}

} // namespace

TEST_F(ModuleComponentTest, ALongTargetNameIsElidedInTheRowAndFullInTheTooltipAndTitle) {
    NoMotion motion;
    ManyTargets f(2, kLongTitle);
    auto* panel = f.openOutput(f.lfoId);
    ASSERT_NE(panel, nullptr);
    PortConnectionRow* row = nullptr;
    for (int i = 0; i < panel->rowCount(); ++i)
        if (panel->rowAt(i)->connection().fullLabel.startsWith(kLongTitle))
            row = panel->rowAt(i);
    ASSERT_NE(row, nullptr);
    const auto full = row->connection().fullLabel;
    EXPECT_EQ(row->connection().label, full) << "another card's name keeps its module: the panel is on the LFO";
    EXPECT_GT(textWidth(full), row->modRow()->nameBounds().getWidth()) << "too long: it ends in '...'";
    EXPECT_EQ(row->modRow()->shownName(), full.fromFirstOccurrenceOf(synth::ui::portSeparator(), false, false))
        << "too long: the row gives up the module and keeps the port";
    auto& name = row->modRow()->nameArea();
    EXPECT_EQ(dynamic_cast<juce::SettableTooltipClient&>(name).getTooltip(), full);
    EXPECT_EQ(name.getTitle(), full);
    EXPECT_TRUE(row->getTitle().contains(full));
    EXPECT_TRUE(row->modRow()->removeButton().getTitle().contains(full));
    EXPECT_EQ(row->modRow()->getHeight(), PortConnectionRow::kHeight);
}

TEST_F(ModuleComponentTest, TheKnobsModDotPanelShowsTheFullNameOnHoverToo) {
    NoMotion motion;
    ManyTargets f(2);
    f.engine.getGraph().getNodeForId(f.lfoId)->properties.set("displayName", kLongTitle);
    f.engine.updateModuleNames();
    f.refresh();
    f.editor->getModDot().openPopover(f.vcaId, f.gainChannel, *findModuleComp(*f.editor, f.module(f.vcaId)));
    ASSERT_NE(f.popover(), nullptr);
    auto* row = f.popover()->sourcesPage().rowAt(0);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(dynamic_cast<juce::SettableTooltipClient&>(row->nameArea()).getTooltip(), kLongTitle);
    EXPECT_GT(textWidth(kLongTitle), row->nameBounds().getWidth());
    EXPECT_GE(row->bar().getWidth(), ModDotSourceRow::kMinBar);
    EXPECT_LE(row->nameBounds().getWidth(), ModDotSourceRow::kNameMax);
    EXPECT_GT(row->nameBounds().getWidth(), 56) << "the knob's panel lays a row out as the jack's panel does";
}

// The knob's panel in every theme with the longest name: the same one-line fit as the jack's panel.
TEST_F(ModuleComponentTest, TheKnobsModDotPanelFitsTheLongestNameInEveryTheme) {
    NoMotion motion;
    ManyTargets f(2);
    f.engine.getGraph().getNodeForId(f.lfoId)->properties.set("displayName", kLongTitle);
    f.engine.updateModuleNames();
    f.refresh();
    f.editor->getModDot().openPopover(f.vcaId, f.gainChannel, *findModuleComp(*f.editor, f.module(f.vcaId)));
    ASSERT_NE(f.popover(), nullptr);
    auto* row = f.popover()->sourcesPage().rowAt(0);
    ASSERT_NE(row, nullptr);
    for (const auto& theme : synth::theme::builtInThemes()) {
        SCOPED_TRACE(theme.name.toStdString());
        synth::theme::AppLookAndFeel lnf;
        lnf.applyTheme(theme);
        row->setLookAndFeel(&lnf);
        row->resized();
        std::vector<juce::Component*> parts = {&row->nameArea(),       &row->bar(),
                                               &row->amountButton(),   &row->curveChip(),
                                               &row->timelineButton(), &row->removeButton()};
        std::sort(parts.begin(), parts.end(), [](auto* a, auto* b) { return a->getX() < b->getX(); });
        for (size_t i = 0; i < parts.size(); ++i) {
            SCOPED_TRACE(parts[i]->getTitle().toStdString());
            EXPECT_TRUE(row->getLocalBounds().contains(parts[i]->getBounds())) << "nothing is clipped";
            if (i > 0)
                EXPECT_GT(parts[i]->getX(), parts[i - 1]->getRight()) << "no two controls touch";
        }
        EXPECT_GE(row->bar().getWidth(), ModDotSourceRow::kMinBar);
        row->setLookAndFeel(nullptr);
    }
}

TEST_F(ModuleComponentTest, AJacksOwnCardIsLeftOffItsRowsAndKeptInTheTooltip) {
    NoMotion motion;
    RowsFixture f(RowsFixture::Shape::LfoOnTwoKnobs);
    // The LFO modulating one of its own knobs: the far end is the panel's own card.
    const auto targets = f.module(f.lfoId)->getModulationTargets();
    ASSERT_FALSE(targets.empty());
    ASSERT_NE(f.engine.addModRouting(f.lfoId, 0, f.lfoId, targets.front().channelIndex).uid, 0u);
    f.refresh();
    auto* panel = f.openOutput(f.lfoId);
    ASSERT_NE(panel, nullptr);
    bool seenOwn = false;
    for (int i = 0; i < panel->rowCount(); ++i) {
        const auto& c = panel->rowAt(i)->connection();
        const bool own = c.far.node == f.lfoId;
        seenOwn |= own;
        EXPECT_EQ(c.label == c.fullLabel, !own) << c.fullLabel.toStdString();
        if (own) {
            EXPECT_FALSE(c.label.contains(f.editor->getModuleTitle(f.lfoId, f.module(f.lfoId))))
                << c.label.toStdString();
            EXPECT_TRUE(c.fullLabel.startsWith(f.editor->getModuleTitle(f.lfoId, f.module(f.lfoId))));
        }
    }
    EXPECT_TRUE(seenOwn);
}

// 1, 8 and 32 targets: every row is one line, the panel is exactly the rows' height and a cap scrolls the rest.
TEST_F(ModuleComponentTest, ThePortPanelsHeightIsOneLinePerRowAndScrollsUnderTheCap) {
    NoMotion motion;
    for (const int count : {1, 8, 32}) {
        SCOPED_TRACE(count);
        ManyTargets f(count < 2 ? 2 : count, kLongTitle);
        auto* panel = f.openOutput(f.lfoId);
        ASSERT_NE(panel, nullptr);
        if (count == 1) {
            clickNow(panel->rowAt(0)->removeButton());
            panel->applyTimelineAtMs(panel->timeline().totalMs());
        }
        const int rows = panel->rowCount();
        EXPECT_EQ(rows, count);
        const int chrome = PortConnectionsPanel::kTitleHeight + PortConnectionsPanel::kSplitRowHeight +
                           PortConnectionsPanel::kBottomPad;
        EXPECT_EQ(panel->getHeight(), chrome + rows * PortConnectionRow::kHeight) << "one line per row, no more";
        for (int i = 0; i < rows; ++i)
            EXPECT_EQ(panel->rowAt(i)->getHeight(), PortConnectionRow::kHeight) << "row " << i;
        panel->setMaxHeight(300);
        EXPECT_LE(panel->getHeight(), 300) << "a cap holds however many rows";
        if (rows * PortConnectionRow::kHeight > 300 - chrome)
            EXPECT_EQ(panel->getHeight(), 300) << "and the rows scroll under it";
    }
}

// The widest name in every built-in theme, with and without the scroll bar: nothing overlaps, nothing is clipped, the
// fader keeps its minimum (the guard for the layout-overlap kind on these rows).
TEST_F(ModuleComponentTest, OneLineRowsFitInEveryThemeWithTheLongestName) {
    NoMotion motion;
    ManyTargets f(32, kLongTitle);
    auto* panel = f.openOutput(f.lfoId);
    ASSERT_NE(panel, nullptr);
    ASSERT_GT(panel->rowCount(), 20);
    for (const auto& theme : synth::theme::builtInThemes()) {
        synth::theme::AppLookAndFeel lnf;
        lnf.applyTheme(theme);
        panel->setLookAndFeel(&lnf);
        for (const int cap : {0, 300}) { // 0: no scroll bar (all rows), 300: the scroll bar takes 10 px
            panel->setMaxHeight(cap);
            for (int i = 0; i < panel->rowCount(); ++i) {
                panel->rowAt(i)->modRow()->resized();
                expectOneLineRowFits(*panel->rowAt(i), theme.name.toStdString() + " row " + std::to_string(i));
            }
        }
        panel->setLookAndFeel(nullptr);
    }
}
