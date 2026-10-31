// The places that open a curve (dot-row chip, matrix chip, cable) all reach the one attenuverter curve, and the cable
// menu and cable chips behave by cable kind and by the preference.

#include "ModCurveTestRig.h"
#include "Modules/AttenuverterModule.h"
#include "UI/Graph/GraphEditor/GraphEditorCableCurveChips.h"
#include "UI/Graph/GraphEditor/GraphEditorCableMenu.h"
#include "UI/Graph/ModMatrixCurveColumn.h"
#include "UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h"

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
