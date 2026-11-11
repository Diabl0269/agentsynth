// The places that open a curve (dot-row chip, matrix chip, cable) all reach the one attenuverter curve, and the cable
// menu and cable chips behave by cable kind and by the preference.

#include "../../Graph/ModuleComponent/ModuleComponentTestFixture.h"
#include "../ShapeEditor/ShapeEditorTestHelpers.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "ModCurveTestRig.h"
#include "Modules/ADSRModule.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/LFOModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/GraphEditor/GraphEditorCableCurveChips.h"
#include "UI/Graph/GraphEditor/GraphEditorCableMenu.h"
#include "UI/Graph/ModMatrixCurveColumn.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/ModuleViews/ModCurve/ModCurveWindow.h"
#include "UI/ModuleViews/ShapeEditor/LfoShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include "UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h"
#include <cmath>
#include <set>

using synth::ui::CableMenuItem;
using Cable = graph_editor_types::VisibleCable;

namespace {
Cable cableOf(Cable::Kind kind, synth::ui::CableSignal signal, bool poly) {
    Cable c;
    c.kind = kind;
    c.signal = signal;
    c.isPolyBus = poly;
    return c;
}

struct Launched {
    std::unique_ptr<synth::ui::ModCurveBox> box;
    Launched() {
        synth::ui::test_hooks::modCurvePopoverLauncherForTest() = [this](std::unique_ptr<synth::ui::ModCurveBox> b,
                                                                         juce::Component&) { box = std::move(b); };
    }
    ~Launched() {
        box.reset();
        synth::ui::test_hooks::modCurvePopoverLauncherForTest() = nullptr;
    }
};
} // namespace

TEST(CableMenuItems, EditCurveIsOfferedByCableKind) {
    using K = Cable::Kind;
    using S = synth::ui::CableSignal;
    EXPECT_EQ(synth::ui::cableMenuItemsFor(cableOf(K::AttenuverterChain, S::ModCV, false)).size(), 2u);
    EXPECT_EQ(synth::ui::cableMenuItemsFor(cableOf(K::ModRouting, S::ModCV, false)).size(), 2u);
    EXPECT_EQ(synth::ui::cableMenuItemsFor(cableOf(K::ModRouting, S::PolyBus, true)).size(), 1u);
    EXPECT_EQ(synth::ui::cableMenuItemsFor(cableOf(K::Direct, S::Audio, false)).size(), 1u);
    EXPECT_EQ(synth::ui::cableMenuItemsFor(cableOf(K::Direct, S::Midi, false)).size(), 1u);
}

TEST_F(ModuleComponentTest, DotRowChipAndMatrixChipEditTheSameCurve) {
    Fixture f;
    Launched launched;
    juce::Component anchor;
    synth::ui::ModCurveChip dotChip, matrixChip;
    synth::ui::wireModCurveChip(dotChip, *f.editor, f.attenId, "LFO");
    synth::ui::mod_matrix_curve::wireChip(matrixChip, f.editor.get(), f.attenId);
    synth::ui::mod_matrix_curve::nameChip(matrixChip, 2);
    EXPECT_EQ(matrixChip.getTitle(), "Routing 3 curve");
    EXPECT_FALSE(dotChip.showsBentCurve());

    clickNow(dotChip);
    ASSERT_NE(launched.box, nullptr) << "the dot row chip opens the box";
    synth::ui::ModCurveEditTarget(*f.editor, f.attenId).setOnce(synth::ModCurve::preset(synth::ModCurve::Preset::Log));
    dotChip.refresh();
    matrixChip.refresh();
    EXPECT_TRUE(dotChip.showsBentCurve());
    EXPECT_TRUE(matrixChip.showsBentCurve()) << "both chips read the one attenuverter curve";
    EXPECT_EQ(synth::ui::ModCurveEditTarget(*f.editor, f.attenId).read().value(),
              synth::ModCurve::preset(synth::ModCurve::Preset::Log));
}

TEST_F(ModuleComponentTest, DirectCableSpliceKeepsChannelsAtAmountOneLinearAndUndoesInOneStep) {
    Fixture f(false);
    auto& graph = f.engine.getGraph();
    const juce::AudioProcessorGraph::Connection direct{{f.lfoId, 0}, {f.vcaId, f.gainChannel}};
    ASSERT_TRUE(graph.addConnection(direct));
    const int nodesBefore = graph.getNumNodes();

    Cable cable = cableOf(Cable::Kind::ModRouting, synth::ui::CableSignal::ModCV, false);
    cable.id = {f.lfoId.uid, 0, f.vcaId.uid, f.gainChannel, 0};
    const auto atten = synth::ui::spliceAttenuverterIntoCable(*f.editor, cable);
    ASSERT_NE(atten.uid, 0u);
    EXPECT_FALSE(graph.isConnected(direct));
    EXPECT_TRUE(graph.isConnected({{f.lfoId, 0}, {atten, 0}}));
    EXPECT_TRUE(graph.isConnected({{atten, 0}, {f.vcaId, f.gainChannel}}));
    auto* module = dynamic_cast<AttenuverterModule*>(graph.getNodeForId(atten)->getProcessor());
    ASSERT_NE(module, nullptr);
    EXPECT_TRUE(module->getCurve().isLinear());
    auto* amount = dynamic_cast<juce::AudioParameterFloat*>(module->getParameters()[1]);
    ASSERT_NE(amount, nullptr);
    EXPECT_NEAR(amount->get(), 1.0f, 1e-4f);

    f.undo.undo();
    EXPECT_TRUE(graph.isConnected(direct));
    EXPECT_EQ(graph.getNumNodes(), nodesBefore);
}

TEST_F(ModuleComponentTest, CableChipHasNoHitAreaWhilePreferenceIsOffAndOpensBoxWhenOn) {
    Fixture f;
    Launched launched;
    f.refresh();
    const auto cables = f.editor->buildVisibleCables();
    const Cable* chain = nullptr;
    for (const auto& c : cables)
        if (c.kind == Cable::Kind::AttenuverterChain)
            chain = &c;
    ASSERT_NE(chain, nullptr);
    const auto centre = synth::ui::cable_curve_chips::chipRect(*chain).getCentre();

    f.editor->showCurvesOnCables = false;
    EXPECT_EQ(synth::ui::cable_curve_chips::chipAt(*f.editor, centre).uid, 0u);
    EXPECT_FALSE(synth::ui::cable_curve_chips::press(*f.editor, *f.editor, centre));
    EXPECT_EQ(launched.box, nullptr);

    f.editor->showCurvesOnCables = true;
    EXPECT_EQ(synth::ui::cable_curve_chips::chipAt(*f.editor, centre).uid, f.attenId.uid);
    EXPECT_TRUE(synth::ui::cable_curve_chips::press(*f.editor, *f.editor, centre));
    EXPECT_NE(launched.box, nullptr) << "a press on the chip opens the box";
    const auto knob = juce::Point<float>((chain->p1.x + chain->p2.x) * 0.5f, (chain->p1.y + chain->p2.y) * 0.5f);
    EXPECT_EQ(synth::ui::cable_curve_chips::chipAt(*f.editor, knob).uid, 0u) << "the knob itself is not the chip";
}

class CurvePreferenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        juce::PropertiesFile::Options options;
        options.applicationName = "ModCurvePrefTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        appProperties.setStorageParameters(options);
        appProperties.getUserSettings()->clear();
    }
    void TearDown() override { appProperties.getUserSettings()->clear(); }
    juce::ApplicationProperties appProperties;
};

TEST_F(CurvePreferenceTest, ShowCurvesOnCablesDefaultsOffPersistsAndReachesTheEditor) {
    {
        PreferencesSettingsTab tab(appProperties);
        EXPECT_FALSE(tab.isShowCurvesOnCablesEnabled());
        EXPECT_FALSE(appProperties.getUserSettings()->containsKey("showCurvesOnCables"));
        tab.setShowCurvesOnCablesEnabled(true);
    }
    PreferencesSettingsTab again(appProperties);
    EXPECT_TRUE(again.isShowCurvesOnCablesEnabled());
}

// Regression test for FRO1445: every curve editor draws its first and last dot whole and the shape edge to edge. One
// guard over every user of CurveEditorComponent: the envelope card, the LFO card and its morph card, the shape window
// (Filter and LFO) and the modulation curve box and window. A user missing from `registeredUsers()` fails the scan
// below, so a new editor cannot join without a dot check.
using synth::ui::CurveEditorComponent;
using synth::ui::CurveEditorGeometry;

namespace {

constexpr float kDotExtent = CurveEditorGeometry::kNodeMaxExtentPx;

/** The line as drawn runs from the first node to the last; each dot, at its widest, must lie inside the component. */
void expectEndDotsInside(CurveEditorComponent& editor, const juce::String& what) {
    ASSERT_GT(editor.getWidth(), 40) << what;
    const auto line = editor.getDrawnCurvePointsForTest();
    ASSERT_GE(line.size(), 2u) << what;
    EXPECT_GE(line.front().x - kDotExtent, 0.0f) << what << ": the first dot is cut by the left edge";
    EXPECT_LE(line.back().x + kDotExtent, (float)editor.getWidth())
        << what << ": the last dot is cut by the right edge";
}

/** A free-mode editor's shape spans the box: its ends sit at the insets and nowhere short of them. */
void expectSpansTheBox(CurveEditorComponent& editor, const juce::String& what) {
    const auto line = editor.getDrawnCurvePointsForTest();
    ASSERT_GE(line.size(), 2u) << what;
    EXPECT_NEAR(line.front().x, CurveEditorGeometry::kNodeInsetPx, 0.51f) << what;
    EXPECT_NEAR(line.back().x, (float)editor.getWidth() - CurveEditorGeometry::kNodeInsetPx, 0.51f)
        << what << ": the shape stops short of the right edge";
}

/** The ring of the first dot is painted at its left extreme: not clipped away by the edge. */
void expectFirstDotPaintedWhole(CurveEditorComponent& editor, const juce::String& what) {
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), false, 1.0f);
    const auto first = editor.getDrawnCurvePointsForTest().front();
    const auto ringLeft =
        image.getPixelAt((int)std::floor(first.x - CurveEditorGeometry::kNodeRadiusPx), (int)std::round(first.y));
    const auto beyond = image.getPixelAt(0, (int)std::round(first.y));
    EXPECT_NE(ringLeft, beyond) << what << ": no ring pixel where the first dot's left edge should be";
}

std::set<juce::String> registeredUsers() {
    return {"Graph/CardBody/CardBodyViews.cpp", "Graph/ModuleComponent/ModuleComponentLfoCard.cpp",
            "Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h", "ModuleViews/ModCurve/ModCurveEditorView.h",
            "ModuleViews/ShapeEditor/ShapeListEditor.h"};
}

} // namespace

TEST_F(ModuleComponentTest, EveryCurveEditorUserIsCoveredByTheEdgeDotGuard) {
    auto root = juce::File::getCurrentWorkingDirectory().getChildFile(__FILE__).getParentDirectory();
    while (root.getParentDirectory() != root && !root.getChildFile("Source/UI").isDirectory())
        root = root.getParentDirectory();
    const auto source = root.getChildFile("Source/UI");
    ASSERT_TRUE(source.isDirectory()) << source.getFullPathName();
    std::set<juce::String> found;
    for (const auto& file : source.findChildFiles(juce::File::findFiles, true, "*.h;*.cpp")) {
        const auto text = file.loadFileAsString();
        const bool owns = text.contains("CurveEditorComponent curve") ||
                          text.contains("CurveEditorComponent editor_") ||
                          text.contains("make_unique<synth::ui::CurveEditorComponent>") ||
                          text.contains("make_unique<CurveEditorComponent>");
        if (owns)
            found.insert(file.getRelativePathFrom(source).replaceCharacter('\\', '/'));
    }
    EXPECT_FALSE(found.empty());
    for (const auto& path : found)
        EXPECT_TRUE(registeredUsers().count(path) > 0)
            << path << " owns a curve editor but the edge-dot guard does not cover it: add a case below";
}

TEST_F(ModuleComponentTest, ModCurveBoxAndWindowDrawBothEndDotsWholeAndSpanTheBox) {
    BoxRig box;
    box.box->setSize(320, 260);
    expectEndDotsInside(box.editor(), "mod curve box");
    expectSpansTheBox(box.editor(), "mod curve box");
    expectFirstDotPaintedWhole(box.editor(), "mod curve box");

    synth::ui::ModCurveWindowContent window(box.target);
    window.setSize(520, 440);
    expectEndDotsInside(window.view().editor(), "mod curve window");
    expectSpansTheBox(window.view().editor(), "mod curve window");
}

TEST_F(ModuleComponentTest, ShapeWindowsDrawBothEndDotsWhole) {
    synth::ui::test::ShapeRig filter;
    synth::ui::ShapeListEditor filterEditor(filter.adapter, 0);
    filterEditor.setSize(720, 460);
    expectEndDotsInside(filterEditor.getCurveEditor(), "filter shape window");
    expectFirstDotPaintedWhole(filterEditor.getCurveEditor(), "filter shape window");

    AudioEngine engine;
    auto* lfo = new LFOModule();
    const auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
    AppUndoManager undo;
    auto adapter = std::make_shared<synth::ui::LfoShapesAdapter>(*lfo, &undo, engine.getGraph(), node->nodeID, "LFO");
    synth::ui::ShapeListEditor lfoEditor(adapter, 0);
    lfoEditor.setSize(720, 460);
    expectEndDotsInside(lfoEditor.getCurveEditor(), "LFO shape window");
    expectSpansTheBox(lfoEditor.getCurveEditor(), "LFO shape window");
}

TEST_F(ModuleComponentTest, LfoAndEnvelopeCardsDrawBothEndDotsWhole) {
    AudioEngine engine;
    GraphEditor editor(engine);
    {
        LFOModule lfo;
        ModuleComponent card(&lfo, juce::AudioProcessorGraph::NodeID(1), editor);
        *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&lfo, "shape")) = LFOModule::kCustomShapeIndex;
        card.timerCallback();
        CurveEditorComponent* curve = nullptr;
        for (auto* child : card.getChildren())
            if (auto* c = dynamic_cast<CurveEditorComponent*>(child))
                curve = c;
        ASSERT_NE(curve, nullptr);
        expectEndDotsInside(*curve, "LFO card");
        expectSpansTheBox(*curve, "LFO card");
        expectFirstDotPaintedWhole(*curve, "LFO card");
    }
    {
        ADSRModule adsr;
        ModuleComponent card(&adsr, juce::AudioProcessorGraph::NodeID(2), editor);
        juce::ToggleButton* toggle = nullptr;
        for (auto* child : card.getChildren())
            if (auto* t = dynamic_cast<juce::ToggleButton*>(child))
                if (t->getButtonText() == "Show Envelope Graph")
                    toggle = t;
        ASSERT_NE(toggle, nullptr);
        toggle->setToggleState(true, juce::sendNotificationSync);
        CurveEditorComponent* curve = nullptr;
        for (auto* child : card.getChildren())
            if (auto* c = dynamic_cast<CurveEditorComponent*>(child))
                curve = c;
        ASSERT_NE(curve, nullptr);
        expectEndDotsInside(*curve, "envelope card");
        expectFirstDotPaintedWhole(*curve, "envelope card");
    }
}
