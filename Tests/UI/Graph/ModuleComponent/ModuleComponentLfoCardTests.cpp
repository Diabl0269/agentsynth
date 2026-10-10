// ModuleComponentLfoCard tests: the Custom-waveform section's visibility (shape-gated),
// the curve model built from LFOModule's custom wave, two-way sync driven through REAL
// synthesized mouse events (not the model's primitives directly -- see CurveEditorComponent's
// dragFrozenRange_ doc comment / CurveEditorTestHelpers.h), undo-gesture bracketing via
// recordNodeExtraStateChange, the generation-poll reverse sync, presets/tools, snap defaults, the
// playhead, and the context menu built through setShowContextMenuHookForTest.
#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"

#include "Modules/LFOModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::LfoCustomWave;
using synth::ui::CurveEditorComponent;
using synth::ui::CurveEditorGeometry;
using synth::ui::CurveHitKind;

namespace {

CurveEditorComponent* findLfoCurveEditor(ModuleComponent& comp) {
    for (auto* child : comp.getChildren())
        if (auto* curve = dynamic_cast<CurveEditorComponent*>(child))
            return curve;
    return nullptr;
}

juce::ComboBox* findLfoGridCombo(ModuleComponent& comp) {
    for (auto* child : comp.getChildren())
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child))
            if (combo->getNumItems() == 7 && combo->getItemText(0) == "Grid Off")
                return combo;
    return nullptr;
}

juce::TextButton* findButtonByText(ModuleComponent& comp, const juce::String& text) {
    for (auto* child : comp.getChildren())
        if (auto* btn = dynamic_cast<juce::TextButton*>(child))
            if (btn->getButtonText() == text)
                return btn;
    return nullptr;
}

juce::MouseEvent curveMouseEvent(juce::Component& comp, juce::Point<float> pos, bool dragged,
                                 juce::Point<float> downPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), 1,
                            dragged);
}

// createLfoCardControls configures the real component with setVisibleRangeOverride(1.0) and
// setZeroSegmentPx(0.0f) -- a geometry built with CurveEditorGeometry's own bare defaults would
// map pixels differently from the component's actual internal currentGeometry(), which is
// exactly what the real mouseDown/mouseDrag/mouseDoubleClick hit-testing uses. Every geometry a
// test builds to compute a click/drag point must mirror that config, or the computed point misses
// its intended target by more than kHitRadiusPx.
CurveEditorGeometry lfoGeometry(CurveEditorComponent& curve) {
    synth::ui::CurveGeometryConfig config;
    config.explicitVisibleRange = 1.0;
    config.zeroSegmentPx = 0.0f;
    return CurveEditorGeometry(curve.getModel(), curve.getLocalBounds().toFloat(), config);
}

void selectShape(juce::AudioProcessor& processor, int index) {
    *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&processor, "shape")) = index;
}

} // namespace

TEST_F(ModuleComponentTest, LfoCustomSectionHiddenForBuiltInShapesAndCardHeightUnchanged) {
    AudioEngine engine;
    GraphEditor editor(engine);
    LFOModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* curve = findLfoCurveEditor(moduleComponent);
    ASSERT_NE(curve, nullptr);
    EXPECT_FALSE(curve->isVisible()) << "default shape is Sine -- the section must be hidden";
}

TEST_F(ModuleComponentTest, ShapeCustomShowsEditorAndToolbarAndGrowsTheCard) {
    AudioEngine engine;
    GraphEditor editor(engine);
    LFOModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* curve = findLfoCurveEditor(moduleComponent);
    ASSERT_NE(curve, nullptr);
    const int heightBefore = moduleComponent.getHeight();

    selectShape(processor, LFOModule::kCustomShapeIndex);

    EXPECT_TRUE(curve->isVisible());
    EXPECT_NE(findLfoGridCombo(moduleComponent), nullptr);
    EXPECT_NE(findButtonByText(moduleComponent, "Shapes"), nullptr);
    EXPECT_NE(findButtonByText(moduleComponent, "Tools"), nullptr);
    EXPECT_GT(moduleComponent.getHeight(), heightBefore) << "showing the section must grow the card";
    EXPECT_TRUE(moduleComponent.getLocalBounds().contains(curve->getBounds()));

    selectShape(processor, 0); // back to Sine
    EXPECT_FALSE(curve->isVisible());
    EXPECT_EQ(moduleComponent.getHeight(), heightBefore) << "hiding it again must shrink the card back";
}

TEST_F(ModuleComponentTest, CurveModelMatchesModuleWaveOnConstruction) {
    AudioEngine engine;
    GraphEditor editor(engine);
    LFOModule processor;
    processor.setCustomWave(LfoCustomWave::preset(LfoCustomWave::Preset::Square));
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* curve = findLfoCurveEditor(moduleComponent);
    ASSERT_NE(curve, nullptr);
    const auto& model = curve->getModel();
    const auto square = LfoCustomWave::preset(LfoCustomWave::Preset::Square);
    ASSERT_EQ(model.getNumNodes(), (int)square.points.size());
    for (int i = 0; i < model.getNumNodes(); ++i) {
        EXPECT_NEAR(model.getNode(i).x, (double)square.points[(size_t)i].x, 1e-6);
        EXPECT_NEAR(model.getNode(i).y, square.points[(size_t)i].y, 1e-6);
    }
}

TEST_F(ModuleComponentTest, RealMouseDragWritesWaveToModuleWithOneUndoStep) {
    AudioEngine engine;
    auto* lfo = new LFOModule();
    auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
    ASSERT_NE(node, nullptr);
    GraphEditor editor(engine);
    AppUndoManager undoManager;
    ModuleComponent moduleComponent(lfo, node->nodeID, editor, &undoManager);
    selectShape(*lfo, LFOModule::kCustomShapeIndex);

    auto* curve = findLfoCurveEditor(moduleComponent);
    ASSERT_NE(curve, nullptr);
    ASSERT_GT(curve->getWidth(), 0);

    const CurveEditorGeometry geometry = lfoGeometry(*curve);
    const auto midPos = geometry.nodePosition(1); // the mid-point of the Triangle default (y == 1.0, the top)
    const auto targetPos =
        midPos + juce::Point<float>(0.0f, 20.0f); // down: y is already maxed, so up would clamp to a no-op

    const int serialBefore = undoManager.getEditSerial();
    curve->mouseDown(curveMouseEvent(*curve, midPos, false, midPos));
    curve->mouseDrag(curveMouseEvent(*curve, targetPos, true, midPos));
    curve->mouseUp(curveMouseEvent(*curve, targetPos, true, midPos));

    EXPECT_TRUE(lfo->getExtraState().isVoid() == false) << "the drag must have written a non-default wave";
    EXPECT_EQ(undoManager.getEditSerial(), serialBefore + 1) << "one whole drag must cost exactly one undo step";

    const auto afterDrag = lfo->getCustomWave();
    undoManager.undo();
    moduleComponent.timerCallback(); // the card re-syncs via the generation poll
    EXPECT_NE(lfo->getCustomWave(), afterDrag) << "undo must restore the pre-drag wave";
    EXPECT_TRUE(lfo->getCustomWave().isDefault());
}

TEST_F(ModuleComponentTest, DoubleClickAddIsOneUndoStepThatUndoActuallyRemoves) {
    AudioEngine engine;
    auto* lfo = new LFOModule();
    auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
    GraphEditor editor(engine);
    AppUndoManager undoManager;
    ModuleComponent moduleComponent(lfo, node->nodeID, editor, &undoManager);
    selectShape(*lfo, LFOModule::kCustomShapeIndex);

    auto* curve = findLfoCurveEditor(moduleComponent);
    ASSERT_NE(curve, nullptr);
    const int pointsBefore = lfo->getCustomWave().points.size();

    const CurveEditorGeometry geometry = lfoGeometry(*curve);
    const auto emptySpot = geometry.nodePosition(0) + juce::Point<float>(30.0f, -30.0f);
    const int serialBefore = undoManager.getEditSerial();
    curve->mouseDoubleClick(curveMouseEvent(*curve, emptySpot, false, emptySpot));

    EXPECT_EQ((int)lfo->getCustomWave().points.size(), pointsBefore + 1);
    EXPECT_EQ(undoManager.getEditSerial(), serialBefore + 1);

    undoManager.undo();
    moduleComponent.timerCallback();
    EXPECT_EQ((int)lfo->getCustomWave().points.size(), pointsBefore) << "undo must actually remove the added point";
}

TEST_F(ModuleComponentTest, DoubleClickRemoveAndBendDragRoundTrip) {
    AudioEngine engine;
    auto* lfo = new LFOModule();
    auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
    GraphEditor editor(engine);
    AppUndoManager undoManager;
    ModuleComponent moduleComponent(lfo, node->nodeID, editor, &undoManager);
    selectShape(*lfo, LFOModule::kCustomShapeIndex);

    auto* curve = findLfoCurveEditor(moduleComponent);
    ASSERT_NE(curve, nullptr);

    // Bend drag on segment 0 (origin -> mid-point).
    const CurveEditorGeometry geometry = lfoGeometry(*curve);
    const auto handle = geometry.bendHandlePosition(0);
    ASSERT_TRUE(handle.has_value());
    curve->mouseDown(curveMouseEvent(*curve, *handle, false, *handle));
    const auto bendTarget = *handle + juce::Point<float>(0.0f, 20.0f);
    curve->mouseDrag(curveMouseEvent(*curve, bendTarget, true, *handle));
    curve->mouseUp(curveMouseEvent(*curve, bendTarget, true, *handle));
    EXPECT_NE(lfo->getCustomWave().points[0].bend, 0.0f);

    // Double-click the mid-point to remove it.
    const CurveEditorGeometry geometryAfterBend = lfoGeometry(*curve);
    const auto midPos = geometryAfterBend.nodePosition(1);
    const int pointsBefore = (int)lfo->getCustomWave().points.size();
    curve->mouseDoubleClick(curveMouseEvent(*curve, midPos, false, midPos));
    EXPECT_EQ((int)lfo->getCustomWave().points.size(), pointsBefore - 1);
}

TEST_F(ModuleComponentTest, SnapOnCardDefaultsToEighthsAndOffDisablesIt) {
    AudioEngine engine;
    GraphEditor editor(engine);
    LFOModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);
    selectShape(processor, LFOModule::kCustomShapeIndex);

    auto* combo = findLfoGridCombo(moduleComponent);
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->getText(), "Grid 1/8") << "1/8 is the documented default";

    auto* curve = findLfoCurveEditor(moduleComponent);
    ASSERT_NE(curve, nullptr);
    combo->setSelectedItemIndex(0, juce::sendNotificationSync); // "Off"
    // Off must disable snap: a drag a few px off a coarse grid line stays off-grid.
    const CurveEditorGeometry geometry = lfoGeometry(*curve);
    const auto midPos = geometry.nodePosition(1);
    const auto off = midPos + juce::Point<float>(3.0f, 3.0f);
    curve->mouseDown(curveMouseEvent(*curve, midPos, false, midPos));
    curve->mouseDrag(curveMouseEvent(*curve, off, true, midPos));
    const double x = curve->getModel().getNode(1).x;
    EXPECT_NE(std::fmod(x * 4.0, 1.0), 0.0) << "Off disables snap entirely";
}

TEST_F(ModuleComponentTest, GridComboOffersTheFinerSixtyFourthAndHundredTwentyEighth) {
    AudioEngine engine;
    GraphEditor editor(engine);
    LFOModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);
    selectShape(processor, LFOModule::kCustomShapeIndex);

    auto* combo = findLfoGridCombo(moduleComponent);
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->getItemText(5), "Grid 1/64");
    EXPECT_EQ(combo->getItemText(6), "Grid 1/128");

    auto* curve = findLfoCurveEditor(moduleComponent);
    ASSERT_NE(curve, nullptr);
    combo->setSelectedItemIndex(6, juce::sendNotificationSync);
    const CurveEditorGeometry geometry = lfoGeometry(*curve);
    const auto midPos = geometry.nodePosition(1);
    const auto off = midPos + juce::Point<float>(3.0f, 3.0f);
    curve->mouseDown(curveMouseEvent(*curve, midPos, false, midPos));
    curve->mouseDrag(curveMouseEvent(*curve, off, true, midPos));
    const double x = curve->getModel().getNode(1).x * 128.0;
    EXPECT_NEAR(x, std::round(x), 1e-6) << "1/128 snaps x to a 128th of the cycle";
}

TEST_F(ModuleComponentTest, PresetsAndToolsApplyAndAreUndoable) {
    AudioEngine engine;
    auto* lfo = new LFOModule();
    auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
    GraphEditor editor(engine);
    AppUndoManager undoManager;
    ModuleComponent moduleComponent(lfo, node->nodeID, editor, &undoManager);
    selectShape(*lfo, LFOModule::kCustomShapeIndex);

    const int serialBefore = undoManager.getEditSerial();
    moduleComponent.applyLfoWavePreset(3); // Square
    EXPECT_EQ(lfo->getCustomWave(), LfoCustomWave::preset(LfoCustomWave::Preset::Square));
    EXPECT_EQ(undoManager.getEditSerial(), serialBefore + 1);

    moduleComponent.applyLfoWaveTool(0); // Invert
    EXPECT_EQ(lfo->getCustomWave(), []() {
        auto w = LfoCustomWave::preset(LfoCustomWave::Preset::Square);
        w.apply(LfoCustomWave::Tool::Invert);
        return w;
    }());
    EXPECT_EQ(undoManager.getEditSerial(), serialBefore + 2);

    undoManager.undo();
    moduleComponent.timerCallback();
    EXPECT_EQ(lfo->getCustomWave(), LfoCustomWave::preset(LfoCustomWave::Preset::Square));
}

TEST_F(ModuleComponentTest, OwnDragDoesNotTriggerGenerationResync) {
    AudioEngine engine;
    auto* lfo = new LFOModule();
    auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
    GraphEditor editor(engine);
    AppUndoManager undoManager;
    ModuleComponent moduleComponent(lfo, node->nodeID, editor, &undoManager);
    selectShape(*lfo, LFOModule::kCustomShapeIndex);

    auto* curve = findLfoCurveEditor(moduleComponent);
    ASSERT_NE(curve, nullptr);
    moduleComponent.applyLfoWavePreset(3); // Square -- own write, updates lfoLastSeenWaveGeneration

    const auto modelBeforeTick = curve->getModel();
    moduleComponent.timerCallback(); // must NOT rebuild the model (no generation mismatch)
    EXPECT_EQ(curve->getModel().getNumNodes(), modelBeforeTick.getNumNodes());
    for (int i = 0; i < curve->getModel().getNumNodes(); ++i)
        EXPECT_FLOAT_EQ(curve->getModel().getNode(i).y, modelBeforeTick.getNode(i).y);
}

TEST_F(ModuleComponentTest, PlayheadFollowsModulePhaseOnlyWhileVisible) {
    AudioEngine engine;
    GraphEditor editor(engine);
    LFOModule processor;
    processor.prepareToPlay(44100.0, 512);
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    auto* curve = findLfoCurveEditor(moduleComponent);
    ASSERT_NE(curve, nullptr);

    // Hidden (default Sine): the tick must not touch the playhead.
    moduleComponent.timerCallback();
    EXPECT_FALSE(curve->getPlayhead().has_value());

    selectShape(processor, LFOModule::kCustomShapeIndex);
    juce::AudioBuffer<float> buf(3, 16);
    juce::MidiBuffer midi;
    processor.processBlock(buf, midi);

    moduleComponent.timerCallback();
    EXPECT_TRUE(curve->getPlayhead().has_value()) << "visible + a real phase must show a playhead";
}

TEST_F(ModuleComponentTest, ContextMenuBuildsShapesToolsGridItems) {
    AudioEngine engine;
    GraphEditor editor(engine);
    LFOModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);
    selectShape(processor, LFOModule::kCustomShapeIndex);

    auto* curve = findLfoCurveEditor(moduleComponent);
    ASSERT_NE(curve, nullptr);

    std::vector<juce::String> topLevelItems;
    bool deletePointEnabled = true;
    moduleComponent.setShowContextMenuHookForTest([&](juce::PopupMenu& menu) {
        juce::PopupMenu::MenuItemIterator it(menu, false);
        while (it.next()) {
            const auto& item = it.getItem();
            topLevelItems.push_back(item.text);
            if (item.text == "Delete Point")
                deletePointEnabled = item.isEnabled;
        }
    });

    const CurveEditorGeometry geometry = lfoGeometry(*curve);
    // Right-click on an interior (movable) point: "Delete Point" must be present and enabled.
    const auto midPos = geometry.nodePosition(1);
    curve->mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), midPos,
                                      juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 0.0f, 0.0f, 0.0f,
                                      0.0f, 0.0f, curve, curve, juce::Time::getCurrentTime(), midPos,
                                      juce::Time::getCurrentTime(), 1, false));
    EXPECT_NE(std::find(topLevelItems.begin(), topLevelItems.end(), "Delete Point"), topLevelItems.end());
    EXPECT_TRUE(deletePointEnabled);
    EXPECT_NE(std::find(topLevelItems.begin(), topLevelItems.end(), "Shapes"), topLevelItems.end());
    EXPECT_NE(std::find(topLevelItems.begin(), topLevelItems.end(), "Grid"), topLevelItems.end());
    EXPECT_NE(std::find(topLevelItems.begin(), topLevelItems.end(), "Invert"), topLevelItems.end());

    // Right-click an endpoint: "Delete Point" must be present but disabled.
    topLevelItems.clear();
    const auto originPos = geometry.nodePosition(0);
    curve->mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), originPos,
                                      juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 0.0f, 0.0f, 0.0f,
                                      0.0f, 0.0f, curve, curve, juce::Time::getCurrentTime(), originPos,
                                      juce::Time::getCurrentTime(), 1, false));
    EXPECT_FALSE(deletePointEnabled) << "an endpoint can never be deleted";
}

// Opt-in visual check, the LFO twin of AdsrCardRendersToPngForVisualInspection: renders a Custom
// LFO card (Soft Sine preset) with the app's real LookAndFeel. Set LFO_CARD_PNG=<path> to write it.
TEST_F(ModuleComponentTest, LfoCustomCardRendersToPngForVisualInspection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    LFOModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);
    selectShape(processor, LFOModule::kCustomShapeIndex);
    processor.setCustomWave(synth::LfoCustomWave::preset(synth::LfoCustomWave::Preset::SoftSine));
    moduleComponent.timerCallback(); // generation poll re-syncs the editor from the module

    synth::theme::AppLookAndFeel lf;
    moduleComponent.setLookAndFeel(&lf);
    juce::Image img(juce::Image::ARGB, moduleComponent.getWidth(), moduleComponent.getHeight(), true,
                    juce::SoftwareImageType());
    juce::Graphics g(img);
    moduleComponent.paintEntireComponent(g, true);
    moduleComponent.setLookAndFeel(nullptr);

    const char* pngPath = std::getenv("LFO_CARD_PNG");
    if (pngPath == nullptr || juce::String(pngPath).isEmpty())
        GTEST_SKIP() << "set LFO_CARD_PNG=<path> to write the rendered card for visual inspection";
    juce::File outFile(pngPath);
    outFile.deleteFile();
    juce::FileOutputStream stream(outFile);
    ASSERT_TRUE(stream.openedOk());
    juce::PNGImageFormat png;
    ASSERT_TRUE(png.writeImageToStream(img, stream));
}
