// ControlStyleCardTests.cpp -- the cards of a real GraphEditor publish their control style (own, else the
// type's), the "Control Style..." panel writes both levels, and the card menu offers it.

#include "../CardBody/CardBodyTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "MacroSet.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardBody/ControlStyleNodeOverride.h"
#include "UI/Graph/ControlStyle/ControlStyleCards.h"
#include "UI/Graph/ControlStyle/ControlStyleMenu.h"
#include "UI/Graph/ControlStyle/ControlStylePanel.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/ControlStyleOverride.h"
#include <gtest/gtest.h>

using namespace cardbody_test;
using synth::theme::ControlStyleOverride;
using synth::theme::KnobStyle;
using Scope = synth::ui::ControlStylePanel::Scope;

namespace {

// The editor's user settings, in a throwaway file.
struct SettingsFile {
    juce::ApplicationProperties props;
    SettingsFile() {
        juce::PropertiesFile::Options options;
        options.applicationName = "ControlStyleCardTest-" + juce::Uuid().toString();
        options.filenameSuffix = "settings";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        props.setStorageParameters(options);
    }
    ~SettingsFile() {
        if (auto* settings = props.getUserSettings())
            settings->getFile().deleteFile();
    }
};

struct StyledCanvas {
    SettingsFile settings; // declared first: outlives the canvas
    CardCanvas canvas;

    StyledCanvas() { canvas.editor.getModDot().host.appProperties = &settings.props; }
    ~StyledCanvas() { canvas.editor.getModDot().host.appProperties = nullptr; }

    juce::PropertiesFile& user() { return *settings.props.getUserSettings(); }
    void refresh() { synth::refreshAllControlStyles(canvas.editor); }
};

std::optional<int> prop(ModuleComponent* card, const char* name) {
    const auto& value = card->getProperties()[name];
    return value.isVoid() ? std::nullopt : std::optional<int>((int)value);
}

std::optional<int> knobOf(ModuleComponent* card) { return prop(card, synth::theme::kKnobStyleProperty); }
std::optional<int> faderOf(ModuleComponent* card) { return prop(card, synth::theme::kFaderStyleProperty); }

juce::MouseEvent clickEvent(juce::Component& comp, juce::Point<int> pos) {
    return mouseAt(comp, pos.toFloat(), juce::ModifierKeys());
}

void click(juce::Component& comp) {
    const auto pos = comp.getLocalBounds().getCentre();
    comp.mouseEnter(clickEvent(comp, pos));
    comp.mouseDown(clickEvent(comp, pos));
    comp.mouseUp(clickEvent(comp, pos));
}

} // namespace

TEST(ControlStyleCards, ACardCarriesNoStylePropertyUntilSomethingChoosesOne) {
    StyledCanvas c;
    const auto id = c.canvas.add(std::make_unique<OscillatorModule>(), 0, 0);
    c.canvas.editor.updateComponents();
    EXPECT_FALSE(knobOf(c.canvas.card(id)));
    EXPECT_FALSE(faderOf(c.canvas.card(id)));
}

TEST(ControlStyleCards, ATypeStyleReachesEveryCardOfThatTypeAndNoOtherType) {
    StyledCanvas c;
    const auto osc1 = c.canvas.add(std::make_unique<OscillatorModule>(), 0, 0);
    const auto osc2 = c.canvas.add(std::make_unique<OscillatorModule>(), 400, 0);
    const auto filter = c.canvas.add(std::make_unique<FilterModule>(), 800, 0);
    c.canvas.editor.updateComponents();

    synth::theme::writeTypeControlStyle(c.user(), "Oscillator", {KnobStyle::Ring, std::nullopt});
    c.refresh();
    EXPECT_EQ(knobOf(c.canvas.card(osc1)), (int)KnobStyle::Ring);
    EXPECT_EQ(knobOf(c.canvas.card(osc2)), (int)KnobStyle::Ring);
    EXPECT_FALSE(faderOf(c.canvas.card(osc1))) << "only the kind that was chosen";
    EXPECT_FALSE(knobOf(c.canvas.card(filter)));
}

TEST(ControlStyleCards, AModulesOwnStyleBeatsTheTypeStyleAndFollowClearsEachLevel) {
    StyledCanvas c;
    const auto osc1 = c.canvas.add(std::make_unique<OscillatorModule>(), 0, 0);
    const auto osc2 = c.canvas.add(std::make_unique<OscillatorModule>(), 400, 0);
    c.canvas.editor.updateComponents();
    auto& graph = c.canvas.engine.getGraph();

    // The module first, then its whole type: the module keeps its own.
    synth::setControlStyleNodeOverride(graph, nullptr, osc1, {KnobStyle::Neon, std::nullopt});
    synth::theme::writeTypeControlStyle(c.user(), "Oscillator", {KnobStyle::Ring, KnobStyle::Hardware});
    c.refresh();
    EXPECT_EQ(knobOf(c.canvas.card(osc1)), (int)KnobStyle::Neon);
    EXPECT_EQ(knobOf(c.canvas.card(osc2)), (int)KnobStyle::Ring);
    EXPECT_EQ(faderOf(c.canvas.card(osc1)), (int)KnobStyle::Hardware) << "each kind resolves on its own";

    synth::theme::writeTypeControlStyle(c.user(), "Oscillator", {});
    c.refresh();
    EXPECT_EQ(knobOf(c.canvas.card(osc1)), (int)KnobStyle::Neon);
    EXPECT_FALSE(knobOf(c.canvas.card(osc2)));
    EXPECT_FALSE(faderOf(c.canvas.card(osc1)));

    synth::setControlStyleNodeOverride(graph, nullptr, osc1, {});
    c.refresh();
    EXPECT_FALSE(knobOf(c.canvas.card(osc1)));
}

TEST(ControlStyleCards, ACardBuiltLaterAndAnUpdateComponentsPassPickUpTheStyle) {
    StyledCanvas c;
    const auto osc = c.canvas.add(std::make_unique<OscillatorModule>(), 0, 0);
    synth::theme::writeTypeControlStyle(c.user(), "Oscillator", {KnobStyle::Neon, std::nullopt});
    synth::setControlStyleNodeOverride(c.canvas.engine.getGraph(), nullptr, osc, {std::nullopt, KnobStyle::Ring});
    c.canvas.editor.updateComponents(); // the card is built here
    EXPECT_EQ(knobOf(c.canvas.card(osc)), (int)KnobStyle::Neon);
    EXPECT_EQ(faderOf(c.canvas.card(osc)), (int)KnobStyle::Ring);

    synth::setControlStyleNodeOverride(c.canvas.engine.getGraph(), nullptr, osc, {});
    c.canvas.editor.updateComponents(); // what an undo or redo restore runs
    EXPECT_FALSE(faderOf(c.canvas.card(osc)));
}

// Regression guard for the real-project bug kind: a style saved on a node inside a macro must reach the card
// that opening the project builds.
TEST(ControlStyleCards, AProjectWhoseMacroMemberCarriesAStyleOpensWithItOnTheCard) {
    StyledCanvas c;
    auto& graph = c.canvas.engine.getGraph();
    juce::AudioProcessorGraph source;
    auto sourceNode = source.addNode(std::make_unique<OscillatorModule>());
    sourceNode->properties.set("x", 400);
    sourceNode->properties.set("y", 300);
    sourceNode->properties.set("uuid", juce::Uuid().toDashedString());
    ASSERT_TRUE(
        synth::setControlStyleNodeOverride(source, nullptr, sourceNode->nodeID, {KnobStyle::Neon, KnobStyle::Ring}));
    const auto json = synth::AIStateMapper::graphToJSON(source);

    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/true));
    auto* node = graph.getNodes().getFirst().get();
    synth::Macro macro;
    macro.id = "m1";
    macro.name = "Macro";
    macro.bounds = {400, 300, 160, 60};
    macro.members.push_back(node->properties["uuid"].toString());
    c.canvas.editor.getMacros().add(macro);
    c.canvas.editor.updateComponents();

    auto* card = c.canvas.card(node->nodeID);
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(knobOf(card), (int)KnobStyle::Neon);
    EXPECT_EQ(faderOf(card), (int)KnobStyle::Ring);
}

TEST(ControlStyleMenu, EveryCardWithKnobsOffersControlStyleAfterEditLayout) {
    StyledCanvas c;
    const auto osc = c.canvas.add(std::make_unique<OscillatorModule>(), 0, 0);
    const auto eq = c.canvas.add(synth::AIStateMapper::createModule("Parametric EQ"), 400, 0);
    c.canvas.editor.updateComponents();

    for (const auto id : {osc, eq}) {
        const auto menu = c.canvas.card(id)->buildModuleContextMenu();
        EXPECT_NE(menuItem(menu, "Control Style..."), nullptr) << (int)id.uid;
    }

    // Right after Edit Layout... on a card drawn from a layout.
    const auto menu = c.canvas.card(osc)->buildModuleContextMenu();
    juce::StringArray texts;
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next())
        texts.add(it.getItem().text);
    EXPECT_EQ(texts.indexOf("Control Style..."), texts.indexOf("Edit Layout...") + 1);
}

TEST(ControlStyleMenu, ChoosingTheItemOpensThePanelForThatCardsNodeAndType) {
    StyledCanvas c;
    const auto osc = c.canvas.add(std::make_unique<OscillatorModule>(), 0, 0);
    c.canvas.editor.updateComponents();
    std::unique_ptr<juce::Component> opened;
    synth::setControlStylePanelLauncherForTest([&](std::unique_ptr<juce::Component> p) { opened = std::move(p); });

    const auto menu = c.canvas.card(osc)->buildModuleContextMenu();
    const auto* item = menuItem(menu, "Control Style...");
    ASSERT_NE(item, nullptr);
    item->action();
    synth::setControlStylePanelLauncherForTest(nullptr);

    auto* panel = dynamic_cast<synth::ui::ControlStylePanel*>(opened.get());
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->getScopeButtonForTest(Scope::Type).getButtonText(), "Every Oscillator");
}

namespace {
struct PanelRig : StyledCanvas {
    NodeID osc1, osc2;
    std::unique_ptr<synth::ui::ControlStylePanel> panel;

    PanelRig() {
        osc1 = canvas.add(std::make_unique<OscillatorModule>(), 0, 0);
        osc2 = canvas.add(std::make_unique<OscillatorModule>(), 400, 0);
        canvas.editor.updateComponents();
        panel = std::make_unique<synth::ui::ControlStylePanel>(canvas.editor, osc1, "Oscillator");
        panel->getPickerForTest().setTheme(synth::theme::makeObsidian());
    }
    ControlStyleOverride own() { return synth::getControlStyleNodeOverride(canvas.engine.getGraph(), osc1); }
    synth::ui::KnobStylePicker& picker() { return panel->getPickerForTest(); }
};
} // namespace

TEST(ControlStylePanel, ThisModuleScopeWritesTheNodeAsOneUndoStepThatUndoClears) {
    PanelRig rig;
    click(rig.picker().getStyleButtonForTest((int)KnobStyle::Neon));
    EXPECT_EQ(rig.own(), (ControlStyleOverride{KnobStyle::Neon, std::nullopt}));
    EXPECT_EQ(knobOf(rig.canvas.card(rig.osc1)), (int)KnobStyle::Neon) << "applied live";
    EXPECT_FALSE(knobOf(rig.canvas.card(rig.osc2)));

    ASSERT_TRUE(rig.canvas.undo.canUndo());
    ASSERT_TRUE(rig.canvas.undo.undo());
    EXPECT_TRUE(rig.own().isEmpty());
    EXPECT_FALSE(knobOf(rig.canvas.card(rig.osc1))) << "undo repaints the card back";
    EXPECT_FALSE(rig.canvas.undo.canUndo()) << "one click, one step";
    ASSERT_TRUE(rig.canvas.undo.redo());
    EXPECT_EQ(knobOf(rig.canvas.card(rig.osc1)), (int)KnobStyle::Neon);
}

TEST(ControlStylePanel, EveryTypeScopeWritesTheSettingAndTheOtherCardsFollow) {
    PanelRig rig;
    click(rig.picker().getStyleButtonForTest((int)KnobStyle::Neon)); // this module: Neon
    rig.panel->getScopeButtonForTest(Scope::Type).setToggleState(true, juce::dontSendNotification);
    rig.panel->setScope(Scope::Type);
    EXPECT_TRUE(rig.picker().getFollowButtonForTest(false).getToggleState()) << "the type has no style of its own";

    click(rig.picker().getStyleButtonForTest((int)KnobStyle::Ring));
    EXPECT_EQ(synth::theme::loadTypeControlStyle(rig.user(), "Oscillator").knob, KnobStyle::Ring);
    EXPECT_EQ(knobOf(rig.canvas.card(rig.osc1)), (int)KnobStyle::Neon) << "the first stays Neon";
    EXPECT_EQ(knobOf(rig.canvas.card(rig.osc2)), (int)KnobStyle::Ring);

    click(rig.picker().getFaderStyleButtonForTest((int)KnobStyle::Hardware));
    EXPECT_EQ(synth::theme::loadTypeControlStyle(rig.user(), "Oscillator"),
              (ControlStyleOverride{KnobStyle::Ring, KnobStyle::Hardware}));

    click(rig.picker().getFollowButtonForTest(false));
    click(rig.picker().getFollowButtonForTest(true));
    EXPECT_TRUE(synth::theme::loadTypeControlStyle(rig.user(), "Oscillator").isEmpty());
    EXPECT_FALSE(knobOf(rig.canvas.card(rig.osc2)));

    rig.panel->setScope(Scope::Module);
    EXPECT_TRUE(rig.picker().getStyleButtonForTest((int)KnobStyle::Neon).getToggleState())
        << "switching scope re-reads that scope";
}

TEST(ControlStylePanel, FollowClearsOnlyItsRowAndItsLevel) {
    PanelRig rig;
    click(rig.picker().getStyleButtonForTest((int)KnobStyle::Neon));
    click(rig.picker().getFaderStyleButtonForTest((int)KnobStyle::Ring));
    EXPECT_EQ(rig.own(), (ControlStyleOverride{KnobStyle::Neon, KnobStyle::Ring}));
    click(rig.picker().getFollowButtonForTest(false));
    EXPECT_EQ(rig.own(), (ControlStyleOverride{std::nullopt, KnobStyle::Ring}));
    click(rig.picker().getFollowButtonForTest(true));
    EXPECT_TRUE(rig.own().isEmpty());
    EXPECT_TRUE(rig.canvas.engine.getGraph().getNodeForId(rig.osc1)->properties["controlStyle"].isVoid());
}

TEST(ControlStylePanel, EveryControlIsNamedHasATooltipAndTabsInVisualOrder) {
    PanelRig rig;
    std::vector<juce::Button*> expected{&rig.panel->getScopeButtonForTest(Scope::Module),
                                        &rig.panel->getScopeButtonForTest(Scope::Type),
                                        &rig.picker().getFollowButtonForTest(false)};
    for (int i = 0; i < synth::theme::kKnobStyleCount; ++i)
        expected.push_back(&rig.picker().getStyleButtonForTest(i));
    expected.push_back(&rig.picker().getFollowButtonForTest(true));
    for (int i = 0; i < synth::theme::kKnobStyleCount; ++i)
        expected.push_back(&rig.picker().getFaderStyleButtonForTest(i));

    for (auto* button : expected) {
        EXPECT_TRUE(button->getTitle().isNotEmpty());
        EXPECT_TRUE(button->getTooltip().isNotEmpty()) << button->getTitle();
    }
    EXPECT_EQ(rig.picker().getStyleButtonForTest((int)KnobStyle::Neon).getTitle(), "Neon knob style for this module");
    EXPECT_EQ(rig.picker().getFollowButtonForTest(false).getTooltip(),
              "Use the style set for every Oscillator, else Settings > Appearance > Controls");

    rig.panel->setScope(Scope::Type);
    EXPECT_EQ(rig.picker().getStyleButtonForTest((int)KnobStyle::Neon).getTitle(),
              "Neon knob style for every Oscillator");
    EXPECT_EQ(rig.picker().getFollowButtonForTest(false).getTooltip(),
              "Use the style from Settings > Appearance > Controls");

    juce::KeyboardFocusTraverser traverser;
    const auto order = traverser.getAllComponents(rig.panel.get());
    std::vector<juce::Component*> got(order.begin(), order.end());
    ASSERT_EQ(got.size(), expected.size());
    for (size_t i = 0; i < got.size(); ++i)
        EXPECT_EQ(got[i], expected[i]) << "Tab stop " << i;
}
