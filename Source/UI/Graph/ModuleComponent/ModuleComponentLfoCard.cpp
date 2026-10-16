// ModuleComponentLfoCard.cpp -- the LFO card's Custom-waveform section: the Grid/
// Shapes/Tools toolbar + Free-mode curve editor, the two-way sync between it and LFOModule's
// custom wave (getExtraState/setExtraState), undo-gesture wiring, the generation-poll reverse
// sync, and the playhead poll. ModuleComponent is declared in ModuleComponent.h; the rest of its
// implementation lives in the sibling ModuleComponent*.cpp units next to this one.
// ModuleComponentEnvelopeCard.cpp is the pattern this
// mirrors throughout.
#include "AudioEngine/AudioEngine.h"
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/LFOModule.h"
#include "Modules/Shapes/MorphShapes.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h"
#include "UI/ModuleViews/CurveEditor/CurveWaveConversion.h"
#include <cmath>

using namespace detail;
using synth::LfoCustomWave;
using synth::ui::CurveHitKind;
using synth::ui::CurveHitResult;
using synth::ui::CurvePlayhead;

namespace {

constexpr int kLfoWaveGraphHeight = 150;
constexpr int kLfoGridComboWidth = 96;
constexpr int kLfoToolbarButtonWidth = 74;
constexpr int kLfoToolbarGap = 6;

// Preset/tool display names, index-parallel to synth::LfoCustomWave::Preset and the 0-3 range of
// synth::LfoCustomWave::Tool (index 4 is "Reset to Default", handled outside the enum -- see
// ModuleComponent::applyLfoWaveTool). Plain ASCII only (Source/CLAUDE.md).
const char* const kLfoPresetNames[] = {"Triangle",  "Ramp Up",   "Ramp Down", "Square",
                                       "Pulse 1/4", "Steps (4)", "Soft Sine"};
const char* const kLfoToolNames[] = {"Invert", "Reverse", "Straighten", "Clear", "Reset to Default"};
// Grid combo entries: divisions-per-axis (0 == Off), index-parallel to the display label.
const int kLfoGridDivisions[] = {0, 4, 8, 16, 32, 64, 128};
const char* const kLfoGridLabels[] = {"Grid Off",  "Grid 1/4",  "Grid 1/8",  "Grid 1/16",
                                      "Grid 1/32", "Grid 1/64", "Grid 1/128"};

LFOModule* asLfo(juce::AudioProcessor* module) { return dynamic_cast<LFOModule*>(module); }

/** The shape the morph cursor is on: the one the small curve editor shows and edits (shape 0 with one shape). */
int nearestLfoShape(const LFOModule& lfo) {
    return synth::morph::nearestShapeIndex(lfo.getCustomShapes().shapes, lfo.getCurrentMorphX(),
                                           lfo.getCurrentMorphY());
}

/** The curve editor's accessible name says which shape it edits once there is a choice. */
void describeEditedShape(synth::ui::CurveEditorComponent& editor, const LFOModule& lfo, int index) {
    const auto& shapes = lfo.getCustomShapes().shapes;
    juce::String text = "Custom waveform of the LFO";
    if (shapes.size() > 1)
        text << ", editing "
             << (shapes[(size_t)index].name.isEmpty() ? "shape " + juce::String(index + 1)
                                                      : shapes[(size_t)index].name);
    editor.setDescription(text);
}

} // namespace

void ModuleComponent::createLfoCardControls() {
    auto* lfo = asLfo(module);
    if (lfo == nullptr)
        return;

    lfoCurveEditor = std::make_unique<synth::ui::CurveEditorComponent>();
    lfoCurveEditor->setTitle("LFO custom wave");
    lfoCurveEditor->setDescription("Custom waveform of the LFO");
    lfoEditedShape_ = nearestLfoShape(*lfo);
    describeEditedShape(*lfoCurveEditor, *lfo, lfoEditedShape_);
    lfoCurveEditor->setModel(
        synth::ui::curveModelFromWave(lfo->getCustomShapes().shapes[(size_t)lfoEditedShape_].curve));
    lfoCurveEditor->setVisibleRangeOverride(1.0);
    lfoCurveEditor->setZeroSegmentPx(0.0f);
    lfoCurveEditor->setTimeLabelFormatter([](double) { return juce::String(); });
    lfoCurveEditor->setGrid(synth::ui::CurveEditorComponent::CurveGrid{8, 8});
    lfoCurveEditor->setSnapToGrid(true);
    lfoCurveEditor->setVisible(false); // syncLfoCustomSectionVisibility() decides the real state
    addChildComponent(lfoCurveEditor.get());

    wireLfoGestureCallbacks();
    lfoCurveEditor->onNodeChanged = [this](int) { writeLfoWaveFromCurve(); };
    lfoCurveEditor->onBendChanged = [this](int) { writeLfoWaveFromCurve(); };
    lfoCurveEditor->onPointsChanged = [this] { writeLfoWaveFromCurve(); };
    // With several shapes a double-click on empty space opens the shape editor (Shift keeps adding a point); with one
    // shape it adds a point as it always did.
    lfoCurveEditor->onEmptyDoubleClick = [this](const juce::MouseEvent& e) {
        if (morphCard_ == nullptr || e.mods.isShiftDown() || morphCard_->getNumShapes() < 2)
            return false;
        morphCard_->openEditor(lfoEditedShape_);
        return true;
    };
    lfoCurveEditor->onContextMenu = [this](const juce::MouseEvent& e, CurveHitResult hit) {
        showLfoWaveContextMenu(e, hit);
    };

    lfoGridCombo = std::make_unique<juce::ComboBox>();
    for (int i = 0; i < (int)std::size(kLfoGridLabels); ++i)
        lfoGridCombo->addItem(kLfoGridLabels[i], i + 1);
    lfoGridCombo->setSelectedId(3, juce::dontSendNotification); // 1/8, the default
    lfoGridCombo->setTitle("LFO custom wave grid");
    lfoGridCombo->setTooltip("Grid: the snap grid of the custom wave editor");
    lfoGridCombo->onChange = [this] {
        if (lfoGridCombo == nullptr)
            return;
        const int i = lfoGridCombo->getSelectedId() - 1;
        if (i >= 0 && i < (int)std::size(kLfoGridDivisions))
            setLfoGridDivisions(kLfoGridDivisions[i]);
    };
    addChildComponent(lfoGridCombo.get()); // the custom section's fade shows it

    lfoShapesButton = std::make_unique<juce::TextButton>("Shapes");
    lfoShapesButton->onClick = [this] {
        juce::PopupMenu menu;
        juce::Component::SafePointer<ModuleComponent> safeThis(this);
        for (int i = 0; i < (int)std::size(kLfoPresetNames); ++i)
            menu.addItem(kLfoPresetNames[i], [safeThis, i] {
                if (safeThis != nullptr)
                    safeThis->applyLfoWavePreset(i);
            });
        showContextMenuHook_(menu);
    };
    addChildComponent(lfoShapesButton.get());

    lfoToolsButton = std::make_unique<juce::TextButton>("Tools");
    lfoToolsButton->onClick = [this] {
        juce::PopupMenu menu;
        juce::Component::SafePointer<ModuleComponent> safeThis(this);
        for (int i = 0; i < (int)std::size(kLfoToolNames); ++i)
            menu.addItem(kLfoToolNames[i], [safeThis, i] {
                if (safeThis != nullptr)
                    safeThis->applyLfoWaveTool(i);
            });
        showContextMenuHook_(menu);
    };
    addChildComponent(lfoToolsButton.get());
    attachBlockFade(lfoFade_, {lfoCurveEditor.get(), lfoGridCombo.get(), lfoShapesButton.get(), lfoToolsButton.get()});

    lfoLastSeenWaveGeneration = lfo->getCustomWaveGeneration();
    syncLfoCustomSectionVisibility();

    // createControls()'s own tail already ran updateLayout() once, before these children existed
    // -- mirrors createEnvelopeCardControls()'s identical need.
    updateLayout();
}

void ModuleComponent::wireLfoGestureCallbacks() {
    if (lfoCurveEditor == nullptr)
        return;

    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    lfoCurveEditor->onGestureStart = [safeThis] {
        if (safeThis == nullptr)
            return;
        safeThis->lfoCurveGestureActive = true;
        if (auto* lfo = asLfo(safeThis->module))
            safeThis->lfoWaveBefore = lfo->getExtraState();
    };
    lfoCurveEditor->onGestureEnd = [safeThis] {
        if (safeThis == nullptr)
            return;
        safeThis->writeLfoWaveFromCurve();
        if (auto* lfo = asLfo(safeThis->module)) {
            const juce::var after = lfo->getExtraState();
            if (safeThis->undoManager != nullptr)
                safeThis->undoManager->recordNodeExtraStateChange(safeThis->owner.getAudioEngine().getGraph(),
                                                                  safeThis->nodeId, safeThis->lfoWaveBefore, after);
        }
        safeThis->lfoCurveGestureActive = false;
    };
}

void ModuleComponent::writeLfoWaveFromCurve() {
    auto* lfo = asLfo(module);
    if (lfoCurveEditor == nullptr || lfo == nullptr)
        return;
    lfo->setCustomShapeCurve(lfoEditedShape_, synth::ui::waveFromCurveModel(lfoCurveEditor->getModel()));
    lfoLastSeenWaveGeneration = lfo->getCustomWaveGeneration();
}

void ModuleComponent::syncLfoCurveFromModule() {
    // Message-thread only -- callers (parameterValueChanged, the 15 Hz tick) are responsible for
    // marshalling, exactly like syncEnvelopeCurveFromParams.
    auto* lfo = asLfo(module);
    if (lfoCurveEditor == nullptr || lfo == nullptr || lfoCurveGestureActive)
        return;
    lfoEditedShape_ = nearestLfoShape(*lfo);
    describeEditedShape(*lfoCurveEditor, *lfo, lfoEditedShape_);
    lfoCurveEditor->setModel(
        synth::ui::curveModelFromWave(lfo->getCustomShapes().shapes[(size_t)lfoEditedShape_].curve));
    lfoLastSeenWaveGeneration = lfo->getCustomWaveGeneration();
}

void ModuleComponent::syncLfoCustomSectionVisibility() {
    auto* lfo = asLfo(module);
    if (lfoCurveEditor == nullptr || lfo == nullptr)
        return;

    auto* shapeParam = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(module, "shape"));
    const bool isCustom = shapeParam != nullptr && shapeParam->getIndex() == LFOModule::kCustomShapeIndex;

    auto* bipolarParam = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(module, "bipolar"));
    const bool bipolar = bipolarParam != nullptr && bipolarParam->get();
    lfoCurveEditor->setFillBaselineLevel(bipolar ? 0.5f : 0.0f);

    // The editor and its toolbar fade in and out with their height (docs/layout/animation.md#fading-things-in-and-out).
    fadeBlock(lfoFade_, isCustom);
}

void ModuleComponent::applyLfoWavePreset(int presetIndex) {
    auto* lfo = asLfo(module);
    if (lfo == nullptr || presetIndex < 0 || presetIndex >= (int)std::size(kLfoPresetNames))
        return;

    const juce::var before = lfo->getExtraState();
    lfo->setCustomShapeCurve(lfoEditedShape_, LfoCustomWave::preset((LfoCustomWave::Preset)presetIndex));
    lfoLastSeenWaveGeneration = lfo->getCustomWaveGeneration();
    syncLfoCurveFromModule();
    if (undoManager != nullptr)
        undoManager->recordNodeExtraStateChange(owner.getAudioEngine().getGraph(), nodeId, before,
                                                lfo->getExtraState());
}

void ModuleComponent::applyLfoWaveTool(int toolIndex) {
    auto* lfo = asLfo(module);
    if (lfo == nullptr || toolIndex < 0 || toolIndex >= (int)std::size(kLfoToolNames))
        return;

    const juce::var before = lfo->getExtraState();
    if (toolIndex == 4) { // Reset to Default
        lfo->setCustomShapeCurve(lfoEditedShape_, LfoCustomWave::defaultWave());
    } else {
        LfoCustomWave wave = lfo->getCustomShapes().shapes[(size_t)lfoEditedShape_].curve;
        wave.apply((LfoCustomWave::Tool)toolIndex);
        lfo->setCustomShapeCurve(lfoEditedShape_, wave);
    }
    lfoLastSeenWaveGeneration = lfo->getCustomWaveGeneration();
    syncLfoCurveFromModule();
    if (undoManager != nullptr)
        undoManager->recordNodeExtraStateChange(owner.getAudioEngine().getGraph(), nodeId, before,
                                                lfo->getExtraState());
}

void ModuleComponent::setLfoGridDivisions(int n) {
    if (lfoCurveEditor == nullptr)
        return;
    if (n <= 0) {
        lfoCurveEditor->setGrid(synth::ui::CurveEditorComponent::CurveGrid{4, 2});
        lfoCurveEditor->setSnapToGrid(false);
    } else {
        lfoCurveEditor->setGrid(synth::ui::CurveEditorComponent::CurveGrid{n, juce::jmin(n, 8)});
        lfoCurveEditor->setSnapToGrid(true);
    }
}

void ModuleComponent::showLfoWaveContextMenu(const juce::MouseEvent&, CurveHitResult hit) {
    if (lfoCurveEditor == nullptr)
        return;

    juce::PopupMenu menu;
    juce::Component::SafePointer<ModuleComponent> safeThis(this);

    if (hit.kind == CurveHitKind::Node) {
        const bool canDelete = lfoCurveEditor->getModel().canRemovePoint(hit.index);
        const int index = hit.index;
        menu.addItem("Delete Point", canDelete, false, [safeThis, index] {
            if (safeThis != nullptr && safeThis->lfoCurveEditor != nullptr)
                safeThis->lfoCurveEditor->removeNode(index);
        });
        menu.addSeparator();
    }

    juce::PopupMenu shapesMenu;
    for (int i = 0; i < (int)std::size(kLfoPresetNames); ++i)
        shapesMenu.addItem(kLfoPresetNames[i], [safeThis, i] {
            if (safeThis != nullptr)
                safeThis->applyLfoWavePreset(i);
        });
    menu.addSubMenu("Shapes", shapesMenu);

    for (int i = 0; i < (int)std::size(kLfoToolNames); ++i)
        menu.addItem(kLfoToolNames[i], [safeThis, i] {
            if (safeThis != nullptr)
                safeThis->applyLfoWaveTool(i);
        });

    juce::PopupMenu gridMenu;
    for (int i = 0; i < (int)std::size(kLfoGridLabels); ++i) {
        const int divisions = kLfoGridDivisions[i];
        gridMenu.addItem(juce::String(kLfoGridLabels[i]).fromFirstOccurrenceOf("Grid ", false, false),
                         [safeThis, divisions] {
                             if (safeThis != nullptr)
                                 safeThis->setLfoGridDivisions(divisions);
                         });
    }
    menu.addSubMenu("Grid", gridMenu);

    showContextMenuHook_(menu);
}

void ModuleComponent::updateLfoWavePlayhead() {
    auto* lfo = asLfo(module);
    if (lfoCurveEditor == nullptr || lfo == nullptr)
        return;
    // The cursor moved onto another shape (its knobs, the map, a cable, undo): the editor follows it.
    if (!lfoCurveGestureActive && nearestLfoShape(*lfo) != lfoEditedShape_)
        syncLfoCurveFromModule();
    if (!lfoCurveEditor->isVisible())
        return;

    const double phase = (double)lfo->getPhaseForUI();
    CurvePlayhead playhead = lfoCurveEditor->getModel().playheadForX(phase);
    // Quantise to whole editor pixels so setPlayhead's unchanged-value early return skips
    // redundant repaints (spec section 1 "Playhead").
    const auto geometry =
        synth::ui::CurveEditorGeometry(lfoCurveEditor->getModel(), lfoCurveEditor->getLocalBounds().toFloat());
    const float segmentPx = std::abs(
        geometry.nodePosition(juce::jmin(playhead.segment + 1, lfoCurveEditor->getModel().getNumNodes() - 1)).x -
        geometry.nodePosition(playhead.segment).x);
    if (segmentPx > 0.0f)
        playhead.progress = std::round(playhead.progress * segmentPx) / segmentPx;
    lfoCurveEditor->setPlayhead(playhead);
}

int ModuleComponent::layoutLfoCustomWaveSection(int y, int contentX, int contentW, bool apply) {
    if (lfoCurveEditor == nullptr || !lfoCurveEditor->isVisible())
        return y;

    // Each part takes its height in step with the fade, so the rows under the section slide.
    if (apply) {
        const int rowH = blockHeight(lfoFade_, kRowHeight);
        int x = contentX;
        lfoGridCombo->setBounds(x, y, kLfoGridComboWidth, rowH);
        x += kLfoGridComboWidth + kLfoToolbarGap;
        lfoShapesButton->setBounds(x, y, kLfoToolbarButtonWidth, rowH);
        x += kLfoToolbarButtonWidth + kLfoToolbarGap;
        lfoToolsButton->setBounds(x, y, kLfoToolbarButtonWidth, rowH);
    }
    y += blockHeight(lfoFade_, kRowHeight + 2);

    if (apply)
        lfoCurveEditor->setBounds(contentX, y, contentW, blockHeight(lfoFade_, kLfoWaveGraphHeight));
    y += blockHeight(lfoFade_, kLfoWaveGraphHeight + 8);

    return y;
}
