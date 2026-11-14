// CurveEditorSurfaces.h -- the one list of surfaces that own a curve editor (mod curve box and window, LFO card, Filter
// card curve, Filter and LFO shape windows), walked by every guard that must hold on all of them: snap lands on lines,
// a delete eases, J flips Snap. A new surface that owns a `CurveEditorComponent` fails
// `EveryCurveEditorOwnerIsInTheSurfaceList` until it is added here. Header-only.
#pragma once

#include "../../Graph/ModuleComponent/ModuleComponentTestFixture.h"
#include "../ModCurve/ModCurveTestRig.h"
#include "../ShapeEditor/ShapeEditorTestHelpers.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/Filter/CustomFilterShapes.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include "UI/ModuleViews/ModCurve/ModCurveWindow.h"
#include "UI/ModuleViews/ShapeEditor/LfoShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"
#include <functional>
#include <set>

namespace synth::ui::test {

/** Runs `check(editor, name)` on every surface's curve editor, each built fresh and kept alive for the call. */
inline void forEachCurveEditorSurface(const std::function<void(CurveEditorComponent&, const juce::String&)>& check) {
    {
        BoxRig box;
        box.box->setSize(320, 260);
        check(box.editor(), "mod curve box");
    }
    {
        // Its own rig: the box and the window edit one curve, and the box's sweep leaves it changed.
        BoxRig box;
        ModCurveWindowContent window(box.target);
        window.setSize(520, 440);
        check(window.view().editor(), "mod curve window");
    }
    {
        ShapeRig filter;
        ShapeListEditor filterEditor(filter.adapter, 0);
        filterEditor.setSize(720, 460);
        check(filterEditor.getCurveEditor(), "filter shape window");
    }
    {
        AudioEngine engine;
        auto* lfo = new LFOModule();
        const auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
        AppUndoManager undo;
        auto adapter = std::make_shared<LfoShapesAdapter>(*lfo, &undo, engine.getGraph(), node->nodeID, "LFO");
        ShapeListEditor lfoEditor(adapter, 0);
        lfoEditor.setSize(720, 460);
        check(lfoEditor.getCurveEditor(), "LFO shape window");
    }
    {
        AudioEngine engine;
        GraphEditor editor(engine);
        LFOModule lfo;
        ModuleComponent card(&lfo, juce::AudioProcessorGraph::NodeID(1), editor);
        *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&lfo, "shape")) = LFOModule::kCustomShapeIndex;
        card.timerCallback();
        CurveEditorComponent* curve = nullptr;
        for (auto* child : card.getChildren())
            if (auto* c = dynamic_cast<CurveEditorComponent*>(child))
                curve = c;
        ASSERT_NE(curve, nullptr);
        check(*curve, "LFO card");
    }
    {
        AudioEngine engine;
        auto* filter = new FilterModule();
        const auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(filter));
        GraphEditor editor(engine);
        *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(filter, "filterType")) =
            FilterModule::kCustomTypeIndex;
        ModuleComponent card(filter, node->nodeID, editor);
        // The card's view edits the shape under the morph cursor; between dots it is a read-only blend.
        const auto first = CustomFilterShapes::defaultShapes().shapes.front();
        *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(filter, "morphX")) = first.position[0];
        *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(filter, "morphY")) = first.position[1];
        card.timerCallback();
        ASSERT_NE(card.getMorphCard(), nullptr);
        check(card.getMorphCard()->getCurveView(), "Filter card curve");
    }
}

/** The source files that own a curve editor and where each is driven (the envelope card's is a fixed-shape plateau
 *  curve with no grid, no snap and no point edits). */
inline std::set<juce::String> curveEditorOwners() {
    return {"Graph/CardBody/CardBodyViews.cpp", "Graph/ModuleComponent/ModuleComponentLfoCard.cpp",
            "Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h", "ModuleViews/ModCurve/ModCurveEditorView.cpp",
            "ModuleViews/ShapeEditor/ShapeListEditor.h"};
}

/** Fails for every file under Source/UI that owns a curve editor and is not in `curveEditorOwners()`. */
inline void expectEveryCurveEditorOwnerIsListed(const juce::String& thisTest) {
    auto root = juce::File::getCurrentWorkingDirectory();
    while (root.getParentDirectory() != root &&
           !(root.getChildFile("Source/UI").isDirectory() && root.getChildFile(thisTest).existsAsFile()))
        root = root.getParentDirectory();
    const auto source = root.getChildFile("Source/UI");
    ASSERT_TRUE(source.isDirectory()) << source.getFullPathName();
    bool any = false;
    for (const auto& file : source.findChildFiles(juce::File::findFiles, true, "*.h;*.cpp")) {
        const auto text = file.loadFileAsString();
        if (!(text.contains("CurveEditorComponent curve") || text.contains("CurveEditorComponent editor_") ||
              text.contains("make_unique<synth::ui::CurveEditorComponent>") ||
              text.contains("make_unique<CurveEditorComponent>")))
            continue;
        any = true;
        const auto path = file.getRelativePathFrom(source).replaceCharacter('\\', '/');
        EXPECT_TRUE(curveEditorOwners().count(path) > 0)
            << path << " owns a curve editor that the shared surface list does not drive: add it to "
            << "forEachCurveEditorSurface in CurveEditorSurfaces.h";
    }
    EXPECT_TRUE(any);
}

} // namespace synth::ui::test
