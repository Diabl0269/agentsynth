// The port connections panel shows a modulation cable the way the knob's sources panel does: the amount bar, the typed
// percent and the curve chip, from the jack the cable lands on and from the jack it leaves (an LFO's output). A drag
// of the amount is one undo step; an audio row has none of it. docs/layout/cables.md#port-connections-panel.

#include "../ModDot/ModDotTestFixture.h"

#include "Modules/OscillatorModule.h"
#include "UI/Graph/ModDot/ModDotSourceRow.h"
#include "UI/Graph/ModDot/ModDotSourcesPage.h"
#include "UI/Graph/PortPanel/PortConnectionList.h"
#include "UI/Graph/PortPanel/PortConnectionsPanel.h"
#include "UI/Graph/PortPanel/PortPanelController.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/ModuleViews/ModCurve/ModCurveChip.h"

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
                                                                      &row.removeButton()}) {
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
    EXPECT_EQ(row->getHeight(), PortConnectionRow::kHeight) << "rows keep the panel's height";
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
// controls. A new surface that lists modulations is one more entry below.
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
